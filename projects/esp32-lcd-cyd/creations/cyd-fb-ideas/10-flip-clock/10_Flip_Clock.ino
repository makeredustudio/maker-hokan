/*
  10_翻頁時鐘 / Flip Clock  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 私人群組「CYD - Cheap Yellow Display ESP32」
  Stephanus Nieberg 嘅帖子（佢用 ChatGPT/Codex 整咗 chat robot、internet radio 同 flipclock）：
  https://www.facebook.com/groups/8104937882859357/posts/29340118458914658/
  原作者冇公開 code。呢份係學造工作室「AI 補完」自己重寫嘅翻頁時鐘，唔係原作者嘅程式。
  （未喺實機測試，只係 compile 通過）

  功能 / Features
   - 橫向 320x240：HH MM SS 三塊深色圓角「翻頁牌」，中間有分隔線
   - 數字一變就有翻頁動畫（上半塊向下摺），用 sprite 喺記憶體砌好先推上屏，唔會成個屏閃
   - NTP 對時（香港時間 HKT-8），下面一行英文日期 + 中文「星期X」（內置細點陣字，唔使字型檔）
   - 12 / 24 小時制（長撳屏幕切換）；輕撳屏幕 = 換顏色主題（會記住）
   - （可選）每個鐘頭喇叭 GPIO26「滴」一聲，預設關
   - （可選）用光敏電阻 LDR (GPIO34) 自動調暗背光（PWM，GPIO21），預設關
   - 冇 WiFi / 連唔到 → DEMO 模式：由 12:31:03 開始行（跟原帖相入面嘅時間），用 millis() 計

  Library：TFT_eSPI（Larry 嘅 libraries.zip，已設定 CYD）、XPT2046_Touchscreen
           WiFi / Preferences 係 ESP32 內置
  Board：ESP32 Dev Module（ESP32 core 2.0.17）。Partition：預設 Default 4MB 就得
*/

#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ======================= 改呢度就得 / USER SETTINGS =======================
const char *WIFI_SSID     = "YOUR_WIFI";            // 唔填 = DEMO 模式
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *TZ_INFO       = "HKT-8";                // 香港時間
const char *NTP_SERVER1   = "pool.ntp.org";
const char *NTP_SERVER2   = "time.google.com";
#define FORCE_DEMO          false   // true = 唔連 WiFi，一定用 DEMO 時間
#define WIFI_OFF_AFTER_SYNC true    // 對完時熄 WiFi（慳電、冇咁熱）
#define RESYNC_HOURS        6       // 每幾多個鐘再對一次時（0 = 唔再對）
#define RETRY_WIFI_MIN      10      // WiFi 失敗（DEMO 中）每幾多分鐘再試

#define USE_24H             true    // true = 24 小時制；false = 12 小時制（有 AM/PM）
#define HOUR_LEADING_ZERO   true    // true = 09；false = 「 9」
#define SHOW_CHINESE_DAY    true    // 日期後面顯示「星期X」
#define DEFAULT_THEME       0       // 開機主題（0 = Classic，見下面 THEMES）
#define REMEMBER_TOUCH      true    // 記住用手指揀嘅主題 / 12-24h（關機都記得）

#define FLIP_ANIM           true    // 翻頁動畫
#define FLIP_MS             360     // 一次翻頁幾耐（毫秒）
#define FLIP_FRAMES         10      // 動畫幾多格（多 = 順，但慢啲）

#define CHIME_ENABLE        false   // 每個鐘頭「滴」一聲（GPIO26 喇叭）
#define CHIME_FREQ          2000    // 音高 Hz
#define CHIME_VOLUME        20      // 細聲啲：PWM duty 0–127
#define CHIME_QUIET_FROM    23      // 呢段時間唔響（23:00 至 08:00）
#define CHIME_QUIET_TO      8

#define AUTO_DIM            false   // 用光敏電阻自動調背光
#define LDR_BRIGHT_RAW      50      // 好光時 analogRead(34) 大約幾多（睇 Serial Monitor 再改）
#define LDR_DARK_RAW        800     // 好黑時大約幾多（兩個數大細次序調轉都得）
#define DIM_MIN             15      // 最暗背光 0–255
#define DIM_MAX             255     // 最光背光
#define SCREEN_BRIGHT       200     // 唔用 AUTO_DIM 時嘅背光

