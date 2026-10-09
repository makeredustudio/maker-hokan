/*
  08_溫度時鐘 + SD 卡記錄 / Temperature Clock + SD Logger  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 私人群組「CYD - Cheap Yellow Display ESP32」
  Melvin Lee 嘅溫度時鐘（LM75 + microSD CSV 記錄）：
  https://www.facebook.com/groups/8104937882859357/posts/29375515502041620/
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅 code。
  （未喺實機測試，只係 compile 通過）

  功能 / Features
   - 直向 240x320：日期、大時鐘（NTP，香港時間）、大溫度數字、MAX / MIN
   - 半圓錶盤（範圍可改，預設 10–40°C）：藍→綠→黃→紅色帶 + 指針 + 最高/最低記號
   - LM75 溫度感應器：直接用 Wire 讀（唔使裝 library），地址 0x48–0x4F，自動搜尋
   - 搜唔到感應器 → 自動用「模擬溫度」(SIM)，冇零件都可以試
   - 每 N 分鐘寫一行 CSV 落 microSD：date,time,tempC
   - 頂部狀態：WiFi / SD / 感應器
   - 輕撳一下 = 資料頁（SD、記錄數、I2C 地址…）；長撳 1.5 秒 = 重設 MAX/MIN
   - 背面 RGB LED 跟溫度變色（可關）

  接線 / Wiring（CN1 插座：GND, IO22, IO27, 3.3V）
     LM75 VCC → 3.3V   GND → GND   SDA → IO27   SCL → IO22

  SPI 分配（重要）：TFT 用 HSPI（12/13/14），SD 卡用 VSPI（18/19/23, CS5）。
  因為 ESP32 只得兩條可用 SPI，觸控 (25/32/39/33) 喺呢個 sketch 改用
  「bit-bang」自己讀 XPT2046，所以唔使 XPT2046_Touchscreen library，亦唔會同 SD 打交。

  Library：TFT_eSPI（Larry 嘅 libraries.zip，已設定 CYD）；SD / Wire / WiFi 係 ESP32 內置
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
*/

#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <TFT_eSPI.h>

// ======================= 改呢度就得 / USER SETTINGS =======================
const char *WIFI_SSID     = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *TZ_INFO       = "HKT-8";            // 香港時間
const char *NTP_SERVER1   = "pool.ntp.org";
const char *NTP_SERVER2   = "time.google.com";
#define WIFI_OFF_AFTER_SYNC  true    // 對完時就熄 WiFi（慳電、少啲熱力影響讀數）
#define RESYNC_HOURS         24      // 每幾多個鐘再對一次時（0 = 唔再對）

// LM75 溫度感應器
#define I2C_SDA        27            // CN1 IO27
#define I2C_SCL        22            // CN1 IO22
#define LM75_ADDR      0x48          // 0x48–0x4F（睇模組 A0/A1/A2 焊位）；搜唔到會自動 scan
#define TEMP_OFFSET    0.0f          // 校正：讀數 + 呢個數
#define READ_EVERY_MS  2000          // 幾耐讀一次溫度
#define FORCE_DEMO     false         // true = 唔理感應器，一定用模擬溫度

// 錶盤 / gauge
#define GAUGE_MIN      10            // 錶盤最低（°C）
#define GAUGE_MAX      40            // 錶盤最高（°C）
#define GAUGE_STEP     5             // 刻度數字每格幾多度
#define COOL_BELOW     20.0f         // 低過呢個 = 藍/青色
#define HOT_ABOVE      30.0f         // 高過呢個 = 紅色（中間綠→黃→橙）

// SD 卡記錄 / logging
#define LOG_EVERY_MIN  20            // 每幾多分鐘記錄一次（例如 1, 5, 10, 20, 60）
#define LOG_FILE       "/templog.csv"
#define DEMO_LOG_FILE  "/templog_demo.csv"   // 模擬溫度另外記，唔會撈亂真數據

