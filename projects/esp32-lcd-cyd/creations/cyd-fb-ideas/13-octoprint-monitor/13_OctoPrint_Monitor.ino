/*
  13_OctoPrint 印刷監察 / OctoPrint Monitor  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 私人群組「CYD - Cheap Yellow Display ESP32」
  Mel Patrick 嘅 OctoGlance（2026-09-29，15 讚 / 3 留言）：
  https://www.facebook.com/groups/8104937882859357/posts/29369865492606621/
  原作者 repo：https://github.com/wabbitguy/OctoGlance
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅程式，冇 copy 佢 repo。
  （未喺實機測試，只係 compile 通過。DEMO 唔使打印機。）

  功能 / Features
   - 橫向 320x240：檔名、進度條、噴頭/熱床溫度、剩餘時間、狀態
   - DEMO（預設）：自己行一段假打印，workshop 冇 OctoPrint 都睇到
   - LIVE（可選，#define ENABLE_LIVE）：撳屏幕切換。
       GET {OCTO_HOST}/api/job （檔名、進度、剩餘時間、狀態）
       再 GET {OCTO_HOST}/api/printer （噴頭同熱床溫度；job JSON 本身冇溫度）
   - 背面 RGB LED：DEMO 藍、Printing 綠、Heating 黃、錯誤紅
   - 屏幕用英文標籤（內置字型冇中文）

  Library：TFT_eSPI（Larry libraries.zip，已設定 CYD）、XPT2046_Touchscreen、
           ArduinoJson 6、WiFi / HTTPClient（ESP32 內置）
  Board：ESP32 Dev Module（ESP32 core 2.0.17）。Partition：預設 Default 4MB
*/

#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ======================= 改呢度就得 / USER SETTINGS =======================
#define ENABLE_LIVE      1        // 0 = 編譯時唔包含 LIVE（撳屏幕只會提示）
#define START_IN_DEMO    1        // 1 = 開機一定係 DEMO（workshop 用）

const char *WIFI_SSID     = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *OCTO_HOST     = "http://octopi.local";          // 例如 http://192.168.1.50
const char *OCTO_API_KEY  = "YOUR_OCTOPRINT_API_KEY";       // OctoPrint → Settings → API

#define POLL_MS          4000
#define HTTP_TIMEOUT_MS  3000
#define WIFI_WAIT_MS     12000

#define SCREEN_BRIGHT    200
#define RGB_LED_ENABLE   1
#define RGB_LED_LEVEL    40

#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800
#define TOUCH_MIN_Z 300
// ============================== 設定完 =====================================

#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21
#define LED_R        4
#define LED_G        16
#define LED_B        17
#define CH_BL 0
#define CH_R  1
#define CH_G  2
#define CH_B  3

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;

const int W = 320, H = 240;

uint16_t C_BG, C_PANEL, C_YEL, C_GRN, C_RED, C_DIM, C_CYN, C_WHT;

struct PrintView {
  char name[28];
  char state[20];
  float progress;       // 0–100, negative = unknown
  int timeLeft;         // seconds, negative = unknown
  float hotend, hotendT;
  float bed, bedT;
  bool haveTemp;
};

PrintView view;
bool liveMode = false;
bool wifiBegun = false;
uint32_t wifiStartMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastDrawMs = 0;
uint32_t lastTouchMs = 0;
bool touchWasDown = false;
char statusMsg[40] = "DEMO";
bool layoutDrawn = false;

const char *DEMO_FILES[] = { "cyd_case.gcode", "benchy.gcode", "name_tag.gcode" };
const int DEMO_N = 3;
int demoFile = 0;

bool credsLookReal() {
  if (!WIFI_SSID || !WIFI_PASSWORD || !OCTO_API_KEY) return false;
  if (strcmp(WIFI_SSID, "YOUR_WIFI_NAME") == 0) return false;
  if (strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0) return false;
  if (strcmp(OCTO_API_KEY, "YOUR_OCTOPRINT_API_KEY") == 0) return false;
  if (strlen(WIFI_SSID) == 0 || strlen(OCTO_API_KEY) < 8) return false;
  return true;
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
#if RGB_LED_ENABLE
  ledcWrite(CH_R, 255 - r);
  ledcWrite(CH_G, 255 - g);
  ledcWrite(CH_B, 255 - b);
#else
  (void)r; (void)g; (void)b;
#endif
}