#define LONG_PRESS_MS       1200    // 長撳幾耐 = 切換 12/24 小時
// 觸控校正（呢個 sketch 只係撳任何位置，唔準都冇問題；Serial Monitor 有 raw 值）
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// DEMO 開始時間（原帖相入面嘅 12 31 03，日期用原帖日子）
#define DEMO_YEAR 2026
#define DEMO_MON  9
#define DEMO_DAY  26
#define DEMO_H    12
#define DEMO_M    31
#define DEMO_S    3

// 顏色主題（0xRRGGBB）：名, 背景, 牌, 牌下半, 數字, 分隔線, 日期字
struct Theme { const char *name; uint32_t bg, tile, tile2, digit, line, text; };
Theme THEMES[] = {
  { "Classic",  0x000000, 0x2A2A2A, 0x222222, 0xF0F0F0, 0x000000, 0xB0B0B0 },
  { "Red Case", 0x7A0E12, 0x1C1C1C, 0x151515, 0xFFFFFF, 0x7A0E12, 0xFFD0D0 },
  { "Ocean",    0x001830, 0x0A3A6A, 0x08305A, 0xC8F0FF, 0x001830, 0x60C8FF },
  { "Amber",    0x000000, 0x2A1800, 0x201200, 0xFFB000, 0x000000, 0xFF8C00 },
  { "Mint",     0x061410, 0x123A2C, 0x0E3024, 0x80FFC8, 0x061410, 0x60E0A0 },
  { "Paper",    0xD8D0C0, 0xFAF6EE, 0xEEE8DC, 0x202020, 0xA89C88, 0x403830 },
};
// ============================== 設定完 =====================================

// ---- CYD pins ----
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21
#define SPK_PIN      26
#define LDR_PIN      34
#define LED_R        4
#define LED_G        16
#define LED_B        17
#define CH_BL        0      // LEDC channel（背光）
#define CH_SPK       4      // LEDC channel（喇叭，用另一個 timer）

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
Preferences prefs;

// ---- 版面 / layout (landscape 320x240) ----
const int W = 320, H = 240;
const int TILE_W = 102, TILE_H = 108;   // Font 8 數字闊 55px（墨水 45px），兩個要 102 闊先放得落
const int TILE_Y = 38;
const int TILE_X[3] = { 0, 109, 218 };  // 牌之間 7px 放「:」
const int DATE_Y = 170;
const int NUM_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);

