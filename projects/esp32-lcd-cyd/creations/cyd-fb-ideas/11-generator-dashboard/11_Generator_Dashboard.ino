/*
  11_發電機儀表板 / Generator Dashboard  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 公開群組「CYD - Cheap Yellow Display - ESP32」
  Christopher Althouse 嘅 Genmon CYD Dashboard：
  https://www.facebook.com/groups/710623908028293/posts/1099396695817677/
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅 code。
  （未喺實機測試，只係 compile 通過；LIVE 模式未對真 Genmon 測試過）

  功能 / Features
   - 兩頁觸控儀表板（撳屏幕任何位置轉頁）
       第 1 頁：6 個指針錶 —— 電池電壓、市電電壓、輸出電壓、頻率、轉速、功率
       第 2 頁：狀態頁 —— 引擎狀態、開關模式、電池、輸出（電壓 / 電流 / 功率 / 頻率）、
               保養同停電資料（運行時數、燃油、下次保養、停電幾耐）
   - DEMO 模式（預設）：模擬一部後備發電機：
       市電正常 → 停電 → 引擎啟動 → 運行（轉電俾屋企）→ 市電恢復 → 冷卻 → 停機 → 循環
       預設香港數值 220V / 50Hz / 3000rpm（下面可以改）
   - LIVE 模式：經 WiFi 讀 Genmon 伺服器嘅 JSON（/cmd/gui_status_json），
       JSON key 同網址都喺下面可以改（亦可以改去 Home Assistant 等其他 JSON 來源）
   - 背面 RGB LED：運行中 = 綠、停電而未發動 = 紅閃、警報 = 紅、其他 = 熄
   - 可選：光敏電阻 (LDR) 自動調光
   - 用 sprite / 局部重畫，唔會成個屏閃

  Library：TFT_eSPI（Larry 嘅 libraries.zip，已設定 CYD）、XPT2046_Touchscreen、
           ArduinoJson 6（LIVE 模式用；DEMO 模式都要裝住先 compile 到）
  Board：ESP32 Dev Module（ESP32 core 2.0.17）。Partition：預設 Default 4MB 就得
*/

#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ======================= 改呢度就得 / USER SETTINGS =======================
#define DEMO_MODE        true     // true = 模擬發電機（唔使 WiFi）；false = 讀 Genmon (LIVE)

// ---- 發電機額定數值 / generator nominal values（香港：220V 50Hz）----
#define NOMINAL_V        220.0f   // 輸出 / 市電額定電壓（美國 120 / 240）
#define NOMINAL_HZ       50.0f    // 頻率（美國 60）
#define NOMINAL_RPM      3000.0f  // 2 極發電機：50Hz = 3000rpm，60Hz = 3600rpm
#define RATED_KW         8.0f     // 發電機額定功率 kW（功率錶滿格）
#define BATTERY_NOMINAL  12.0f    // 起動電池 12V
#define UTILITY_LOST_V   (NOMINAL_V * 0.6f)   // 市電低過呢個 = 當停電
#define FUEL_UNIT        "L"      // 燃油單位（Genmon 美國用 gal）
#define FUEL_TANK        200.0f   // 油缸容量（DEMO 用）
#define SERVICE_EVERY_H  100.0f   // 每幾多運行小時保養一次（DEMO 用）

// ---- DEMO 模擬 ----
#define DEMO_SPEED       1.0f     // 模擬速度（2.0 = 快一倍）
#define DEMO_STANDBY_S   20       // 市電正常幾多秒先「停電」
#define DEMO_RUN_S       40       // 停電期間發電機帶負載幾多秒
#define DEMO_COOLDOWN_S  10       // 市電恢復後冷卻幾多秒先停機

// ---- LIVE 模式：WiFi + Genmon ----
const char *WIFI_SSID     = "YOUR_WIFI_SSID";       // 改做你嘅 WiFi 名
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";   // 改做你嘅 WiFi 密碼（唔好 share 出去）
const char *GENMON_URL    = "http://192.168.1.50:8000/cmd/gui_status_json";   // Genmon 部 Pi 嘅 IP
#define POLL_MS          5000     // 幾耐讀一次（毫秒）
#define HTTP_TIMEOUT_MS  3000

// JSON key（Genmon gui_status_json 嘅 key；用其他系統就改呢度）
// 數值可以係數字或者 "13.5 V" 呢類字串，程式會自動攞前面嘅數字
const char *KEY_BATTERY = "BatteryVoltage";
const char *KEY_UTILITY = "UtilityVoltage";
const char *KEY_OUTPUT  = "OutputVoltage";
const char *KEY_FREQ    = "Frequency";
const char *KEY_RPM     = "RPM";
const char *KEY_KW      = "kwOutput";
const char *KEY_ENGINE  = "enginestate";    // 例如 "Running" / "Stopped"
const char *KEY_SWITCH  = "switchstate";    // 例如 "Auto" / "Off" / "Manual"
const char *KEY_BASE    = "basestatus";     // 例如 "READY" / "RUNNING" / "ALARM"