// RGB LED（背面，active-low）
#define RGB_LED_ENABLE true
#define RGB_LED_LEVEL  40            // 光度 0–255（好光好刺眼，40 夠）

#define SCREEN_BRIGHT  200           // 背光 0–255
#define SCREEN_ROTATION 0            // 直向：0；如果上下顛倒咗就改做 2
#define LOG_AT_BOOT    true          // 開機即刻記一行（方便測試 SD）
#define LONG_PRESS_MS  1500          // 長撳幾耐 = 重設 MAX/MIN
// ============================== 設定完 =====================================

// ---- CYD pins ----
#define BL_PIN   21
#define LED_R    4
#define LED_G    16
#define LED_B    17
#define SD_CS    5
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define T_CS     33
#define T_CLK    25
#define T_MOSI   32
#define T_MISO   39
#define T_IRQ    36

TFT_eSPI tft;
SPIClass sdSPI(VSPI);

// ---- 狀態 / state ----
bool wifiOk = false, timeOk = false, sdOk = false, sensorOk = false;
uint8_t sensorAddr = 0;
float tempNow = NAN, tempMax = -1000, tempMin = 1000;
uint32_t logCount = 0;
char lastLog[40] = "-";
int lastLoggedSlot = -1;          // 防止同一分鐘記兩次
uint32_t lastLogMs = 0;
uint32_t lastSyncMs = 0;
bool infoPage = false;
float needleShown = NAN;          // 錶盤指針而家指住嘅溫度

// ---- 版面 / layout (portrait 240x320) ----
const int W = 240, H = 320;
const int GX = 120, GY = 296;     // 錶盤圓心
const int GR = 100, GIR = 86;     // 外/內半徑（色帶）
const int NEEDLE_LEN = 52;