// 24x24 1-bit 點陣，由 Noto Sans CJK HK Bold 產生（OFL 授權）：星 期 日 一 二 三 四 五 六
const uint8_t CJK_GLYPHS[9][72] PROGMEM = {
  { 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0E, 0x00, 0x70, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x07, 0x18, 0x00, 0x0F, 0x1C, 0x00, 0x0F, 0xFF, 0xF0, 0x1F, 0xFF, 0xF0, 0x3C, 0x1C, 0x00, 0x1F, 0xFF, 0xE0, 0x07, 0xFF, 0xE0, 0x00, 0x1C, 0x00, 0x00, 0x1C, 0x00, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 星
  { 0x00, 0x00, 0x00, 0x0C, 0x60, 0x00, 0x0C, 0x67, 0xFC, 0x0C, 0x67, 0xFC, 0x3F, 0xFF, 0x1C, 0x3F, 0xFF, 0x1C, 0x1C, 0xE7, 0x1C, 0x0C, 0x67, 0x1C, 0x0F, 0xE7, 0xFC, 0x0F, 0xE7, 0xFC, 0x0C, 0x67, 0x1C, 0x0F, 0xE7, 0x1C, 0x0F, 0xE7, 0x1C, 0x0C, 0x67, 0xFC, 0x0C, 0x67, 0xFC, 0x3F, 0xFF, 0x1C, 0x3F, 0xFF, 0x1C, 0x0C, 0xC6, 0x1C, 0x0E, 0xE6, 0x1C, 0x1C, 0xFE, 0x1C, 0x38, 0x7E, 0xFC, 0x38, 0x2C, 0x78, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00 },  // 期
  { 0x00, 0x00, 0x00, 0x07, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0E, 0x00, 0x70, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0E, 0x00, 0x70, 0x06, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 日
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 一
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 二
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x0F, 0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0xE0, 0x07, 0xFF, 0xE0, 0x07, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x1F, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 三
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0x18, 0xE7, 0x38, 0x18, 0xE7, 0x38, 0x18, 0xE7, 0x38, 0x18, 0xE7, 0x38, 0x18, 0xE7, 0x38, 0x18, 0xC7, 0x38, 0x19, 0xC7, 0x38, 0x1B, 0xC7, 0xF8, 0x1F, 0x83, 0xF8, 0x1B, 0x00, 0x38, 0x18, 0x00, 0x38, 0x18, 0x00, 0x38, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0x18, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 四
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0, 0x1F, 0xFF, 0xF8, 0x1F, 0xFF, 0xF8, 0x00, 0x70, 0x00, 0x00, 0x70, 0x00, 0x00, 0x70, 0x00, 0x00, 0x70, 0x00, 0x0F, 0xFF, 0xE0, 0x0F, 0xFF, 0xE0, 0x0F, 0xFF, 0xE0, 0x00, 0xE0, 0xE0, 0x00, 0xE0, 0xE0, 0x00, 0xE0, 0xE0, 0x01, 0xC0, 0xE0, 0x01, 0xC0, 0xE0, 0x01, 0xC0, 0xE0, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 五
  { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x38, 0x00, 0x00, 0x38, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x18, 0x00, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC3, 0x80, 0x03, 0xC3, 0x80, 0x03, 0x83, 0xC0, 0x07, 0x81, 0xE0, 0x07, 0x00, 0xE0, 0x0F, 0x00, 0xF0, 0x1E, 0x00, 0x78, 0x1E, 0x00, 0x78, 0x3C, 0x00, 0x3C, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 六
};


// ---- 狀態 / state ----
int themeIdx = DEFAULT_THEME;
bool use24h = USE_24H;
bool demoMode = true;
bool timeOk = false;
time_t demoBase = 0;
uint32_t demoStartMs = 0;
char shown[3][4] = { "", "", "" };     // 每塊牌而家顯示緊咩
int lastSec = -1, lastYday = -1, lastChimeHour = -1;
uint32_t lastSyncMs = 0, lastWifiTryMs = 0;
bool wifiTrying = false;
uint32_t wifiTryStart = 0;
float ldrAvg = -1;
int blNow = SCREEN_BRIGHT;

// 顏色（轉成 16-bit；同 8-bit sprite 一樣嘅色，唔會見到色差）
uint16_t cBg, cTile, cTile2, cDigit, cLine, cText;

TFT_eSprite sprA[3] = { TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft) };
TFT_eSprite sprB[3] = { TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft) };
TFT_eSprite frameSpr = TFT_eSprite(&tft);
TFT_eSprite *curS[3], *nxtS[3];
bool spritesOk = false;

uint16_t rgb(uint32_t c) {
  uint16_t c16 = tft.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
  return tft.color8to16(tft.color16to8(c16));   // 用 8-bit sprite 嘅色，前後一致
}

void applyTheme() {
  const Theme &t = THEMES[themeIdx];
  cBg = rgb(t.bg); cTile = rgb(t.tile); cTile2 = rgb(t.tile2);
  cDigit = rgb(t.digit); cLine = rgb(t.line); cText = rgb(t.text);
}

// =========================================================================
// 時間 / time
// =========================================================================
bool getNow(struct tm &t) {
  if (demoMode) {
    time_t now = demoBase + (millis() - demoStartMs) / 1000;
    localtime_r(&now, &t);
    return true;
  }
  return getLocalTime(&t, 0);
}

void startDemo() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
  struct tm d = {};
  d.tm_year = DEMO_YEAR - 1900; d.tm_mon = DEMO_MON - 1; d.tm_mday = DEMO_DAY;
  d.tm_hour = DEMO_H; d.tm_min = DEMO_M; d.tm_sec = DEMO_S; d.tm_isdst = -1;
  demoBase = mktime(&d);
  demoStartMs = millis();
  demoMode = true;
  Serial.println("DEMO mode: clock starts at 12:31:03 (millis based)");
}

bool wifiConfigured() {
  return !FORCE_DEMO && strcmp(WIFI_SSID, "YOUR_WIFI") != 0 && strlen(WIFI_SSID) > 0;
}

void wifiStart() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiTrying = true;
  wifiTryStart = lastWifiTryMs = millis();
  Serial.printf("WiFi: connecting to %s ...\n", WIFI_SSID);
}

void wifiStop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiTrying = false;
}