// ---- 屏幕 / LED / LDR ----
#define SCREEN_BRIGHT    200      // 背光 0–255
#define AUTO_DIM         false    // true = 用 LDR 自動調光（Serial 會印 LDR raw 值俾你校）
#define LDR_RAW_BRIGHT   0        // 光猛時 LDR raw 值（唔同板唔同，睇 Serial Monitor）
#define LDR_RAW_DARK     800      // 黑暗時 LDR raw 值
#define DIM_MIN_BRIGHT   25       // 最暗背光
#define RGB_LED_ENABLE   true
#define RGB_LED_LEVEL    40       // LED 光度 0–255

// 觸控校正（撳唔準就睇 Serial Monitor 嘅 raw 值再改）
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800
#define TOUCH_MIN_Z 300
// ============================== 設定完 =====================================

// ---- CYD pins ----
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21
#define LED_R        4
#define LED_G        16
#define LED_B        17
#define LDR_PIN      34
#define CH_BL        0
#define CH_R         1
#define CH_G         2
#define CH_B         3

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite tile = TFT_eSprite(&tft);
bool tileOk = false;

const int W = 320, H = 240;
const int HDR_H = 22;
const int TW = 104, TH = 107;          // 錶格大細
const int TILE_X[3] = { 2, 108, 214 };
const int TILE_Y[2] = { 24, 133 };

// 顏色 / colours
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
const uint16_t C_BG    = RGB565(6, 10, 24);
const uint16_t C_TILE  = RGB565(16, 28, 58);
const uint16_t C_EDGE  = RGB565(70, 100, 150);
const uint16_t C_HDR   = RGB565(40, 120, 200);
const uint16_t C_LABEL = RGB565(150, 190, 230);
const uint16_t C_TEXT  = TFT_WHITE;
const uint16_t C_DIM   = RGB565(60, 70, 90);
const uint16_t C_GREEN = RGB565(40, 220, 60);
const uint16_t C_YEL   = RGB565(250, 210, 30);
const uint16_t C_RED   = RGB565(230, 40, 40);
const uint16_t C_CYAN  = RGB565(60, 220, 240);

// ---- 數據 / data ----
struct GenData {
  float battery, utility, output, freq, rpm, kw, amps;
  String engine, sw, base;
  float runHours, fuel, nextService;   // < 0 = 冇資料
};
GenData gd;

// ---- 錶 / gauges ----
struct Zone { float from, to; uint16_t col; };
struct Gauge {
  const char *title;
  float vmin, vmax;
  uint8_t decimals;
  Zone zones[4];
  uint8_t nz;
  float shown;      // 而家指針位置（會慢慢追上真值）
  float drawn;      // 上次畫嘅值
  bool dirty;
};
Gauge gauges[6];

int page = 0;
bool pageDirty = true;
String tagShown = "";

// 停電記錄 / outage tracking
bool outageActive = false;
uint32_t outageStartMs = 0, lastOutageS = 0;
bool hadOutage = false;

// LIVE 狀態
enum LiveState { LV_DEMO, LV_WIFI, LV_ONLINE, LV_OFFLINE };
LiveState liveState = LV_DEMO;
uint32_t lastPoll = 0, lastOkMs = 0;

// =========================================================================
// 工具 / helpers
float parseNum(JsonVariant v) {               // 數字或者 "13.5 V" 都得
  if (v.isNull()) return NAN;
  if (v.is<float>() || v.is<int>() || v.is<long>()) return v.as<float>();
  const char *s = v.as<const char *>();
  if (!s) return NAN;
  while (*s && !((*s >= '0' && *s <= '9') || *s == '-' || *s == '.')) s++;
  if (!*s) return NAN;
  return atof(s);
}

String fmt(float v, int dec) {
  if (isnan(v)) return String("--");
  return String(v, dec);
}