// =========================================================================
// 觸控：bit-bang XPT2046（避開 SD 用緊嘅 VSPI）
// =========================================================================
uint16_t xptRead(uint8_t cmd) {
  digitalWrite(T_CLK, LOW);
  for (int i = 7; i >= 0; i--) {             // 送 8-bit 指令
    digitalWrite(T_MOSI, (cmd >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(T_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(T_CLK, LOW);
  }
  digitalWrite(T_MOSI, LOW);
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {             // 讀 16-bit，有效 12-bit
    digitalWrite(T_CLK, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | digitalRead(T_MISO);
    digitalWrite(T_CLK, LOW);
    delayMicroseconds(1);
  }
  return (v >> 3) & 0x0FFF;                 // 第 1 bit 係 BUSY，之後 12-bit 數據
}

// 回傳壓力值（0 = 冇撳）；同時俾 raw x/y 你校正用
int touchPressure(uint16_t *rx = nullptr, uint16_t *ry = nullptr) {
  if (digitalRead(T_IRQ) == HIGH) return 0;  // IRQ LOW = 有人撳
  digitalWrite(T_CS, LOW);
  int z1 = xptRead(0xB1);
  int z2 = xptRead(0xC1);
  int z = z1 + 4095 - z2;
  if (rx) *rx = xptRead(0xD1);
  if (ry) *ry = xptRead(0x91);
  xptRead(0x90);                             // 返回省電模式 + IRQ
  digitalWrite(T_CS, HIGH);
  return (z > 400) ? z : 0;
}

// =========================================================================
// LM75（raw Wire）
// =========================================================================
bool i2cPresent(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

bool findSensor() {
  if (FORCE_DEMO) return false;
  if (i2cPresent(LM75_ADDR)) { sensorAddr = LM75_ADDR; return true; }
  Serial.printf("LM75 not at 0x%02X, scanning 0x48-0x4F...\n", LM75_ADDR);
  for (uint8_t a = 0x48; a <= 0x4F; a++) {
    if (i2cPresent(a)) { sensorAddr = a; Serial.printf("Found device at 0x%02X\n", a); return true; }
  }
  // 順便 scan 晒成條 bus，方便 debug
  Serial.print("I2C scan:");
  for (uint8_t a = 1; a < 127; a++) if (i2cPresent(a)) Serial.printf(" 0x%02X", a);
  Serial.println(" (done)");
  return false;
}

bool readLM75(float &t) {
  Wire.beginTransmission(sensorAddr);
  Wire.write(0x00);                          // 溫度寄存器
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)sensorAddr, 2) != 2) return false;
  int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
  t = (raw >> 5) * 0.125f;                   // LM75A 11-bit；舊 LM75 9-bit 都啱
  return t > -55 && t < 125;
}

float simTemp() {                            // 模擬：慢慢上落 + 少少雜訊
  float m = millis() / 1000.0f;
  return 26.0f + 6.0f * sinf(m / 90.0f) + 0.3f * sinf(m / 7.0f);
}

// =========================================================================
// 顏色 / colours
// =========================================================================
uint16_t lerp565(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, float f) {
  f = constrain(f, 0.0f, 1.0f);
  return tft.color565(r1 + (r2 - r1) * f, g1 + (g2 - g1) * f, b1 + (b2 - b1) * f);
}

// 藍(凍) → 青 → 綠(舒服) → 黃 → 橙 → 紅(熱)
uint16_t tempColor(float t) {
  float mid = (COOL_BELOW + HOT_ABOVE) / 2;
  if (t <= COOL_BELOW - 4) return tft.color565(40, 90, 255);
  if (t <= COOL_BELOW)     return lerp565(40, 90, 255, 0, 220, 230, (t - (COOL_BELOW - 4)) / 4);
  if (t <= mid)            return lerp565(0, 220, 230, 60, 230, 60, (t - COOL_BELOW) / (mid - COOL_BELOW));
  if (t <= HOT_ABOVE)      return lerp565(60, 230, 60, 255, 200, 0, (t - mid) / (HOT_ABOVE - mid));
  if (t <= HOT_ABOVE + 4)  return lerp565(255, 200, 0, 255, 40, 20, (t - HOT_ABOVE) / 4);
  return tft.color565(255, 40, 20);
}

void setRgbLed(float t) {
  if (!RGB_LED_ENABLE || isnan(t)) return;
  uint8_t r = 0, g = 0, b = 0;
  if (t < COOL_BELOW) b = RGB_LED_LEVEL;
  else if (t > HOT_ABOVE) r = RGB_LED_LEVEL;
  else g = RGB_LED_LEVEL;
  ledcWrite(1, 255 - r);                     // active-low
  ledcWrite(2, 255 - g);
  ledcWrite(3, 255 - b);
}

// =========================================================================
// 錶盤 / gauge  (180° 半圓：左 = GAUGE_MIN，右 = GAUGE_MAX)
// TFT_eSPI drawArc 角度：0° = 正下，順時針；左邊 = 90°，頂 = 180°，右邊 = 270°
// =========================================================================
float tempToRad(float t) {                   // 數學角度（0 = 右，逆時針）
  float f = (constrain(t, GAUGE_MIN, GAUGE_MAX) - GAUGE_MIN) / (float)(GAUGE_MAX - GAUGE_MIN);
  return PI - f * PI;                        // 左 (π) → 右 (0)
}

void drawGaugeFace() {
  tft.fillRect(0, GY - GR - 16, W, GR + 16 + 2, TFT_BLACK);
  // 色帶：每度一段 / one segment per degree
  int span = GAUGE_MAX - GAUGE_MIN;
  for (int i = 0; i < span; i++) {
    float t = GAUGE_MIN + i + 0.5f;
    uint32_t a0 = 90 + (uint32_t)(i * 180 / span);
    uint32_t a1 = 90 + (uint32_t)((i + 1) * 180 / span);
    tft.drawArc(GX, GY, GR, GIR, a0, a1, tempColor(t), TFT_BLACK, false);
  }
  // 刻度同數字 / ticks + labels
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  for (int t = GAUGE_MIN; t <= GAUGE_MAX; t++) {
    float a = tempToRad(t);
    bool major = ((t - GAUGE_MIN) % GAUGE_STEP) == 0;
    int r1 = GIR - 2, r2 = major ? GIR - 10 : GIR - 5;
    tft.drawLine(GX + r1 * cosf(a), GY - r1 * sinf(a), GX + r2 * cosf(a), GY - r2 * sinf(a),
                 major ? TFT_WHITE : TFT_DARKGREY);
    if (major) {
      int rl = GIR - 20;
      tft.drawNumber(t, GX + rl * cosf(a), GY - rl * sinf(a));
    }
  }
  tft.drawFastHLine(GX - GR, GY + 1, 2 * GR + 1, TFT_DARKGREY);
  needleShown = NAN;
}

// 喺色帶外面畫一粒小三角做 MAX/MIN 記號
void drawMarker(float t, uint16_t col) {
  float a = tempToRad(t);
  int r = GR + 3;
  float px = GX + r * cosf(a), py = GY - r * sinf(a);
  float qx = GX + (r + 9) * cosf(a + 0.05f), qy = GY - (r + 9) * sinf(a + 0.05f);
  float sx = GX + (r + 9) * cosf(a - 0.05f), sy = GY - (r + 9) * sinf(a - 0.05f);
  tft.fillTriangle(px, py, qx, qy, sx, sy, col);
}

void drawMarkers() {
  // 清走外圈舊記號 / clear old markers ring
  tft.drawArc(GX, GY, GR + 14, GR + 2, 88, 272, TFT_BLACK, TFT_BLACK, false);
  if (tempMax > -999) drawMarker(tempMax, TFT_RED);
  if (tempMin < 999)  drawMarker(tempMin, TFT_CYAN);
}

void drawNeedleAt(float t, uint16_t col) {
  float a = tempToRad(t);
  float tx = GX + NEEDLE_LEN * cosf(a), ty = GY - NEEDLE_LEN * sinf(a);
  float bx = GX + 6 * cosf(a + PI / 2), by = GY - 6 * sinf(a + PI / 2);
  float cx = GX + 6 * cosf(a - PI / 2), cy = GY - 6 * sinf(a - PI / 2);
  tft.fillTriangle(tx, ty, bx, by, cx, cy, col);
}

void updateNeedle(float t) {
  if (isnan(t)) return;
  if (!isnan(needleShown)) {
    if (fabsf(needleShown - t) < 0.05f) return;
    drawNeedleAt(needleShown, TFT_BLACK);    // 抹走舊指針
    // 指針可能擦咗數字，重畫數字 / redraw labels that the old needle may have crossed
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    for (int v = GAUGE_MIN; v <= GAUGE_MAX; v += GAUGE_STEP) {
      float a = tempToRad(v);
      int rl = GIR - 20;
      tft.drawNumber(v, GX + rl * cosf(a), GY - rl * sinf(a));
    }
  }
  tft.drawFastHLine(GX - GR, GY + 1, 2 * GR + 1, TFT_DARKGREY);
  drawNeedleAt(t, TFT_WHITE);
  tft.fillCircle(GX, GY, 7, tempColor(t));
  tft.drawCircle(GX, GY, 7, TFT_WHITE);
  needleShown = t;
}

// =========================================================================
// 文字區 / text areas
// =========================================================================
void drawBadge(int x, const char *txt, uint16_t bg) {
  int w = tft.textWidth(txt, 1) + 8;
  tft.fillRoundRect(x, 2, w, 13, 3, bg);
  tft.setTextColor(TFT_BLACK, bg);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(txt, x + 4, 5, 1);
}

void drawStatus() {
  tft.fillRect(0, 0, W, 17, TFT_BLACK);
  bool radioOn = (WiFi.getMode() != WIFI_OFF);
  // WiFi：綠 = 連緊，灰 = 熄咗（已對時），紅 = 失敗
  drawBadge(2, "WiFi", wifiOk && radioOn ? TFT_GREEN : (timeOk ? TFT_DARKGREY : TFT_RED));
  drawBadge(40, "NTP", timeOk ? TFT_GREEN : TFT_RED);
  drawBadge(72, sdOk ? "SD" : "NO SD", sdOk ? TFT_GREEN : TFT_RED);
  char s[16];
  if (sensorOk) snprintf(s, sizeof(s), "LM75 0x%02X", sensorAddr);
  else strcpy(s, "SIM TEMP");
  drawBadge(sdOk ? 96 : 116, s, sensorOk ? TFT_GREEN : TFT_ORANGE);
  // 記錄計數 / log count
  char c[16];
  snprintf(c, sizeof(c), "LOG %lu", (unsigned long)logCount);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(c, W - 2, 5, 1);
}

void drawClock() {
  struct tm ti;
  char d[32], t[16];
  if (timeOk && getLocalTime(&ti, 10)) {
    strftime(d, sizeof(d), "%a %d %b %Y", &ti);
    strftime(t, sizeof(t), "%H:%M:%S", &ti);
  } else {
    unsigned long s = millis() / 1000;
    strcpy(d, "No time (WiFi?)");
    snprintf(t, sizeof(t), "%02lu:%02lu:%02lu", (s / 3600) % 100, (s / 60) % 60, s % 60);
  }
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(W);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  tft.drawString(d, W / 2, 22, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(t, W / 2, 50, 6);           // 7-seg 式大字（Font 6）
  tft.setTextPadding(0);
}

void drawTemp() {
  uint16_t col = isnan(tempNow) ? TFT_DARKGREY : tempColor(tempNow);
  char b[12];
  if (isnan(tempNow)) strcpy(b, "--.-");
  else snprintf(b, sizeof(b), "%.1f", tempNow);
  tft.fillRect(0, 104, W, 54, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(col, TFT_BLACK);
  tft.drawString(b, 168, 106, 7);            // 7-segment 大數字（48px 高）
  tft.drawCircle(178, 112, 4, col);          // 「°」
  tft.drawCircle(178, 112, 3, col);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("C", 186, 108, 4);
  if (!sensorOk) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("SIM", 188, 138, 2);
  }
  // MAX / MIN
  char mm[40];
  tft.fillRect(0, 160, W, 18, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  if (tempMax > -999) {
    snprintf(mm, sizeof(mm), "MAX %.1f", tempMax);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(mm, 62, 160, 2);
    snprintf(mm, sizeof(mm), "MIN %.1f", tempMin);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(mm, 178, 160, 2);
  }
}

void drawFooter() {
  tft.fillRect(0, GY + 4, W, H - GY - 4, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  char f[48];
  if (sdOk) snprintf(f, sizeof(f), "SD log every %d min", LOG_EVERY_MIN);
  else strcpy(f, "No SD card - not logging");
  tft.drawString(f, W / 2, GY + 8, 1);
}

void drawMainPage() {
  tft.fillScreen(TFT_BLACK);
  drawStatus();
  drawClock();
  drawTemp();
  drawGaugeFace();
  drawMarkers();
  updateNeedle(tempNow);
  drawFooter();
}

void drawInfoPage() {
  tft.fillScreen(TFT_BLACK);
  drawStatus();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("INFO  (tap = back)", 8, 26, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char l[64];
  int y = 62;
  auto line = [&](const char *s) { tft.drawString(s, 8, y, 2); y += 20; };
  snprintf(l, sizeof(l), "WiFi: %s", wifiOk ? WiFi.localIP().toString().c_str() : (timeOk ? "off (time synced)" : "not connected"));
  line(l);
  snprintf(l, sizeof(l), "Sensor: %s", sensorOk ? "LM75" : "none -> SIM");
  line(l);
  if (sensorOk) { snprintf(l, sizeof(l), "I2C addr: 0x%02X  SDA %d SCL %d", sensorAddr, I2C_SDA, I2C_SCL); line(l); }
  if (sdOk) {
    snprintf(l, sizeof(l), "SD: %llu MB, used %llu MB", SD.cardSize() >> 20, SD.usedBytes() >> 20);
    line(l);
    snprintf(l, sizeof(l), "File: %s", sensorOk ? LOG_FILE : DEMO_LOG_FILE);
    line(l);
  } else line("SD: not found (FAT32, <=32GB?)");
  snprintf(l, sizeof(l), "Log every %d min, count %lu", LOG_EVERY_MIN, (unsigned long)logCount);
  line(l);
  line("Last log:");
  line(lastLog);
  snprintf(l, sizeof(l), "Gauge %d-%d C, offset %.1f", GAUGE_MIN, GAUGE_MAX, TEMP_OFFSET);
  line(l);
  y += 6;
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  line("Long press (1.5s) = reset MAX/MIN");
}

// =========================================================================
// WiFi / NTP
// =========================================================================
void syncTime() {
  if (strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) { Serial.println("WiFi not set"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  wifiOk = (WiFi.status() == WL_CONNECTED);
  if (wifiOk) {
    configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
    struct tm ti;
    if (getLocalTime(&ti, 8000)) {
      timeOk = true;
      Serial.println(&ti, "NTP OK: %Y-%m-%d %H:%M:%S");
    }
  }
  lastSyncMs = millis();
  if (WIFI_OFF_AFTER_SYNC && timeOk) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOk = false;
  }
}

// =========================================================================
// SD 記錄 / logging
// =========================================================================
void initSD() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, sdSPI, 4000000);
  if (!sdOk) { Serial.println("SD init failed (FAT32? inserted?)"); return; }
  Serial.printf("SD OK, %llu MB\n", SD.cardSize() >> 20);
}

void writeLog(float t) {
  if (!sdOk || isnan(t)) return;
  const char *fn = sensorOk ? LOG_FILE : DEMO_LOG_FILE;
  bool isNew = !SD.exists(fn);
  File f = SD.open(fn, FILE_APPEND);
  if (!f) { Serial.println("SD write failed"); sdOk = false; drawStatus(); return; }
  if (isNew) f.println("date,time,tempC");
  struct tm ti;
  char d[16], tm[16];
  if (timeOk && getLocalTime(&ti, 10)) {
    strftime(d, sizeof(d), "%Y-%m-%d", &ti);
    strftime(tm, sizeof(tm), "%H:%M:%S", &ti);
  } else {                                   // 冇時間：日期 0000-00-00，時間用開機計時
    unsigned long s = millis() / 1000;
    strcpy(d, "0000-00-00");
    snprintf(tm, sizeof(tm), "%02lu:%02lu:%02lu", (s / 3600) % 100, (s / 60) % 60, s % 60);
  }
  snprintf(lastLog, sizeof(lastLog), "%s,%s,%.1f", d, tm, t);
  f.println(lastLog);
  f.close();
  logCount++;
  Serial.printf("LOG -> %s : %s\n", fn, lastLog);
}

// 對準時鐘：例如每 20 分鐘 = :00 :20 :40 記；冇時間就按開機計
void checkLog() {
  if (isnan(tempNow)) return;
  struct tm ti;
  if (timeOk && getLocalTime(&ti, 10)) {
    int minuteOfDay = ti.tm_hour * 60 + ti.tm_min;
    if (minuteOfDay % LOG_EVERY_MIN == 0 && minuteOfDay != lastLoggedSlot) {
      lastLoggedSlot = minuteOfDay;
      writeLog(tempNow);
      if (!infoPage) drawStatus();
    }
  } else if (lastLogMs == 0 || millis() - lastLogMs >= (uint32_t)LOG_EVERY_MIN * 60000UL) {
    lastLogMs = millis() | 1;
    writeLog(tempNow);
    if (!infoPage) drawStatus();
  }
}

// =========================================================================
void readTemperature() {
  float t;
  if (sensorOk && readLM75(t)) {
    t += TEMP_OFFSET;
  } else {
    if (sensorOk) { Serial.println("LM75 read failed"); }
    t = sensorOk ? NAN : simTemp();
  }
  if (isnan(t)) return;
  tempNow = roundf(t * 10) / 10.0f;
  bool mmChanged = false;
  if (tempNow > tempMax) { tempMax = tempNow; mmChanged = true; }
  if (tempNow < tempMin) { tempMin = tempNow; mmChanged = true; }
  setRgbLed(tempNow);
  if (!infoPage) {
    drawTemp();
    if (mmChanged) drawMarkers();
    updateNeedle(tempNow);
  }
}

void handleTouch() {
  uint16_t rx, ry;
  if (!touchPressure(&rx, &ry)) return;
  uint32_t t0 = millis();
  Serial.printf("touch raw x=%u y=%u\n", rx, ry);
  bool longDone = false;
  while (touchPressure()) {
    if (!longDone && millis() - t0 > LONG_PRESS_MS) {
      longDone = true;                       // 長撳：重設 MAX/MIN
      tempMax = tempMin = tempNow;
      Serial.println("MAX/MIN reset");
      tft.fillRect(0, 160, W, 18, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("MAX / MIN RESET", W / 2, 160, 2);
    }
    delay(20);
  }
  if (longDone) {
    delay(500);
    if (infoPage) drawInfoPage();
    else { drawTemp(); drawMarkers(); }
  } else {                                   // 輕撳：切換資料頁
    infoPage = !infoPage;
    if (infoPage) drawInfoPage(); else drawMainPage();
  }
}

void setup() {
  Serial.begin(115200);
  // 觸控 bit-bang pins
  pinMode(T_CS, OUTPUT);   digitalWrite(T_CS, HIGH);
  pinMode(T_CLK, OUTPUT);  digitalWrite(T_CLK, LOW);
  pinMode(T_MOSI, OUTPUT); digitalWrite(T_MOSI, LOW);
  pinMode(T_MISO, INPUT);
  pinMode(T_IRQ, INPUT);
  // RGB LED（PWM，active-low）
  if (RGB_LED_ENABLE) {
    ledcSetup(1, 5000, 8); ledcAttachPin(LED_R, 1); ledcWrite(1, 255);
    ledcSetup(2, 5000, 8); ledcAttachPin(LED_G, 2); ledcWrite(2, 255);
    ledcSetup(3, 5000, 8); ledcAttachPin(LED_B, 3); ledcWrite(3, 255);
  } else {
    pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
    digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
  }

  tft.init();
  tft.setRotation(SCREEN_ROTATION);          // 直向 240x320
  tft.fillScreen(TFT_BLACK);
  pinMode(BL_PIN, OUTPUT);
  analogWrite(BL_PIN, SCREEN_BRIGHT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Temp Logger", W / 2, 120, 4);
  tft.drawString("WiFi / NTP...", W / 2, 160, 2);

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  sensorOk = findSensor();
  Serial.println(sensorOk ? "LM75 OK" : "No LM75 -> simulated temperature");

  initSD();
  syncTime();
  readTemperature();
  if (LOG_AT_BOOT) writeLog(tempNow);
  drawMainPage();
}

void loop() {
  handleTouch();

  static uint32_t lastSec = 0;
  if (millis() - lastSec >= 1000) {
    lastSec = millis();
    if (!infoPage) drawClock();
    checkLog();
  }

  static uint32_t lastRead = 0;
  if (millis() - lastRead >= READ_EVERY_MS) {
    lastRead = millis();
    readTemperature();
  }

  // 定時再對時 / periodic NTP resync
  if (RESYNC_HOURS > 0 && millis() - lastSyncMs >= (uint32_t)RESYNC_HOURS * 3600000UL) {
    syncTime();
    if (!infoPage) drawStatus();
  }
  delay(10);
}