// 唔會卡住嘅 WiFi / NTP 流程，喺 loop() 入面行 / non-blocking WiFi + NTP
void wifiTask() {
  if (!wifiConfigured()) return;
  if (!wifiTrying) {
    bool due = false;
    if (!timeOk && millis() - lastWifiTryMs > (uint32_t)RETRY_WIFI_MIN * 60000UL) due = true;
    if (timeOk && RESYNC_HOURS > 0 && millis() - lastSyncMs > (uint32_t)RESYNC_HOURS * 3600000UL) due = true;
    if (due) wifiStart();
    return;
  }
  static bool ntpStarted = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpStarted) {
      ntpStarted = true;
      Serial.printf("WiFi OK, IP %s -> NTP\n", WiFi.localIP().toString().c_str());
      sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
      configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
    }
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && time(nullptr) > 1700000000) {   // 攞到真時間
      struct tm t;
      getLocalTime(&t, 0);
      Serial.println(&t, "NTP OK: %Y-%m-%d %H:%M:%S");
      timeOk = true;
      demoMode = false;
      lastSyncMs = millis();
      ntpStarted = false;
      lastYday = -1;                           // 重畫日期
      drawStatus();
      if (WIFI_OFF_AFTER_SYNC) wifiStop(); else wifiTrying = false;
      return;
    }
  }
  if (millis() - wifiTryStart > 25000) {       // 25 秒都唔得 → 放棄，遲啲再試
    Serial.println("WiFi/NTP failed, will retry later");
    ntpStarted = false;
    wifiStop();
    lastWifiTryMs = millis();
    if (!timeOk && !demoMode) startDemo();
    drawStatus();
  }
}

// =========================================================================
// 畫翻頁牌 / tile rendering
// =========================================================================
// g 可以係 sprite（ox=oy=0）或者直接個屏（sprite 開唔到時後備用）
void renderTile(TFT_eSPI &g, int ox, int oy, const char *txt, const char *tag) {
  const int mid = TILE_H / 2;
  g.fillRect(ox, oy, TILE_W, TILE_H, cBg);
  g.fillRoundRect(ox, oy, TILE_W, TILE_H, 10, cTile);
  g.fillRoundRect(ox, oy + mid, TILE_W, TILE_H - mid, 10, cTile2);   // 下半深少少
  g.fillRect(ox, oy + mid, TILE_W, 10, cTile2);
  g.setTextDatum(MC_DATUM);
  g.setTextColor(cDigit);                      // 冇底色 = 透明
  char c[2] = { 0, 0 };
  for (int i = 0; i < 2; i++) {
    c[0] = txt[i];
    if (c[0] == ' ' || c[0] == 0) continue;
    g.drawString(c, ox + (i == 0 ? 28 : TILE_W - 25), oy + mid - 1, 8);   // Font 8：墨水置中
  }
  // 中間分隔線 + 兩邊「鉸位」凹口 / centre split + hinge notches
  g.fillRect(ox, oy + mid - 1, TILE_W, 2, cLine);
  g.fillRect(ox, oy + mid - 5, 3, 10, cBg);
  g.fillRect(ox + TILE_W - 3, oy + mid - 5, 3, 10, cBg);
  if (tag && tag[0]) {                         // AM / PM
    g.setTextDatum(TL_DATUM);
    g.setTextColor(cDigit);
    g.drawString(tag, ox + 7, oy + 5, 1);
  }
}

// 砌一格動畫：old = 舊牌，neu = 新牌，p = 0..1
void composeFrame(TFT_eSprite *oldS, TFT_eSprite *neuS, float p) {
  const int mid = TILE_H / 2;
  uint8_t *o = (uint8_t *)oldS->getPointer();
  uint8_t *n = (uint8_t *)neuS->getPointer();
  uint8_t *d = (uint8_t *)frameSpr.getPointer();
  if (p < 0.5f) {
    // 第一段：舊牌上半向下摺（高度 cos 咁縮），後面露出新牌上半；下半仲係舊
    float s = cosf(p * PI);
    int fh = max(1, (int)roundf(mid * s));
    for (int y = 0; y < mid; y++) {
      const uint8_t *src;
      if (y < mid - fh) src = n + y * TILE_W;
      else src = o + (mid - (mid - y) * mid / fh) * TILE_W;
      memcpy(d + y * TILE_W, src, TILE_W);
    }
    memcpy(d + mid * TILE_W, o + mid * TILE_W, (TILE_H - mid) * TILE_W);
    frameSpr.drawFastHLine(4, mid - fh, TILE_W - 8, cLine);      // 摺邊陰影
  } else {
    // 第二段：新牌下半由中間向下打開，蓋住舊牌下半
    float s = -cosf(p * PI);
    int bh = TILE_H - mid;
    int fh = max(1, (int)roundf(bh * s));
    memcpy(d, n, mid * TILE_W);
    for (int y = mid; y < TILE_H; y++) {
      const uint8_t *src;
      if (y < mid + fh) src = n + (mid + (y - mid) * bh / fh) * TILE_W;
      else src = o + y * TILE_W;
      memcpy(d + y * TILE_W, src, TILE_W);
    }
    if (mid + fh < TILE_H) frameSpr.drawFastHLine(4, mid + fh, TILE_W - 8, cLine);
  }
}