String hms(uint32_t s) {
  char b[16];
  snprintf(b, sizeof(b), "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  return String(b);
}

bool engineRunning() {
  String e = gd.engine; e.toLowerCase();
  if (e.indexOf("run") >= 0 || e.indexOf("exercis") >= 0 || e.indexOf("cool") >= 0 || e.indexOf("warm") >= 0) return true;
  return (!isnan(gd.rpm) && gd.rpm > 500);
}
bool inAlarm() {
  String b = gd.base; b.toLowerCase();
  String e = gd.engine; e.toLowerCase();
  return b.indexOf("alarm") >= 0 || e.indexOf("alarm") >= 0 || e.indexOf("fault") >= 0;
}

// =========================================================================
// 錶設定 / gauge setup（範圍跟住上面額定值自動計）
void setGauge(int i, const char *t, float mn, float mx, uint8_t dec) {
  gauges[i].title = t; gauges[i].vmin = mn; gauges[i].vmax = mx; gauges[i].decimals = dec;
  gauges[i].nz = 0; gauges[i].shown = mn; gauges[i].drawn = NAN; gauges[i].dirty = true;
}
void addZone(int i, float a, float b, uint16_t c) {
  Gauge &g = gauges[i];
  if (g.nz < 4) g.zones[g.nz++] = { a, b, c };
}
void setupGauges() {
  float V = NOMINAL_V, F = NOMINAL_HZ, R = NOMINAL_RPM, K = RATED_KW, B = BATTERY_NOMINAL;
  setGauge(0, "Battery V", 0, B * 1.334f, 1);
  addZone(0, 0, B * 0.95f, C_RED); addZone(0, B * 0.95f, B * 1.02f, C_YEL);
  addZone(0, B * 1.02f, B * 1.25f, C_GREEN); addZone(0, B * 1.25f, B * 1.334f, C_RED);
  setGauge(1, "Utility V", 0, V * 1.25f, 0);
  addZone(1, 0, V * 0.6f, C_RED); addZone(1, V * 0.6f, V * 0.9f, C_YEL);
  addZone(1, V * 0.9f, V * 1.1f, C_GREEN); addZone(1, V * 1.1f, V * 1.25f, C_RED);
  setGauge(2, "Output V", 0, V * 1.25f, 0);
  addZone(2, 0, V * 0.9f, C_YEL); addZone(2, V * 0.9f, V * 1.1f, C_GREEN); addZone(2, V * 1.1f, V * 1.25f, C_RED);
  setGauge(3, "Freq Hz", F * 0.8f, F * 1.2f, 1);
  addZone(3, F * 0.8f, F * 0.95f, C_YEL); addZone(3, F * 0.95f, F * 1.05f, C_GREEN); addZone(3, F * 1.05f, F * 1.2f, C_RED);
  setGauge(4, "RPM", 0, R * 1.25f, 0);
  addZone(4, 0, R * 0.95f, C_YEL); addZone(4, R * 0.95f, R * 1.05f, C_GREEN); addZone(4, R * 1.05f, R * 1.25f, C_RED);
  setGauge(5, "Power kW", 0, K * 1.25f, 1);
  addZone(5, 0, K * 0.8f, C_GREEN); addZone(5, K * 0.8f, K, C_YEL); addZone(5, K, K * 1.25f, C_RED);
}

float gaugeTarget(int i) {
  switch (i) {
    case 0: return gd.battery;
    case 1: return gd.utility;
    case 2: return gd.output;
    case 3: return gd.freq;
    case 4: return gd.rpm;
    default: return gd.kw;
  }
}

// 數值 → 角度。TFT_eSPI 角度：0 = 正下（6 點鐘），順時針。錶由 60° 掃到 300°
const float ANG0 = 60, ANG1 = 300;
float valToAngle(const Gauge &g, float v) {
  float f = (v - g.vmin) / (g.vmax - g.vmin);
  f = constrain(f, 0.0f, 1.0f);
  return ANG0 + f * (ANG1 - ANG0);
}

// 畫一格錶（畫落 sprite 再一次過推上屏 → 唔閃）
template <typename GFX>
void drawGaugeOn(GFX &g, int ox, int oy, int idx, float tgt) {
  Gauge &gg = gauges[idx];
  const int cx = ox + TW / 2, cy = oy + 58, r = 38, ir = 32;
  g.fillRect(ox, oy, TW, TH, C_BG);
  g.fillRoundRect(ox, oy, TW, TH, 7, C_TILE);
  g.drawRoundRect(ox, oy, TW, TH, 7, C_EDGE);
  g.setTextDatum(TC_DATUM);
  g.setTextColor(C_LABEL, C_TILE);
  g.drawString(gg.title, cx, oy + 3, 2);
  // 底色弧 + 色區
  g.drawArc(cx, cy, r, ir, (uint32_t)ANG0, (uint32_t)ANG1, C_DIM, C_TILE, false);
  for (int z = 0; z < gg.nz; z++) {
    uint32_t a = (uint32_t)valToAngle(gg, gg.zones[z].from);
    uint32_t b = (uint32_t)valToAngle(gg, gg.zones[z].to);
    if (b > a) g.drawArc(cx, cy, r, ir, a, b, gg.zones[z].col, C_TILE, false);
  }
  // 刻度 / ticks
  for (int k = 0; k <= 6; k++) {
    float a = (ANG0 + k * (ANG1 - ANG0) / 6.0f) * DEG_TO_RAD;
    int x0 = cx - (ir - 1) * sinf(a), y0 = cy + (ir - 1) * cosf(a);
    int x1 = cx - (ir - 6) * sinf(a), y1 = cy + (ir - 6) * cosf(a);
    g.drawLine(x0, y0, x1, y1, C_TEXT);
  }
  // 細字：最小 / 最大
  g.setTextColor(C_LABEL, C_TILE);
  g.setTextDatum(MC_DATUM);
  const float LA = 74 * DEG_TO_RAD, LR = ir - 13;   // 喺弧兩端內側
  g.drawString(String((int)gg.vmin), cx - (int)(LR * sinf(LA)), cy + (int)(LR * cosf(LA)), 1);
  g.drawString(String((int)roundf(gg.vmax)), cx + (int)(LR * sinf(LA)), cy + (int)(LR * cosf(LA)), 1);
  // 指針 / needle
  bool valid = !isnan(tgt);
  float a = valToAngle(gg, valid ? gg.shown : gg.vmin) * DEG_TO_RAD;
  int nx = cx - (r - 4) * sinf(a), ny = cy + (r - 4) * cosf(a);
  g.drawWedgeLine(cx, cy, nx, ny, 3.0f, 1.0f, valid ? C_TEXT : C_DIM, C_TILE);
  g.fillCircle(cx, cy, 4, C_RED);
  // 數值 / value
  g.setTextDatum(BC_DATUM);
  g.setTextColor(C_TEXT, C_TILE);
  g.drawString(valid ? String(tgt, (unsigned int)gg.decimals) : String("--"), cx, oy + TH - 2, 4);
}

void drawTile(int i) {
  int ox = TILE_X[i % 3], oy = TILE_Y[i / 3];
  float tgt = gaugeTarget(i);
  if (tileOk) {
    drawGaugeOn(tile, 0, 0, i, tgt);
    tile.pushSprite(ox, oy);
  } else {
    drawGaugeOn(tft, ox, oy, i, tgt);           // 後備：sprite 開唔到就直接畫
  }
}

// =========================================================================
// 頂部 / header
String currentTag(uint16_t &bg, uint16_t &fg) {
  fg = TFT_BLACK;
  switch (liveState) {
    case LV_DEMO:    bg = C_YEL;   return "DEMO";
    case LV_WIFI:    bg = C_LABEL; return "WIFI..";
    case LV_ONLINE:  bg = C_GREEN; return "ONLINE";
    default:         bg = C_RED;   fg = TFT_WHITE; return "OFFLINE";
  }
}

void drawHeader(bool force) {
  uint16_t bg, fg;
  String tag = currentTag(bg, fg);
  String full = tag + (outageActive ? "|OUT" : "") + String(page);
  if (!force && full == tagShown) return;
  tagShown = full;
  tft.fillRect(0, 0, W, HDR_H, C_HDR);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, C_HDR);
  tft.drawString(page == 0 ? "Generator Dashboard" : "Generator Status", 6, HDR_H / 2, 2);
  if (outageActive) {
    tft.fillRoundRect(160, 3, 70, HDR_H - 6, 4, C_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, C_RED);
    tft.drawString("OUTAGE", 195, HDR_H / 2, 2);
  }
  // 頁數點 / page dots
  tft.fillCircle(240, HDR_H / 2, 3, page == 0 ? TFT_WHITE : C_DIM);
  tft.fillCircle(250, HDR_H / 2, 3, page == 1 ? TFT_WHITE : C_DIM);
  tft.fillRoundRect(258, 3, 60, HDR_H - 6, 4, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(tag, 288, HDR_H / 2, 2);
}

// =========================================================================
// 第 2 頁：狀態頁（固定框只畫一次，數值有改先重畫 → 唔閃）
const int P_ENG_X = 2, P_ENG_Y = 24, P_W = 157, P_H = 104;
const int P_OUT_X = 161;
const int P_MNT_X = 2, P_MNT_Y = 131, P_MNT_W = 316, P_MNT_H = 107;
#define NFIELDS 14
String fieldCache[NFIELDS];

void panel(int x, int y, int w, int h, const char *title) {
  tft.fillRoundRect(x, y, w, h, 6, C_TILE);
  tft.drawRoundRect(x, y, w, h, 6, C_EDGE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_LABEL, C_TILE);
  tft.drawString(title, x + 6, y + 4, 2);
}

void label(int x, int y, const char *s) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_LABEL, C_TILE);
  tft.drawString(s, x, y, 2);
}