void updateLed() {
  uint8_t L = RGB_LED_LEVEL;
  if (!liveMode) { setLed(0, 0, L); return; }
  if (strstr(statusMsg, "fail") || strstr(statusMsg, "no WiFi") || strstr(statusMsg, "set WiFi")) {
    setLed(L, 0, 0); return;
  }
  if (strstr(view.state, "Heat") || strstr(view.state, "heat")) { setLed(L, L / 2, 0); return; }
  if (strstr(view.state, "Print") || strstr(view.state, "print")) { setLed(0, L, 0); return; }
  setLed(0, 0, L / 2);
}

void fmtTime(int sec, char *out, size_t n) {
  if (sec < 0) { strncpy(out, "--:--", n); out[n - 1] = 0; return; }
  int h = sec / 3600;
  int m = (sec % 3600) / 60;
  int s = sec % 60;
  if (h > 0) snprintf(out, n, "%d:%02d:%02d", h, m, s);
  else snprintf(out, n, "%02d:%02d", m, s);
}

void fmtTemp(float v, char *out, size_t n) {
  if (v < 0) { strncpy(out, "--", n); out[n - 1] = 0; return; }
  snprintf(out, n, "%.0f", v);
}

void clearView() {
  strncpy(view.name, "-", sizeof(view.name));
  strncpy(view.state, "-", sizeof(view.state));
  view.progress = -1;
  view.timeLeft = -1;
  view.hotend = view.hotendT = view.bed = view.bedT = -1;
  view.haveTemp = false;
}

void tickDemo() {
  // 約 70 秒走完一件，方便 workshop 睇到進度郁
  uint32_t cycle = 70000;
  uint32_t t = millis() % (cycle * DEMO_N);
  demoFile = (t / cycle) % DEMO_N;
  float u = (t % cycle) / (float)cycle;          // 0–1
  strncpy(view.name, DEMO_FILES[demoFile], sizeof(view.name) - 1);
  view.name[sizeof(view.name) - 1] = 0;

  if (u < 0.12f) {
    strncpy(view.state, "Heating", sizeof(view.state));
    view.progress = u / 0.12f * 4.0f;
    float k = u / 0.12f;
    view.hotend = 25 + (205 - 25) * k;
    view.hotendT = 210;
    view.bed = 25 + (58 - 25) * k;
    view.bedT = 60;
    view.timeLeft = 18 * 60;
  } else if (u < 0.92f) {
    strncpy(view.state, "Printing", sizeof(view.state));
    float p = (u - 0.12f) / 0.80f;
    view.progress = 4.0f + p * 94.0f;
    view.hotend = 208 + 3.0f * sinf(millis() / 900.0f);
    view.hotendT = 210;
    view.bed = 59 + 1.2f * sinf(millis() / 1400.0f);
    view.bedT = 60;
    view.timeLeft = (int)((1.0f - p) * 16 * 60);
  } else {
    strncpy(view.state, "Finishing", sizeof(view.state));
    view.progress = 98 + (u - 0.92f) / 0.08f * 2.0f;
    if (view.progress > 100) view.progress = 100;
    view.hotend = 180;
    view.hotendT = 0;
    view.bed = 45;
    view.bedT = 0;
    view.timeLeft = 0;
  }
  view.haveTemp = true;
}

bool jsonNum(JsonVariant v, float &out) {
  if (v.isNull() || v.is<const char *>() || v.is<JsonObject>() || v.is<JsonArray>()) return false;
  out = v.as<float>();
  return true;
}

bool httpGetJson(const char *path, DynamicJsonDocument &doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = String(OCTO_HOST) + path;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("X-Api-Key", OCTO_API_KEY);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("GET %s -> %d\n", path, code);
    http.end();
    snprintf(statusMsg, sizeof(statusMsg), "HTTP %d", code);
    return false;
  }
  String body = http.getString();
  http.end();
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("JSON %s\n", err.c_str());
    strncpy(statusMsg, "bad JSON", sizeof(statusMsg));
    return false;
  }
  return true;
}