void showTiles(const char txt[3][4], const char *tag, bool animate) {
  bool changed[3];
  bool any = false;
  for (int i = 0; i < 3; i++) {
    changed[i] = strcmp(txt[i], shown[i]) != 0;
    any |= changed[i];
  }
  if (!any) return;
  if (!spritesOk) {                             // 後備：直接畫落屏
    for (int i = 0; i < 3; i++)
      if (changed[i]) renderTile(tft, TILE_X[i], TILE_Y, txt[i], i == 0 ? tag : "");
  } else {
    for (int i = 0; i < 3; i++)
      if (changed[i]) renderTile(*nxtS[i], 0, 0, txt[i], i == 0 ? tag : "");
    if (animate && FLIP_ANIM) {
      uint32_t t0 = millis();
      for (int f = 1; f < FLIP_FRAMES; f++) {
        float p = (float)f / FLIP_FRAMES;
        for (int i = 0; i < 3; i++) {
          if (!changed[i]) continue;
          composeFrame(curS[i], nxtS[i], p);
          frameSpr.pushSprite(TILE_X[i], TILE_Y);
        }
        uint32_t target = t0 + (uint32_t)FLIP_MS * f / FLIP_FRAMES;
        while ((int32_t)(millis() - target) < 0) delay(1);
      }
    }
    for (int i = 0; i < 3; i++) {
      if (!changed[i]) continue;
      nxtS[i]->pushSprite(TILE_X[i], TILE_Y);
      TFT_eSprite *tmp = curS[i]; curS[i] = nxtS[i]; nxtS[i] = tmp;   // 新變舊
    }
  }
  for (int i = 0; i < 3; i++) strcpy(shown[i], txt[i]);
}

// =========================================================================
// 其他畫面 / other drawing
// =========================================================================
void drawBadge(int x, const char *s, uint16_t bg) {
  int w = tft.textWidth(s, 2) + 10;
  tft.fillRoundRect(x, 6, w, 20, 5, bg);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_BLACK, bg);
  tft.drawString(s, x + 5, 16, 2);
}

void drawStatus() {
  tft.fillRect(0, 0, W, 32, cBg);
  if (demoMode) drawBadge(4, wifiConfigured() ? "DEMO (no WiFi)" : "DEMO", TFT_ORANGE);
  else drawBadge(4, "NTP  HKT", TFT_GREEN);
  if (wifiTrying) drawBadge(demoMode ? (wifiConfigured() ? 116 : 60) : 84, "WiFi...", TFT_YELLOW);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(cText, cBg);
  char s[32];
  snprintf(s, sizeof(s), "%s%s  %s", use24h ? "24h" : "12h", CHIME_ENABLE ? "  CHIME" : "",
           THEMES[themeIdx].name);
  tft.drawString(s, W - 6, 16, 2);
}

void drawColons() {
  for (int i = 0; i < 2; i++) {
    int x = TILE_X[i] + TILE_W + 3;
    tft.fillRect(x - 3, TILE_Y, 7, TILE_H, cBg);
    tft.fillCircle(x, TILE_Y + TILE_H / 2 - 18, 3, cText);
    tft.fillCircle(x, TILE_Y + TILE_H / 2 + 18, 3, cText);
  }
}