// 有變先畫；padW = 清走舊字嘅闊度
void field(int id, int x, int y, const String &s, uint16_t fg, uint16_t bg, int font, int padW) {
  if (fieldCache[id] == s) return;
  fieldCache[id] = s;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg, bg);
  tft.setTextPadding(padW);
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
}

void drawStatusStatic() {
  tft.fillRect(0, HDR_H, W, H - HDR_H, C_BG);
  panel(P_ENG_X, P_ENG_Y, P_W, P_H, "ENGINE STATE");
  panel(P_OUT_X, P_ENG_Y, P_W, P_H, "OUTPUT");
  panel(P_MNT_X, P_MNT_Y, P_MNT_W, P_MNT_H, "MAINTENANCE & OUTAGE");
  label(P_ENG_X + 6, P_ENG_Y + 58, "Switch:");
  label(P_ENG_X + 6, P_ENG_Y + 78, "Battery:");
  label(P_OUT_X + 6, P_ENG_Y + 24, "Voltage:");
  label(P_OUT_X + 6, P_ENG_Y + 43, "Current:");
  label(P_OUT_X + 6, P_ENG_Y + 62, "Power:");
  label(P_OUT_X + 6, P_ENG_Y + 81, "Freq:");
  label(P_MNT_X + 6, P_MNT_Y + 24, "Run:");
  label(P_MNT_X + 150, P_MNT_Y + 24, "Utility:");
  label(P_MNT_X + 6, P_MNT_Y + 44, "Fuel:");
  label(P_MNT_X + 150, P_MNT_Y + 44, "Service in:");
  label(P_MNT_X + 6, P_MNT_Y + 64, "Outage:");
  for (int i = 0; i < NFIELDS; i++) fieldCache[i] = "\x01";   // 強制全部重畫
}

