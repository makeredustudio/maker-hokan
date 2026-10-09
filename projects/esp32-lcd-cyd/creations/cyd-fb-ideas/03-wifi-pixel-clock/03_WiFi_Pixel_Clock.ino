/*
  03_WiFi像素時鐘  (CYD ESP32-2432S028R)
  ------------------------------------------------------------
  靈感來自 CYD Facebook 群組 Rohit Singh 嘅 Smart WiFi Pixel LED Clock（原作用 ESP8266 + LED）：
  https://www.facebook.com/groups/710623908028293/posts/1100310049059675/
  影片：https://youtube.com/shorts/vdNNB9bn1NY
  呢份係學造工作室「AI 補完」喺 CYD 上面自己重寫嘅版本，唔係原作者嘅 code。

  功能：
   - WiFi + NTP 自動對時（香港時間 UTC+8）
   - 時間用「LED 點陣」大字顯示，暗咗嘅 LED 都見到，好有懷舊味
   - 撳一下屏幕 = 轉顏色主題
   - 用 CYD 背面嘅光敏電阻 (LDR, GPIO34) 自動調光

  Library：TFT_eSPI（已設定 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module
*/

#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ====== 改呢度 ======
const char *WIFI_SSID     = "YourWiFiName";
const char *WIFI_PASSWORD = "YourWiFiPassword";
#define TZ_INFO "HKT-8"          // 香港時區；其他地方可以改，例如 "JST-9"
#define USE_24H  true            // false = 12 小時制
#define AUTO_BRIGHTNESS true     // false = 固定最光

// LDR：CYD 上光越猛讀數越細（大約 0 = 好光，數百至過千 = 暗）
// 打開 Serial Monitor 睇 "LDR=" 數值，再按你環境改呢兩個數
#define LDR_BRIGHT  0
#define LDR_DARK    400

// ====== 硬件腳位 ======
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define LDR_PIN      34
#define BL_PIN       21

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;

// 5x7 點陣字型：每個數字 7 行，每行用低 5 bit
const uint8_t DIGITS[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
  {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
};

// 顏色主題：亮 LED、暗 LED、背景、文字
struct Theme { uint16_t on, off, bg, text; const char *name; };
const Theme THEMES[] = {
  { TFT_RED,     0x2000, TFT_BLACK, TFT_ORANGE, "Retro Red" },
  { TFT_GREEN,   0x0120, TFT_BLACK, TFT_GREENYELLOW, "Matrix" },
  { TFT_CYAN,    0x0104, TFT_BLACK, TFT_SKYBLUE, "Ice" },
  { TFT_YELLOW,  0x2100, TFT_BLACK, TFT_GOLD, "Amber" },
  { TFT_MAGENTA, 0x2004, 0x0004,    TFT_PINK, "Neon" },
  { TFT_WHITE,   0x18E3, TFT_BLACK, TFT_LIGHTGREY, "Mono" },
};
const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
int themeIdx = 0;

// 點陣排版：HH:MM 一共 25 列 x 7 行，每粒 LED 間距 12px
const int PITCH = 12, DOT_R = 5;
const int ORIGIN_X = (320 - 25 * PITCH) / 2 + PITCH / 2;
const int ORIGIN_Y = 40;
int shown[4] = { -1, -1, -1, -1 };   // 而家畫緊嘅 4 個數字，有變先重畫
bool colonShown = false;

void drawDot(int col, int row, bool on) {
  const Theme &t = THEMES[themeIdx];
  tft.fillCircle(ORIGIN_X + col * PITCH, ORIGIN_Y + row * PITCH, DOT_R, on ? t.on : t.off);
}

void drawDigit(int pos, int d) {
  static const int startCol[4] = { 0, 6, 14, 20 };   // 第 12、13 列係冒號
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 5; c++)
      drawDot(startCol[pos] + c, r, DIGITS[d][r] & (0x10 >> c));
}

void drawColon(bool on) {
  drawDot(12, 2, on);
  drawDot(12, 4, on);
}

// 秒數：底部一排 60 粒小 LED
void drawSeconds(int sec) {
  const Theme &t = THEMES[themeIdx];
  for (int i = 0; i < 60; i++) {
    int x = 10 + i * 5, y = 150;
    tft.fillRect(x, y, 4, 6, i <= sec ? t.on : t.off);
  }
}

void drawDate(struct tm &ti) {
  const Theme &t = THEMES[themeIdx];
  static const char *WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %s", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, WD[ti.tm_wday]);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(t.text, t.bg);
  tft.setTextPadding(300);
  tft.drawString(buf, 160, 185, 4);
  if (!USE_24H) tft.drawString(ti.tm_hour < 12 ? "AM" : "PM", 160, 215, 2);
  else          tft.drawString(t.name, 160, 215, 2);
  tft.setTextPadding(0);
}

void redrawAll() {
  tft.fillScreen(THEMES[themeIdx].bg);
  for (int i = 0; i < 4; i++) shown[i] = -1;
}

// 自動調光：讀 LDR，平滑咗先改背光 PWM
void updateBrightness() {
  static float smooth = 255;
  int raw = analogRead(LDR_PIN);
  int target = AUTO_BRIGHTNESS ? map(constrain(raw, LDR_BRIGHT, LDR_DARK), LDR_BRIGHT, LDR_DARK, 255, 20) : 255;
  smooth = smooth * 0.9 + target * 0.1;
  ledcWrite(0, (int)smooth);
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 2000) { lastLog = millis(); Serial.printf("LDR=%d  BL=%d\n", raw, (int)smooth); }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  ledcSetup(0, 5000, 8);         // 背光 PWM：通道 0、5kHz、8 bit
  ledcAttachPin(BL_PIN, 0);
  ledcWrite(0, 255);
  analogSetPinAttenuation(LDR_PIN, ADC_0db);   // LDR 訊號好細，用最靈敏檔

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 160, 120, 4);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "stdtime.gov.hk");
  struct tm ti;
  tft.drawString("Syncing time...   ", 160, 120, 4);
  while (!getLocalTime(&ti, 1000)) {}
  redrawAll();
}

int lastSec = -1;
unsigned long lastTouch = 0;

void loop() {
  // 撳屏幕轉主題
  if (ts.tirqTouched() && ts.touched() && millis() - lastTouch > 500) {
    lastTouch = millis();
    themeIdx = (themeIdx + 1) % THEME_COUNT;
    redrawAll();
    lastSec = -1;
  }

  struct tm ti;
  if (getLocalTime(&ti, 10) && ti.tm_sec != lastSec) {
    lastSec = ti.tm_sec;
    int h = ti.tm_hour;
    if (!USE_24H) { h %= 12; if (h == 0) h = 12; }
    int d[4] = { h / 10, h % 10, ti.tm_min / 10, ti.tm_min % 10 };
    for (int i = 0; i < 4; i++)
      if (d[i] != shown[i]) { drawDigit(i, d[i]); shown[i] = d[i]; }
    colonShown = (ti.tm_sec % 2 == 0);   // 冒號一秒閃一次
    drawColon(colonShown);
    drawSeconds(ti.tm_sec);
    drawDate(ti);
  }
  updateBrightness();
  delay(20);
}