void drawDate(const struct tm &t) {
  static const char *EN_DAY[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char *EN_MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  char s[32];
  snprintf(s, sizeof(s), "%s %02d %s %04d", EN_DAY[t.tm_wday], t.tm_mday, EN_MON[t.tm_mon], t.tm_year + 1900);
  tft.fillRect(0, DATE_Y - 4, W, 34, cBg);
  int tw = tft.textWidth(s, 4);
  int cw = SHOW_CHINESE_DAY ? (12 + 3 * 24) : 0;
  int x = (W - tw - cw) / 2;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(cText, cBg);
  tft.drawString(s, x, DATE_Y, 4);
  if (SHOW_CHINESE_DAY) {
    int gx = x + tw + 12;
    // 星 期 + 日/一/二/三/四/五/六（glyph 2..8）
    tft.drawBitmap(gx, DATE_Y, CJK_GLYPHS[0], 24, 24, cText, cBg);
    tft.drawBitmap(gx + 24, DATE_Y, CJK_GLYPHS[1], 24, 24, cText, cBg);
    tft.drawBitmap(gx + 48, DATE_Y, CJK_GLYPHS[2 + t.tm_wday], 24, 24, cText, cBg);
  }
}

void drawHint() {
  tft.fillRect(0, 214, W, 26, cBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cText, cBg);
  tft.drawString("tap = theme    hold = 12/24h", W / 2, 226, 2);
}

void buildTileText(const struct tm &t, char txt[3][4], char *tag) {
  int h = t.tm_hour;
  tag[0] = 0;
  if (!use24h) {
    strcpy(tag, h < 12 ? "AM" : "PM");
    h %= 12;
    if (h == 0) h = 12;
  }
  if (HOUR_LEADING_ZERO || h >= 10) snprintf(txt[0], 4, "%02d", h);
  else snprintf(txt[0], 4, " %d", h);
  snprintf(txt[1], 4, "%02d", t.tm_min);
  snprintf(txt[2], 4, "%02d", t.tm_sec);
}

void redrawAll() {
  tft.fillScreen(cBg);
  for (int i = 0; i < 3; i++) shown[i][0] = 0;  // 迫重畫 / force redraw
  drawStatus();
  drawColons();
  drawHint();
  struct tm t;
  if (getNow(t)) {
    char txt[3][4], tag[4];
    buildTileText(t, txt, tag);
    showTiles(txt, tag, false);
    drawDate(t);
    lastYday = t.tm_yday;
  }
}

// =========================================================================
// 喇叭、背光 / chime + backlight
// =========================================================================
void beep(int freq, int ms) {
  ledcSetup(CH_SPK, freq, 8);
  ledcAttachPin(SPK_PIN, CH_SPK);
  ledcWrite(CH_SPK, CHIME_VOLUME);
  delay(ms);
  ledcWrite(CH_SPK, 0);
}

void checkChime(const struct tm &t) {
  if (!CHIME_ENABLE) return;
  if (t.tm_min != 0 || t.tm_hour == lastChimeHour) return;
  lastChimeHour = t.tm_hour;
  bool quiet = (CHIME_QUIET_FROM > CHIME_QUIET_TO)
                 ? (t.tm_hour >= CHIME_QUIET_FROM || t.tm_hour < CHIME_QUIET_TO)
                 : (t.tm_hour >= CHIME_QUIET_FROM && t.tm_hour < CHIME_QUIET_TO);
  if (quiet) return;
  Serial.printf("Chime %02d:00\n", t.tm_hour);
  beep(CHIME_FREQ, 40);                        // 「滴」
  delay(90);
  beep(CHIME_FREQ * 3 / 4, 40);                // 「嗒」
}

void autoDim() {
  if (!AUTO_DIM) return;
  int raw = analogRead(LDR_PIN);
  ldrAvg = (ldrAvg < 0) ? raw : ldrAvg * 0.85f + raw * 0.15f;      // 平滑
  float f = (ldrAvg - LDR_BRIGHT_RAW) / (float)(LDR_DARK_RAW - LDR_BRIGHT_RAW);   // 0 = 光, 1 = 黑
  f = constrain(f, 0.0f, 1.0f);
  int target = DIM_MAX - (int)(f * (DIM_MAX - DIM_MIN));
  if (target > blNow) blNow += max(1, (target - blNow) / 4);       // 慢慢變，唔會跳
  else if (target < blNow) blNow -= max(1, (blNow - target) / 4);
  ledcWrite(CH_BL, blNow);
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    Serial.printf("LDR raw=%d avg=%.0f -> backlight %d\n", raw, ldrAvg, blNow);
  }
}