void updateStatusPage() {
  // 引擎狀態大格：顏色跟狀態
  String e = gd.engine.length() ? gd.engine : String("--");
  uint16_t ebg = C_DIM, efg = TFT_WHITE;
  if (inAlarm()) { ebg = C_RED; efg = TFT_WHITE; }
  else if (e.indexOf("Crank") >= 0 || e.indexOf("Start") >= 0 || e.indexOf("Stopping") >= 0) { ebg = C_YEL; efg = TFT_BLACK; }
  else if (e.indexOf("Cool") >= 0 || e.indexOf("Warm") >= 0) { ebg = C_CYAN; efg = TFT_BLACK; }
  else if (engineRunning()) { ebg = C_GREEN; efg = TFT_BLACK; }
  String key = e + String(ebg);
  if (fieldCache[0] != key) {
    fieldCache[0] = key;
    tft.fillRect(P_ENG_X + 6, P_ENG_Y + 24, P_W - 12, 30, ebg);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(efg, ebg);
    String shown = e.length() > 12 ? e.substring(0, 12) : e;
    tft.drawString(shown, P_ENG_X + 10, P_ENG_Y + 40, 4);
  }
  field(1, P_ENG_X + 72, P_ENG_Y + 58, gd.sw.length() ? gd.sw : String("--"), C_TEXT, C_TILE, 2, 78);
  field(2, P_ENG_X + 72, P_ENG_Y + 78, fmt(gd.battery, 2) + " V", C_YEL, C_TILE, 2, 78);

  field(3, P_OUT_X + 72, P_ENG_Y + 24, fmt(gd.output, 0) + " V", C_TEXT, C_TILE, 2, 78);
  field(4, P_OUT_X + 72, P_ENG_Y + 43, fmt(gd.amps, 2) + " A", C_TEXT, C_TILE, 2, 78);
  field(5, P_OUT_X + 72, P_ENG_Y + 62, fmt(gd.kw, 2) + " kW", C_GREEN, C_TILE, 2, 78);
  field(6, P_OUT_X + 72, P_ENG_Y + 81, fmt(gd.freq, 1) + " Hz", C_TEXT, C_TILE, 2, 78);

  field(7, P_MNT_X + 44, P_MNT_Y + 24, gd.runHours < 0 ? String("--") : fmt(gd.runHours, 2) + " h", C_CYAN, C_TILE, 2, 100);
  uint16_t ucol = (!isnan(gd.utility) && gd.utility >= UTILITY_LOST_V) ? C_GREEN : C_RED;
  field(8, P_MNT_X + 205, P_MNT_Y + 24, fmt(gd.utility, 0) + " V", ucol, C_TILE, 2, 100);
  field(9, P_MNT_X + 44, P_MNT_Y + 44, gd.fuel < 0 ? String("--") : fmt(gd.fuel, 1) + " " FUEL_UNIT, C_GREEN, C_TILE, 2, 100);
  field(10, P_MNT_X + 230, P_MNT_Y + 44, gd.nextService < 0 ? String("--") : fmt(gd.nextService, 1) + " h", C_TEXT, C_TILE, 2, 80);
  String o1, o2;
  uint16_t ocol = C_YEL;
  if (outageActive) {
    o1 = "Utility LOST - in outage for";
    o2 = hms((millis() - outageStartMs) / 1000);
    ocol = C_RED;
  } else if (hadOutage) {
    o1 = "Utility OK. Last outage lasted";
    o2 = hms(lastOutageS);
    ocol = C_GREEN;
  } else {
    o1 = "Utility OK.";
    o2 = "No outage since power-on";
    ocol = C_GREEN;
  }
  field(11, P_MNT_X + 64, P_MNT_Y + 64, o1, ocol, C_TILE, 2, 246);
  field(12, P_MNT_X + 64, P_MNT_Y + 84, o2, ocol, C_TILE, 2, 246);
  String src = DEMO_MODE ? String("Demo data") : String("Genmon");
  field(13, P_MNT_X + 6, P_MNT_Y + 88, src, C_DIM, C_TILE, 1, 56);
}