void pollLive() {
#if ENABLE_LIVE
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStartMs > WIFI_WAIT_MS) strncpy(statusMsg, "no WiFi", sizeof(statusMsg));
    else strncpy(statusMsg, "WiFi...", sizeof(statusMsg));
    return;
  }
  DynamicJsonDocument job(2048);
  if (!httpGetJson("/api/job", job)) return;

  const char *nm = job["job"]["file"]["name"] | "";
  if (!nm || !nm[0]) nm = "(no file)";
  strncpy(view.name, nm, sizeof(view.name) - 1);
  view.name[sizeof(view.name) - 1] = 0;

  JsonVariant st = job["state"];
  const char *stxt = "-";
  if (st.is<const char *>()) stxt = st.as<const char *>();
  else if (!st["text"].isNull()) stxt = st["text"] | "-";
  strncpy(view.state, stxt, sizeof(view.state) - 1);
  view.state[sizeof(view.state) - 1] = 0;

  float comp;
  if (jsonNum(job["progress"]["completion"], comp)) view.progress = comp;
  else view.progress = -1;
  float left;
  if (jsonNum(job["progress"]["printTimeLeft"], left)) view.timeLeft = (int)left;
  else view.timeLeft = -1;

  view.haveTemp = false;
  view.hotend = view.hotendT = view.bed = view.bedT = -1;
  DynamicJsonDocument prn(2048);
  // printer 端點失敗唔好蓋過 job 已經成功嘅狀態
  char saved[40];
  strncpy(saved, statusMsg, sizeof(saved));
  saved[sizeof(saved) - 1] = 0;
  bool gotPrinter = httpGetJson("/api/printer", prn);
  if (!gotPrinter) strncpy(statusMsg, saved, sizeof(statusMsg));
  if (gotPrinter) {
    float a;
    if (jsonNum(prn["temperature"]["tool0"]["actual"], a)) view.hotend = a;
    if (jsonNum(prn["temperature"]["tool0"]["target"], a)) view.hotendT = a;
    if (jsonNum(prn["temperature"]["bed"]["actual"], a)) view.bed = a;
    if (jsonNum(prn["temperature"]["bed"]["target"], a)) view.bedT = a;
    view.haveTemp = true;
  }
  strncpy(statusMsg, "LIVE", sizeof(statusMsg));
#else
  strncpy(statusMsg, "LIVE compiled off", sizeof(statusMsg));
#endif
}

void enterDemo() {
  liveMode = false;
  if (wifiBegun) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiBegun = false;
  }
  strncpy(statusMsg, "DEMO", sizeof(statusMsg));
  layoutDrawn = false;
  Serial.println("mode DEMO");
}

void enterLive() {
#if !ENABLE_LIVE
  strncpy(statusMsg, "set ENABLE_LIVE 1", sizeof(statusMsg));
  liveMode = false;
  return;
#else
  if (!credsLookReal()) {
    strncpy(statusMsg, "set WiFi + API key", sizeof(statusMsg));
    liveMode = false;
    Serial.println("LIVE refused: placeholders still set");
    return;
  }
  liveMode = true;
  clearView();
  strncpy(view.state, "connecting", sizeof(view.state));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiBegun = true;
  wifiStartMs = millis();
  lastPollMs = 0;
  strncpy(statusMsg, "WiFi...", sizeof(statusMsg));
  layoutDrawn = false;
  Serial.printf("LIVE: WiFi %s  host %s\n", WIFI_SSID, OCTO_HOST);
#endif
}

bool touchEdge() {
  bool down = false;
  if (ts.tirqTouched() || ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z > TOUCH_MIN_Z) down = true;
  }
  bool edge = down && !touchWasDown && (millis() - lastTouchMs > 400);
  touchWasDown = down;
  if (edge) lastTouchMs = millis();
  return edge;
}

void drawTempRow(int y, const char *label, float actual, float target) {
  tft.fillRect(8, y, 304, 22, C_BG);
  tft.setTextFont(2);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(10, y + 2);
  tft.print(label);
  char a[8], b[8];
  fmtTemp(actual, a, sizeof(a));
  fmtTemp(target, b, sizeof(b));
  tft.setTextColor(C_WHT, C_BG);
  tft.setCursor(92, y + 2);
  tft.printf("%s", a);
  tft.setTextColor(C_DIM, C_BG);
  tft.printf(" / %s C", b);
}

