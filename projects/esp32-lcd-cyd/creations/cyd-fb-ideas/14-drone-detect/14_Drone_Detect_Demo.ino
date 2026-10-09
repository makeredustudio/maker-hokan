/*
  14_無人機偵測示範 / Drone Detect DEMO  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 私人群組「CYD - Cheap Yellow Display ESP32」
  Wik Pol 嘅 Bruce Drone Detection Suite（2026-10-01）：
  https://www.facebook.com/groups/8104937882859357/posts/29129914223268417/
  原作者 repo：https://github.com/wikxxl/Drone_detector
  呢份係學造工作室「AI 補完」自己畫嘅「示範畫面」，唔係原作者嘅程式，
  冇 copy Bruce firmware，亦冇任何射頻 / WiFi 掃描程式。

  重要 / IMPORTANT
   - 呢塊板咁樣行，偵測唔到真嘅無人機。
   - 雷達同「警報」全部係寫死嘅假資料，用嚟俾 workshop 睇個畫面。
   - 真係要偵測，要額外射頻硬件同 Bruce firmware，唔係呢份 sketch。
   - 未喺實機測試，只係 compile 通過。

  功能
   - 橫向 320x240：掃描線轉圈、三個假目標、撳屏幕暫停 / 繼續
   - 畫面永遠寫住 DEMO / NOT A DETECTOR
   - RGB LED：掃描中藍色，掃過假目標閃一下紅

  Library：TFT_eSPI、XPT2046_Touchscreen
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
*/

#include <SPI.h>
#include <math.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ======================= 改呢度就得 / USER SETTINGS =======================
#define SCREEN_BRIGHT    200
#define RGB_LED_ENABLE   1
#define RGB_LED_LEVEL    36
#define SWEEP_MS         4000     // 掃一圈幾耐
#define TOUCH_MIN_Z      300
// 呢度冇 WiFi。留兩個 placeholder 只係同其他教材格式一致，程式唔會連線。
const char *WIFI_SSID     = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
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
TFT_eSprite radar(&tft);

const int W = 320, H = 240;
const int RAD_X = 8, RAD_Y = 46, RAD_S = 156;     // radar sprite
const int RCX = RAD_S / 2, RCY = RAD_S / 2;
const int RR = 70;

uint16_t C_BG, C_PANEL, C_YEL, C_GRN, C_RED, C_DIM, C_CYN;

struct Toy {
  float ang;          // radians
  float dist;         // 0–1 of radius
  const char *name;
  int fakeRssi;       // 寫死嘅假數字，唔係量到嘅
};

// 全部係教材用假目標，唔對應任何真機
Toy toys[] = {
  { 0.7f,  0.55f, "toy-blip A", -42 },
  { 2.4f,  0.78f, "toy-blip B", -67 },
  { 4.3f,  0.38f, "toy-blip C", -51 },
};
const int TOY_N = 3;

bool paused = false;
bool spriteOk = false;
bool touchWasDown = false;
uint32_t lastTouchMs = 0;
float sweep = 0;
int flashToy = -1;
uint32_t flashUntil = 0;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
#if RGB_LED_ENABLE
  ledcWrite(CH_R, 255 - r);
  ledcWrite(CH_G, 255 - g);
  ledcWrite(CH_B, 255 - b);
#else
  (void)r; (void)g; (void)b;
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

float angDiff(float a, float b) {
  float d = fabsf(a - b);
  if (d > PI) d = 2 * PI - d;
  return d;
}

void drawChrome() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, 22, C_PANEL);
  tft.setTextFont(2);
  tft.setTextColor(C_YEL, C_PANEL);
  tft.setCursor(6, 3);
  tft.print("DRONE DEMO");
  tft.setTextColor(C_RED, C_PANEL);
  tft.setCursor(168, 3);
  tft.print("NOT A DETECTOR");

  tft.fillRect(0, 22, W, 20, tft.color565(80, 20, 20));
  tft.setTextColor(C_WHT(), C_BG);
  tft.setTextColor(TFT_WHITE, tft.color565(80, 20, 20));
  tft.setCursor(6, 25);
  tft.print("fake scan  -  no radio hardware");

  tft.setTextFont(2);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(172, 50);
  tft.print("SAMPLE LIST");
  tft.drawFastHLine(172, 66, 140, C_PANEL);

  tft.fillRoundRect(172, 200, 140, 32, 6, C_PANEL);
  tft.setTextColor(C_YEL, C_PANEL);
  tft.setCursor(184, 210);
  tft.print(paused ? "tap: resume" : "tap: pause");
}

uint16_t C_WHT() { return TFT_WHITE; }