// =========================================================================
// DEMO：模擬一部後備發電機 / simulated standby generator
enum SimPhase { S_STANDBY, S_DETECT, S_CRANK, S_WARMUP, S_LOADED, S_RETURN, S_COOLDOWN, S_STOPPING };
SimPhase sim = S_STANDBY;
float simT = 0;              // 呢個階段過咗幾多秒（模擬時間）
float simLoadTarget = 4.0f;
float simKw = 0, simRpm = 0, simBatt = 13.6f;
bool simUtility = true;
uint32_t simLastMs = 0;

float noise(float a) { return ((float)random(-1000, 1001) / 1000.0f) * a; }

void simStep() {
  uint32_t now = millis();
  float dt = (now - simLastMs) / 1000.0f * DEMO_SPEED;
  simLastMs = now;
  if (dt > 1.0f) dt = 1.0f;
  simT += dt;
  float rpmTarget = 0, kwTarget = 0, battTarget = 13.6f;
  const char *eng = "Stopped";
  switch (sim) {
    case S_STANDBY:
      simUtility = true; eng = "Stopped";
      if (simT > DEMO_STANDBY_S) { sim = S_DETECT; simT = 0; simUtility = false; }
      break;
    case S_DETECT:                                   // 停電咗，等幾秒確認
      simUtility = false; eng = "Stopped";
      if (simT > 3) { sim = S_CRANK; simT = 0; }
      break;
    case S_CRANK:                                    // 起動摩打轉緊
      eng = "Cranking"; rpmTarget = 350; battTarget = 11.2f;
      if (simT > 2.5f) { sim = S_WARMUP; simT = 0; }
      break;
    case S_WARMUP:                                   // 著咗機，未轉電
      eng = "Warming Up"; rpmTarget = NOMINAL_RPM; battTarget = 14.1f;
      if (simT > 6) { sim = S_LOADED; simT = 0; }
      break;
    case S_LOADED:                                   // 轉電（ATS）俾屋企用
      eng = "Running"; rpmTarget = NOMINAL_RPM; battTarget = 14.1f; kwTarget = simLoadTarget;
      if (random(0, 100) < 4) simLoadTarget = constrain(simLoadTarget + noise(1.5f), RATED_KW * 0.25f, RATED_KW * 0.9f);
      if (simT > DEMO_RUN_S) { sim = S_RETURN; simT = 0; simUtility = true; }
      break;
    case S_RETURN:                                   // 市電返咗，等穩定先轉返
      eng = "Running"; rpmTarget = NOMINAL_RPM; battTarget = 14.1f; kwTarget = simLoadTarget;
      if (simT > 5) { sim = S_COOLDOWN; simT = 0; }
      break;
    case S_COOLDOWN:                                 // 冇負載空轉冷卻
      eng = "Cooldown"; rpmTarget = NOMINAL_RPM; battTarget = 14.1f;
      if (simT > DEMO_COOLDOWN_S) { sim = S_STOPPING; simT = 0; }
      break;
    case S_STOPPING:
      eng = "Stopping"; rpmTarget = 0;
      if (simT > 3) { sim = S_STANDBY; simT = 0; }
      break;
  }
  // 一階追隨（慢慢升 / 跌）
  float k = constrain(dt * 1.5f, 0.0f, 1.0f);
  simRpm += (rpmTarget - simRpm) * k;
  simKw  += (kwTarget - simKw) * constrain(dt * 0.8f, 0.0f, 1.0f);
  simBatt += (battTarget - simBatt) * constrain(dt * 0.7f, 0.0f, 1.0f);

  float ratio = simRpm / NOMINAL_RPM;
  gd.rpm = simRpm > 20 ? simRpm + (simRpm > 2000 ? noise(6) : 0) : 0;
  gd.freq = simRpm > 2000 ? NOMINAL_HZ * ratio + noise(0.05f) : (simRpm > 300 ? NOMINAL_HZ * ratio : 0);
  float droop = 1.0f - 0.015f * (simKw / RATED_KW);
  gd.output = simRpm > 2000 ? NOMINAL_V * constrain(ratio, 0.0f, 1.05f) * droop + noise(0.8f) : 0;
  gd.kw = simKw < 0.05f ? 0 : simKw + noise(0.03f);
  gd.amps = (gd.output > 50) ? gd.kw * 1000.0f / gd.output : 0;
  gd.utility = simUtility ? NOMINAL_V + noise(2.0f) : 0;
  gd.battery = simBatt + noise(0.02f);
  gd.engine = eng;
  gd.sw = "Auto";
  gd.base = engineRunning() ? "RUNNING" : "READY";
  // 運行時數、燃油（燃油用量大約 0.5 L/h + 0.3 L/kWh）
  if (simRpm > 2000) {
    gd.runHours += dt / 3600.0f;
    gd.fuel -= (0.5f + 0.3f * simKw) * dt / 3600.0f;
    if (gd.fuel < FUEL_TANK * 0.1f) gd.fuel = FUEL_TANK * 0.95f;   // 示範：自動「加滿」
  }
  gd.nextService = SERVICE_EVERY_H - fmodf(gd.runHours, SERVICE_EVERY_H);
}

