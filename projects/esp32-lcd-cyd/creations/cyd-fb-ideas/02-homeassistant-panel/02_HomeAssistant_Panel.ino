/*
  02_HomeAssistant智能家居控制板  (CYD ESP32-2432S028R)
  ------------------------------------------------------------
  靈感來自 CYD Facebook 群組 Alberto Zarza 嘅 Home Assistant 控制板：
  https://www.facebook.com/groups/710623908028293/posts/1099588685798478/
  原作用 ESPControl (https://jtenniswood.github.io/espcontrol/) + ESPHome，
  呢份係學造工作室「AI 補完」自己用 Arduino 重寫嘅簡化版，唔係原作者嘅 code。

  功能：
   - 4 粒觸控掣，撳一下就經 HA REST API 開/關燈（homeassistant.toggle）
   - 每 5 秒同步燈嘅狀態（喺手機 App 開咗燈，塊屏都會跟住變色）
   - 頂部顯示溫度感應器讀數

  設定全部喺下面「改呢度就得」嗰段（唔再需要 config.h）
  Library：TFT_eSPI（已設定 CYD）、XPT2046_Touchscreen、ArduinoJson 6
  Board：ESP32 Dev Module
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
// ====== 改呢度就得 ======

// 你屋企 WiFi（ESP32 只支援 2.4GHz）
#define WIFI_SSID     "YourWiFiName"
#define WIFI_PASSWORD "YourWiFiPassword"

// Home Assistant 地址（用 IP 最穩陣），同 Long-Lived Access Token
// Token 喺 HA 左下角你個名 -> Security -> Long-lived access tokens -> Create token
#define HA_URL   "http://192.168.1.100:8123"
#define HA_TOKEN "PASTE_YOUR_LONG_LIVED_TOKEN_HERE"

// 4 粒按鈕：顯示名（英文）+ entity_id（喺 HA「設定 > 裝置與服務 > 實體」搵）
// 燈、開關、風扇都得，因為我哋用 homeassistant.toggle
struct HAButton { const char *label; const char *entity; };
static const HAButton BUTTONS[4] = {
  { "Living",  "light.living_room" },
  { "Bedroom", "light.bedroom" },
  { "Desk",    "switch.desk_lamp" },
  { "Fan",     "fan.bedroom_fan" },
};

// 溫度感應器 entity_id（冇就留空 ""）
#define TEMP_SENSOR "sensor.living_room_temperature"

// 幾耐同 HA 對一次狀態（毫秒）
#define POLL_MS 5000
// ====== 設定完 ======

// CYD 觸控 (XPT2046) 用獨立 SPI
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;

// 按鈕位置（橫向 320x240，2x2）
struct Rect { int x, y, w, h; };
Rect btnRect[4] = {
  { 10,  60, 145, 80 }, { 165,  60, 145, 80 },
  { 10, 150, 145, 80 }, { 165, 150, 145, 80 },
};
int btnState[4] = { -1, -1, -1, -1 };   // -1 未知, 0 關, 1 開
String tempText = "--";

// ---------- HA REST API ----------
// 讀一個 entity 嘅 state，例如 "on" / "off" / "26.5"
bool haGetState(const char *entity, String &state, String &unit) {
  if (WiFi.status() != WL_CONNECTED || strlen(entity) == 0) return false;
  HTTPClient http;
  http.begin(String(HA_URL) + "/api/states/" + entity);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    StaticJsonDocument<64> filter;           // 只要 state 同單位，慳記憶體
    filter["state"] = true;
    filter["attributes"]["unit_of_measurement"] = true;
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
      state = doc["state"].as<String>();
      unit = doc["attributes"]["unit_of_measurement"] | "";
      ok = true;
    }
  } else {
    Serial.printf("GET %s -> %d\n", entity, code);
  }
  http.end();
  return ok;
}

// 叫 HA 切換一個 entity
bool haToggle(const char *entity) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin(String(HA_URL) + "/api/services/homeassistant/toggle");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<128> body;
  body["entity_id"] = entity;
  String json;
  serializeJson(body, json);
  int code = http.POST(json);
  http.end();
  Serial.printf("toggle %s -> %d\n", entity, code);
  return code == 200;
}

// ---------- 畫面 ----------
void drawHeader() {
  tft.fillRect(0, 0, 320, 50, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("My Home", 10, 25, 4);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_NAVY);
  tft.setTextPadding(150);
  tft.drawString(tempText, 310, 25, 4);
  tft.setTextPadding(0);
  // WiFi 小圓點
  tft.fillCircle(150, 25, 5, WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_RED);
}

void drawButton(int i) {
  Rect r = btnRect[i];
  uint16_t bg = btnState[i] == 1 ? TFT_ORANGE : (btnState[i] == 0 ? TFT_DARKGREY : 0x2104);
  uint16_t fg = btnState[i] == 1 ? TFT_BLACK : TFT_WHITE;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 12, bg);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 12, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(BUTTONS[i].label, r.x + r.w / 2, r.y + r.h / 2 - 10, 4);
  const char *s = btnState[i] == 1 ? "ON" : (btnState[i] == 0 ? "OFF" : "?");
  tft.drawString(s, r.x + r.w / 2, r.y + r.h / 2 + 18, 2);
}

void refreshStates() {
  for (int i = 0; i < 4; i++) {
    String st, unit;
    if (haGetState(BUTTONS[i].entity, st, unit)) {
      int ns = (st == "on") ? 1 : 0;
      if (ns != btnState[i]) { btnState[i] = ns; drawButton(i); }
    }
  }
  String st, unit;
  if (haGetState(TEMP_SENSOR, st, unit)) tempText = st + " " + unit;
  drawHeader();
}

// ---------- 主程式 ----------
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 160, 120, 4);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "WiFi failed");

  tft.fillScreen(TFT_BLACK);
  drawHeader();
  for (int i = 0; i < 4; i++) drawButton(i);
  refreshStates();
}

unsigned long lastPoll = 0, lastTouch = 0;

void loop() {
  // 觸控
  if (ts.tirqTouched() && ts.touched() && millis() - lastTouch > 400) {
    TS_Point p = ts.getPoint();
    // 原始值大約 200~3700，轉做螢幕座標（如果唔準，改呢 4 個數）
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 240, 3800, 0, 240);
    for (int i = 0; i < 4; i++) {
      Rect r = btnRect[i];
      if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
        lastTouch = millis();
        tft.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 10, TFT_CYAN);  // 撳嘅反應
        if (haToggle(BUTTONS[i].entity)) {
          btnState[i] = btnState[i] == 1 ? 0 : 1;   // 先估佢轉咗，之後 poll 會校正
        }
        drawButton(i);
        lastPoll = millis() - POLL_MS + 1000;      // 1 秒後再對一次真實狀態
      }
    }
  }

  // 定時同步
  if (millis() - lastPoll > POLL_MS) {
    lastPoll = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    refreshStates();
  }
}