void drawAll(bool full) {
  if (full) tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 26, C_PANEL);
  tft.setTextFont(2);
  tft.setTextColor(C_YEL, C_PANEL);
  tft.setCursor(8, 5);
  tft.print("OCTOPRINT");
  tft.setTextColor(liveMode ? C_CYN : C_YEL, C_PANEL);
  tft.setCursor(200, 5);
  tft.print(liveMode ? "LIVE" : "DEMO");

  tft.setTextFont(4);
  tft.setTextColor(C_WHT, C_BG);
  tft.setCursor(10, 34);
  // 檔名可能好長，裁到約 16 個字
  char shown[18];
  strncpy(shown, view.name, 16);
  shown[16] = 0;
  if (strlen(view.name) > 16) strcat(shown, ".");
  tft.fillRect(8, 32, 304, 28, C_BG);
  tft.setCursor(10, 34);
  tft.print(shown);

  // 進度條
  int bx = 10, by = 70, bw = 230, bh = 18;
  tft.fillRoundRect(bx, by, bw, bh, 4, C_PANEL);
  float p = view.progress < 0 ? 0 : view.progress;
  if (p > 100) p = 100;
  int fw = (int)((bw - 4) * (p / 100.0f));
  if (fw > 0) tft.fillRoundRect(bx + 2, by + 2, fw, bh - 4, 3, C_GRN);
  tft.setTextFont(2);
  tft.setTextColor(C_WHT, C_BG);
  tft.fillRect(246, 68, 70, 22, C_BG);
  tft.setCursor(248, 70);
  if (view.progress < 0) tft.print("--%");
  else { tft.print((int)(view.progress + 0.5f)); tft.print("%"); }

  tft.setTextColor(C_YEL, C_BG);
  tft.fillRect(8, 94, 304, 20, C_BG);
  tft.setCursor(10, 96);
  tft.print(view.state);

  drawTempRow(120, "HOTEND", view.hotend, view.hotendT);
  drawTempRow(144, "BED", view.bed, view.bedT);

  char tm[16];
  fmtTime(view.timeLeft, tm, sizeof(tm));
  tft.fillRect(8, 170, 304, 26, C_BG);
  tft.setTextFont(2);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(10, 174);
  tft.print("TIME LEFT");
  tft.setTextFont(4);
  tft.setTextColor(C_WHT, C_BG);
  tft.setCursor(110, 168);
  tft.print(tm);

  tft.fillRoundRect(10, 206, 300, 28, 6, C_PANEL);
  tft.setTextFont(2);
  tft.setTextColor(C_YEL, C_PANEL);
  tft.setCursor(18, 213);
  tft.print("tap: ");
  tft.print(liveMode ? "back to DEMO" : "try LIVE");
  tft.setTextColor(C_DIM, C_PANEL);
  tft.setCursor(168, 213);
  tft.print(statusMsg);

  layoutDrawn = true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n13_OctoPrint_Monitor  (our rewrite, not OctoGlance)");

  C_BG = tft.color565(12, 14, 18);
  C_PANEL = tft.color565(32, 36, 46);
  C_YEL = tft.color565(255, 196, 0);
  C_GRN = tft.color565(40, 200, 90);
  C_RED = tft.color565(230, 70, 60);
  C_DIM = tft.color565(150, 156, 168);
  C_CYN = tft.color565(80, 210, 230);
  C_WHT = TFT_WHITE;

#if RGB_LED_ENABLE
  ledcSetup(CH_R, 5000, 8); ledcAttachPin(LED_R, CH_R);
  ledcSetup(CH_G, 5000, 8); ledcAttachPin(LED_G, CH_G);
  ledcSetup(CH_B, 5000, 8); ledcAttachPin(LED_B, CH_B);
  setLed(0, 0, 0);
#endif

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  ledcSetup(CH_BL, 5000, 8);
  ledcAttachPin(BL_PIN, CH_BL);
  ledcWrite(CH_BL, SCREEN_BRIGHT);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  clearView();
#if START_IN_DEMO
  enterDemo();
#else
  enterLive();
#endif
  tickDemo();
  drawAll(true);
}

void loop() {
  if (touchEdge()) {
    if (liveMode) enterDemo();
    else enterLive();
    if (!liveMode) tickDemo();
    drawAll(true);
  }

  uint32_t now = millis();
  if (!liveMode) {
    if (now - lastDrawMs > 250) {
      lastDrawMs = now;
      tickDemo();
      drawAll(false);
    }
  } else {
#if ENABLE_LIVE
    if (now - lastPollMs > POLL_MS) {
      lastPollMs = now;
      pollLive();
      drawAll(false);
    }
#endif
  }
  updateLed();
  delay(20);
}