// =========================================================================
// LIVE：讀 Genmon JSON
bool fetchGenmon() {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(GENMON_URL)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[HTTP] %s -> %d\n", GENMON_URL, code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  StaticJsonDocument<256> filter;                 // 淨係攞我哋要嘅 key，慳 RAM
  const char *keys[] = { KEY_BATTERY, KEY_UTILITY, KEY_OUTPUT, KEY_FREQ, KEY_RPM, KEY_KW, KEY_ENGINE, KEY_SWITCH, KEY_BASE };
  for (const char *k : keys) filter[k] = true;
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[JSON] error: %s (Genmon 開咗登入？網址啱唔啱？)\n", err.c_str());
    return false;
  }
  gd.battery = parseNum(doc[KEY_BATTERY]);
  gd.utility = parseNum(doc[KEY_UTILITY]);
  gd.output  = parseNum(doc[KEY_OUTPUT]);
  gd.freq    = parseNum(doc[KEY_FREQ]);
  gd.rpm     = parseNum(doc[KEY_RPM]);
  gd.kw      = parseNum(doc[KEY_KW]);
  gd.engine  = doc[KEY_ENGINE] | "";
  gd.sw      = doc[KEY_SWITCH] | "";
  gd.base    = doc[KEY_BASE] | "";
  // 電流：gui_status_json 冇，用 kW / V 估（單相、功率因數當 1）
  gd.amps = (!isnan(gd.kw) && !isnan(gd.output) && gd.output > 50) ? gd.kw * 1000.0f / gd.output : NAN;
  gd.runHours = -1; gd.fuel = -1; gd.nextService = -1;   // 呢個 endpoint 冇，顯示 "--"
  Serial.printf("[Genmon] batt=%.1f util=%.0f out=%.0f f=%.1f rpm=%.0f kw=%.2f eng=%s\n",
                gd.battery, gd.utility, gd.output, gd.freq, gd.rpm, gd.kw, gd.engine.c_str());
  return true;
}

void liveStep() {
  if (WiFi.status() != WL_CONNECTED) {
    if (liveState == LV_ONLINE) liveState = LV_OFFLINE;
    if (liveState != LV_OFFLINE) liveState = LV_WIFI;
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 15000) {           // 每 15 秒試重連
      lastTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }
  if (millis() - lastPoll < POLL_MS && lastPoll != 0) return;
  lastPoll = millis();
  if (fetchGenmon()) { liveState = LV_ONLINE; lastOkMs = millis(); }
  else if (millis() - lastOkMs > POLL_MS * 3) liveState = LV_OFFLINE;
}

// =========================================================================
// 停電追蹤、LED、背光
void trackOutage() {
  bool lost = !isnan(gd.utility) && gd.utility < UTILITY_LOST_V;
  if (!DEMO_MODE && liveState != LV_ONLINE) return;
  if (lost && !outageActive) { outageActive = true; outageStartMs = millis(); }
  if (!lost && outageActive) {
    outageActive = false; hadOutage = true;
    lastOutageS = (millis() - outageStartMs) / 1000;
  }
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!RGB_LED_ENABLE) return;
  ledcWrite(CH_R, 255 - r);                    // active-low
  ledcWrite(CH_G, 255 - g);
  ledcWrite(CH_B, 255 - b);
}

void updateLed() {
  bool blink = (millis() / 400) % 2;
  uint8_t L = RGB_LED_LEVEL;
  if (inAlarm()) setLed(blink ? L : 0, 0, 0);
  else if (engineRunning()) setLed(0, L, 0);
  else if (outageActive) setLed(blink ? L : 0, 0, 0);
  else setLed(0, 0, 0);
}

int blNow = SCREEN_BRIGHT;
void updateBacklight() {
  if (!AUTO_DIM) return;
  static uint32_t last = 0;
  if (millis() - last < 500) return;
  last = millis();
  int raw = analogRead(LDR_PIN);
  float f = (float)(raw - LDR_RAW_BRIGHT) / (float)(LDR_RAW_DARK - LDR_RAW_BRIGHT);   // 0 = 光，1 = 黑
  f = constrain(f, 0.0f, 1.0f);
  int target = SCREEN_BRIGHT - (int)(f * (SCREEN_BRIGHT - DIM_MIN_BRIGHT));
  blNow += (target - blNow) / 4;               // 慢慢變，唔會跳
  ledcWrite(CH_BL, blNow);
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 5000) { lastPrint = millis(); Serial.printf("LDR raw=%d  backlight=%d\n", raw, blNow); }
}