// =========================================================================
// 觸控 / touch：輕撳 = 換主題，長撳 = 12/24h
// =========================================================================
void savePrefs() {
  if (!REMEMBER_TOUCH) return;
  prefs.putUChar("theme", themeIdx);
  prefs.putBool("h24", use24h);
}

void handleTouch() {
  static bool wasDown = false, longDone = false;
  static uint32_t downMs = 0;
  static uint8_t upCount = 0;
  bool down = ts.touched();
  if (down) {
    upCount = 0;
    if (!wasDown) {
      wasDown = true; longDone = false; downMs = millis();
      TS_Point p = ts.getPoint();
      int x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, W), 0, W - 1);
      int y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, H), 0, H - 1);
      Serial.printf("touch raw x=%d y=%d z=%d -> %d,%d\n", p.x, p.y, p.z, x, y);
    } else if (!longDone && millis() - downMs > LONG_PRESS_MS) {
      longDone = true;
      use24h = !use24h;
      Serial.printf("Hour mode -> %s\n", use24h ? "24h" : "12h");
      savePrefs();
      redrawAll();
    }
  } else if (wasDown && ++upCount >= 3) {      // 放手（去抖）
    wasDown = false; upCount = 0;
    if (!longDone) {
      themeIdx = (themeIdx + 1) % NUM_THEMES;
      Serial.printf("Theme -> %s\n", THEMES[themeIdx].name);
      applyTheme();
      savePrefs();
      redrawAll();
    }
  }
}

// =========================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n10 Flip Clock starting...");
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);  // RGB LED 熄
  pinMode(SPK_PIN, OUTPUT); digitalWrite(SPK_PIN, LOW);
  analogSetPinAttenuation(LDR_PIN, ADC_0db);   // LDR 訊號好細，用 0dB 靈敏啲

  prefs.begin("flipclk", false);
  if (REMEMBER_TOUCH) {
    themeIdx = prefs.getUChar("theme", DEFAULT_THEME);
    use24h = prefs.getBool("h24", USE_24H);
  }
  if (themeIdx >= NUM_THEMES) themeIdx = 0;
  applyTheme();

  tft.init();
  tft.setRotation(1);                          // 橫向 320x240
  tft.fillScreen(cBg);
  ledcSetup(CH_BL, 5000, 8);                   // 背光 PWM
  ledcAttachPin(BL_PIN, CH_BL);
  blNow = AUTO_DIM ? DIM_MAX : SCREEN_BRIGHT;
  ledcWrite(CH_BL, blNow);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 8-bit sprite（每塊 102x108 = 11KB；7 塊大約 77KB）
  spritesOk = true;
  for (int i = 0; i < 3; i++) {
    sprA[i].setColorDepth(8); sprB[i].setColorDepth(8);
    spritesOk &= sprA[i].createSprite(TILE_W, TILE_H) != nullptr;
    spritesOk &= sprB[i].createSprite(TILE_W, TILE_H) != nullptr;
    curS[i] = &sprA[i]; nxtS[i] = &sprB[i];
  }
  frameSpr.setColorDepth(8);
  spritesOk &= frameSpr.createSprite(TILE_W, TILE_H) != nullptr;
  Serial.printf("Sprites %s, free heap %u\n", spritesOk ? "OK" : "FAILED (no animation)", ESP.getFreeHeap());

  startDemo();                                 // 未對到時之前先用 DEMO 時間
  if (wifiConfigured()) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(cText, cBg);
    tft.drawString("Connecting WiFi...", W / 2, H / 2 - 10, 4);
    tft.drawString(WIFI_SSID, W / 2, H / 2 + 20, 2);
    wifiStart();
    while (wifiTrying && millis() - wifiTryStart < 25000) { wifiTask(); delay(100); }
  } else {
    Serial.println("WiFi not set -> DEMO");
  }
  redrawAll();
}

void loop() {
  handleTouch();
  wifiTask();

  struct tm t;
  if (getNow(t) && t.tm_sec != lastSec) {
    lastSec = t.tm_sec;
    char txt[3][4], tag[4];
    buildTileText(t, txt, tag);
    showTiles(txt, tag, true);
    if (t.tm_yday != lastYday) { lastYday = t.tm_yday; drawDate(t); }
    checkChime(t);
  }

  static uint32_t lastDim = 0;
  if (millis() - lastDim > 200) { lastDim = millis(); autoDim(); }
  delay(5);
}