void drawList(int hot) {
  for (int i = 0; i < TOY_N; i++) {
    int y = 74 + i * 38;
    bool on = (i == hot);
    tft.fillRoundRect(170, y, 144, 34, 4, on ? tft.color565(70, 24, 24) : C_PANEL);
    tft.setTextFont(2);
    tft.setTextColor(on ? C_RED : C_YEL, on ? tft.color565(70, 24, 24) : C_PANEL);
    tft.setCursor(176, y + 2);
    tft.print("FAKE");
    tft.setTextColor(TFT_WHITE, on ? tft.color565(70, 24, 24) : C_PANEL);
    tft.setCursor(214, y + 2);
    tft.print(toys[i].name);
    tft.setTextColor(C_DIM, on ? tft.color565(70, 24, 24) : C_PANEL);
    tft.setCursor(176, y + 16);
    tft.printf("made-up %d", toys[i].fakeRssi);
  }
}

void drawRadar(float ang, int hot) {
  if (!spriteOk) return;
  radar.fillSprite(C_BG);
  uint16_t ring = radar.color565(40, 70, 50);
  radar.drawCircle(RCX, RCY, RR, ring);
  radar.drawCircle(RCX, RCY, RR * 2 / 3, ring);
  radar.drawCircle(RCX, RCY, RR / 3, ring);
  radar.drawLine(RCX - RR, RCY, RCX + RR, RCY, ring);
  radar.drawLine(RCX, RCY - RR, RCX, RCY + RR, ring);

  // 掃過嘅扇形尾（幾條淡線）
  for (int k = 1; k <= 6; k++) {
    float a = ang - k * 0.08f;
    int x2 = RCX + (int)(cosf(a) * RR);
    int y2 = RCY + (int)(sinf(a) * RR);
    uint16_t c = radar.color565(20, 40 + k * 18, 30);
    radar.drawLine(RCX, RCY, x2, y2, c);
  }
  int sx = RCX + (int)(cosf(ang) * RR);
  int sy = RCY + (int)(sinf(ang) * RR);
  radar.drawLine(RCX, RCY, sx, sy, C_GRN);

  for (int i = 0; i < TOY_N; i++) {
    int bx = RCX + (int)(cosf(toys[i].ang) * RR * toys[i].dist);
    int by = RCY + (int)(sinf(toys[i].ang) * RR * toys[i].dist);
    uint16_t col = (i == hot) ? C_RED : C_YEL;
    radar.fillCircle(bx, by, (i == hot) ? 5 : 3, col);
  }
  radar.fillCircle(RCX, RCY, 2, C_CYN);
  radar.setTextFont(1);
  radar.setTextColor(C_DIM, C_BG);
  radar.setCursor(4, RAD_S - 10);
  radar.print(paused ? "PAUSED" : "scanning");
  radar.pushSprite(RAD_X, RAD_Y);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n14_Drone_Detect_Demo");
  Serial.println("DEMO ONLY. This sketch does not detect drones.");
  (void)WIFI_SSID;
  (void)WIFI_PASSWORD;

  C_BG = tft.color565(8, 12, 10);
  C_PANEL = tft.color565(24, 32, 28);
  C_YEL = tft.color565(255, 196, 0);
  C_GRN = tft.color565(40, 220, 90);
  C_RED = tft.color565(240, 70, 60);
  C_DIM = tft.color565(140, 160, 145);
  C_CYN = tft.color565(80, 210, 230);

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

  radar.setColorDepth(8);
  spriteOk = radar.createSprite(RAD_S, RAD_S) != nullptr;
  Serial.printf("radar sprite %s\n", spriteOk ? "OK" : "FAILED");

  drawChrome();
  drawList(-1);
  drawRadar(0, -1);
}

void loop() {
  if (touchEdge()) {
    paused = !paused;
    // 只重畫個掣，唔洗成個屏
    tft.fillRoundRect(172, 200, 140, 32, 6, C_PANEL);
    tft.setTextFont(2);
    tft.setTextColor(C_YEL, C_PANEL);
    tft.setCursor(184, 210);
    tft.print(paused ? "tap: resume" : "tap: pause ");
    Serial.println(paused ? "paused" : "scanning");
  }

  if (!paused) {
    sweep = fmodf(millis() / (float)SWEEP_MS * 2.0f * PI, 2.0f * PI);
  }

  int hot = -1;
  for (int i = 0; i < TOY_N; i++) {
    if (angDiff(sweep, toys[i].ang) < 0.22f) { hot = i; break; }
  }
  if (hot >= 0 && hot != flashToy) {
    flashToy = hot;
    flashUntil = millis() + 280;
  }
  if (millis() > flashUntil) {
    // 掃走之後唔再標紅，除非而家仲掃到
    if (hot < 0) flashToy = -1;
  }
  int show = (hot >= 0) ? hot : ((millis() < flashUntil) ? flashToy : -1);

  static int lastShow = -2;
  if (show != lastShow) {
    drawList(show);
    lastShow = show;
  }
  drawRadar(sweep, show);

  uint8_t L = RGB_LED_LEVEL;
  if (paused) setLed(L / 3, L / 3, 0);
  else if (show >= 0) setLed(L, 0, 0);
  else setLed(0, 0, L);

  delay(30);
}