// =========================================================================
// 觸控：撳一下轉頁
bool touchTapped() {
  static bool wasDown = false;
  bool down = false;
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z >= TOUCH_MIN_Z) {
      down = true;
      if (!wasDown) {
        int x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, W), 0, W - 1);
        int y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, H), 0, H - 1);
        Serial.printf("touch raw x=%d y=%d z=%d -> %d,%d\n", p.x, p.y, p.z, x, y);
      }
    }
  }
  bool tapped = down && !wasDown;              // 只計「啱啱撳落」嗰下
  wasDown = down;
  return tapped;
}

void drawPage() {
  drawHeader(true);
  if (page == 0) {
    tft.fillRect(0, HDR_H, W, H - HDR_H, C_BG);
    for (int i = 0; i < 6; i++) { gauges[i].drawn = NAN; gauges[i].dirty = true; }
  } else {
    drawStatusStatic();
  }
  pageDirty = false;
}

String lastTxt[6];
void updateGauges(float dt) {
  for (int i = 0; i < 6; i++) {
    Gauge &g = gauges[i];
    float t = gaugeTarget(i);
    float goal = isnan(t) ? g.vmin : constrain(t, g.vmin, g.vmax);
    float step = constrain(dt * 4.0f, 0.0f, 1.0f);    // 指針慢慢追
    g.shown += (goal - g.shown) * step;
    float span = g.vmax - g.vmin;
    String txt = isnan(t) ? String("--") : String(t, (unsigned int)g.decimals);
    bool moved = isnan(g.drawn) || fabsf(g.shown - g.drawn) > span * 0.004f;
    if (moved || txt != lastTxt[i] || g.dirty) {
      drawTile(i);
      g.drawn = g.shown; lastTxt[i] = txt; g.dirty = false;
    }
  }
}

// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n11_Generator_Dashboard");

  if (RGB_LED_ENABLE) {
    ledcSetup(CH_R, 5000, 8); ledcAttachPin(LED_R, CH_R);
    ledcSetup(CH_G, 5000, 8); ledcAttachPin(LED_G, CH_G);
    ledcSetup(CH_B, 5000, 8); ledcAttachPin(LED_B, CH_B);
    setLed(0, 0, 0);
  }
  pinMode(LDR_PIN, INPUT);

  tft.init();
  tft.setRotation(1);                          // 橫向 320x240
  tft.fillScreen(C_BG);
  ledcSetup(CH_BL, 5000, 8);                   // 背光 PWM
  ledcAttachPin(BL_PIN, CH_BL);
  ledcWrite(CH_BL, blNow);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tile.setColorDepth(16);
  tileOk = tile.createSprite(TW, TH) != nullptr;   // 104x107x2 ≈ 22KB
  Serial.printf("tile sprite %s\n", tileOk ? "OK" : "FAILED (直接畫)");

  setupGauges();
  gd.battery = 13.6f; gd.utility = NOMINAL_V; gd.output = 0; gd.freq = 0; gd.rpm = 0; gd.kw = 0; gd.amps = 0;
  gd.engine = "Stopped"; gd.sw = "Auto"; gd.base = "READY";
  gd.runHours = 372.0f; gd.fuel = FUEL_TANK * 0.7f; gd.nextService = 0;

  if (DEMO_MODE) {
    liveState = LV_DEMO;
    simLastMs = millis();
  } else {
    liveState = LV_WIFI;
    gd.battery = gd.utility = gd.output = gd.freq = gd.rpm = gd.kw = gd.amps = NAN;
    gd.engine = ""; gd.sw = ""; gd.base = "";
    gd.runHours = gd.fuel = gd.nextService = -1;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("WiFi connecting to %s ...\n", WIFI_SSID);
  }
  drawPage();
}

void loop() {
  static uint32_t lastFrame = 0, lastData = 0, lastStatus = 0;
  uint32_t now = millis();

  if (touchTapped()) { page = 1 - page; pageDirty = true; }
  if (pageDirty) drawPage();

  if (now - lastData >= 200) {                 // 每 0.2 秒更新數據
    lastData = now;
    if (DEMO_MODE) simStep(); else liveStep();
    trackOutage();
    updateLed();
  }
  updateBacklight();
  drawHeader(false);

  if (page == 0) {
    if (now - lastFrame >= 33) {               // ~30fps 郁指針
      float dt = (now - lastFrame) / 1000.0f;
      if (dt > 0.2f) dt = 0.2f;
      lastFrame = now;
      updateGauges(dt);
    }
  } else if (now - lastStatus >= 500) {
    lastStatus = now;
    updateStatusPage();
  }
  delay(2);
}
