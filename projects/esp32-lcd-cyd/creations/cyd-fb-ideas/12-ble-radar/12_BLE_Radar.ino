/*
  12_藍牙雷達 / BLE Radar  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 公開群組「CYD - Cheap Yellow Display - ESP32」
  一位匿名成員嘅 BlueWatch 項目（BlueWatch-CYD-Radar）：
  https://www.facebook.com/groups/710623908028293/posts/1090636286693718/
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅 code。
  （未喺實機測試，只係 compile 通過）

  功能 / Features
   - 用 ESP32 內置 BLE 掃描附近嘅藍牙廣播（唔使任何額外零件 / library）
   - 雷達頁：會轉嘅掃描光束；每部裝置一粒點
       距離中心 = 訊號強度 RSSI（愈近中心 = 訊號愈強，大約愈近）
       角度 = 由 MAC 地址計出嚟（固定，唔係真方向！）
       顏色 = RSSI：紅 = 強、黃 = 中、綠 = 弱
       有廣播名就顯示名，冇就顯示 MAC 尾 3 個 byte
   - 列表頁：按 RSSI 由強到弱排，手指上下拖捲動
   - 頂部掣：LIST/RADAR 轉頁、CLEAR 清空、PAUSE 暫停掃描
   - 太耐冇再見到嘅裝置會自動消失（AGE_OUT_S）
   - 雷達用 8-bit sprite 畫，唔會閃

  ⚠ 私隱：只係「聽」公開廣播，唔會連接任何裝置；請唔好用嚟追蹤人。
     手機多數用隨機 MAC，過一陣會換地址，所以同一部手機可能變成幾粒點。

  Library：TFT_eSPI（Larry 嘅 libraries.zip，已設定 CYD）、XPT2046_Touchscreen
           BLE 係 ESP32 core 內置
  Board：ESP32 Dev Module（ESP32 core 2.0.17）。Partition：預設 Default 4MB 就得
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ======================= 改呢度就得 / USER SETTINGS =======================
#define SCAN_TIME_S      4        // 每輪掃描幾多秒（完咗自動再掃）
#define SCAN_INTERVAL_MS 100      // BLE 掃描 interval（毫秒）
#define SCAN_WINDOW_MS   90       // BLE 掃描 window（≤ interval；愈大愈易搵到但更耗電）
#define ACTIVE_SCAN      true     // true = 主動掃描（多啲裝置會回覆名）；false = 只係被動聽
#define MAX_DEVICES      40       // 最多記住幾多部
#define AGE_OUT_S        30       // 幾多秒冇再見到就移除
#define FADE_AFTER_S     10       // 幾多秒冇見到就變暗

// RSSI（dBm，數字愈接近 0 = 訊號愈強）
#define RSSI_STRONG      -35      // 呢個或更強 = 雷達中心
#define RSSI_WEAK        -100     // 呢個或更弱 = 雷達最外圈
#define RSSI_NEAR        -60      // ≥ 呢個 = 紅色「近」
#define RSSI_MID         -75      // ≥ 呢個 = 黃色「中」，再弱 = 綠色「遠」
#define RSSI_MIN_SHOW    -100     // 弱過呢個就唔顯示

#define BEAM_DEG_PER_S   90       // 光束轉速（度 / 秒）
#define MAX_LABELS       14       // 雷達上最多顯示幾多個名（太多會好亂）
#define SCREEN_BRIGHT    200      // 背光 0–255
#define RGB_LED_ENABLE   true     // 背面 LED：有「近」裝置 = 紅、掃描中 = 藍閃、暫停 = 熄
#define RGB_LED_LEVEL    30

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
#define CH_BL        0
#define CH_R         1
#define CH_G         2
#define CH_B         3

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite radar = TFT_eSprite(&tft);   // 8-bit 雷達畫布
TFT_eSprite rowSpr = TFT_eSprite(&tft);  // 列表一行
bool radarOk = false, rowOk = false;

const int W = 320, H = 240;
const int TOP_H = 28;
const int RS = 210;                       // 雷達 sprite 大細
const int RAD_X = 1, RAD_Y = TOP_H + 1;         // 雷達 sprite 位置
const int RC = RS / 2, RR = RS / 2 - 2;   // 中心 / 半徑
const int PX = 216;                       // 右邊資料欄 x
const int LIST_Y = TOP_H + 16, ROW_H = 19;
const int ROWS = (H - LIST_Y) / ROW_H;    // 10 行

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
const uint16_t C_BG    = RGB565(0, 16, 48);
const uint16_t C_GRID  = RGB565(30, 80, 140);
const uint16_t C_BEAM  = RGB565(40, 230, 240);
const uint16_t C_BTN   = RGB565(20, 60, 150);
const uint16_t C_BTNON = RGB565(220, 120, 20);
const uint16_t C_EDGE  = RGB565(120, 170, 240);
const uint16_t C_LABEL = RGB565(150, 200, 240);
const uint16_t C_TEXT  = TFT_WHITE;
const uint16_t C_NEAR  = RGB565(255, 60, 50);
const uint16_t C_MIDC  = RGB565(255, 220, 40);
const uint16_t C_FAR   = RGB565(60, 230, 90);
const uint16_t C_ROW1  = RGB565(6, 30, 80);
const uint16_t C_ROW2  = RGB565(10, 40, 100);

// ---- 裝置表 / device table ----
struct Dev {
  bool used;
  uint8_t mac[6];
  char name[20];          // "" = 冇廣播名
  uint16_t company;       // 0xFFFF = 冇 manufacturer data
  uint8_t addrType;       // 0 public, 1 random ...
  float rssi;             // 平滑咗嘅 RSSI
  int8_t rssiRaw;
  uint16_t angle;         // 0..359（由 MAC hash 計，固定）
  uint32_t firstSeen, lastSeen;
};
Dev devs[MAX_DEVICES];
portMUX_TYPE devMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t advCount = 0;

BLEScan *scan = nullptr;
volatile bool scanRunning = false;
bool paused = false;
int page = 0;                 // 0 = 雷達，1 = 列表
int listScroll = 0;           // 列表捲咗幾多行
float beam = 0;
uint32_t pausedAt = 0;

// =========================================================================
uint16_t hashAngle(const uint8_t *m) {     // FNV-1a hash → 固定角度
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6; i++) { h ^= m[i]; h *= 16777619u; }
  return h % 360;
}

const char *companyName(uint16_t id) {     // 幾個常見 Bluetooth SIG 公司編號
  switch (id) {
    case 0x004C: return "Apple";
    case 0x0006: return "Microsoft";
    case 0x0075: return "Samsung";
    case 0x00E0: return "Google";
    case 0x0087: return "Garmin";
    default:     return nullptr;
  }
}

void shortMac(const Dev &d, char *out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X", d.mac[3], d.mac[4], d.mac[5]);
}

// 雷達 / 列表顯示嘅名：廣播名 > 公司名 + MAC 尾 > MAC 尾
void labelOf(const Dev &d, char *out, size_t n) {
  char sm[10]; shortMac(d, sm, sizeof(sm));
  if (d.name[0]) { snprintf(out, n, "%s", d.name); return; }
  const char *c = companyName(d.company);
  if (c) snprintf(out, n, "%s %s", c, sm + 3);
  else snprintf(out, n, "%s", sm);
}

uint16_t rssiColour(float r) {
  if (r >= RSSI_NEAR) return C_NEAR;
  if (r >= RSSI_MID) return C_MIDC;
  return C_FAR;
}

uint16_t dim(uint16_t c, float f) {        // 將顏色變暗（f 0..1）
  uint8_t r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  uint8_t br = ((C_BG >> 11) & 0x1F) << 3, bg = ((C_BG >> 5) & 0x3F) << 2, bb = (C_BG & 0x1F) << 3;
  r = br + (r - br) * f; g = bg + (g - bg) * f; b = bb + (b - bb) * f;
  return RGB565(r, g, b);
}

int rssiToRadius(float r) {
  float f = (RSSI_STRONG - r) / (float)(RSSI_STRONG - RSSI_WEAK);
  f = constrain(f, 0.08f, 1.0f);
  return (int)(f * RR);
}

// =========================================================================
// BLE 掃描 callback（喺藍牙 task 行，所以要用 devMux 保護）
class RadarCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice adv) override {
    uint8_t mac[6];
    memcpy(mac, *adv.getAddress().getNative(), 6);
    int rssi = adv.getRSSI();
    char nm[20] = "";
    if (adv.haveName()) {
      std::string s = adv.getName();
      size_t j = 0;
      for (size_t i = 0; i < s.size() && j < sizeof(nm) - 1; i++) {
        char c = s[i];
        nm[j++] = (c >= 32 && c <= 126) ? c : '?';   // TFT 字型淨係有英文，其他字變 ?
      }
      nm[j] = 0;
    }
    uint16_t company = 0xFFFF;
    if (adv.haveManufacturerData()) {
      std::string md = adv.getManufacturerData();
      if (md.size() >= 2) company = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
    }
    uint8_t at = (uint8_t)adv.getAddressType();
    uint32_t now = millis();

    portENTER_CRITICAL(&devMux);
    advCount++;
    int slot = -1, freeSlot = -1, oldest = -1;
    uint32_t oldestT = 0xFFFFFFFF;
    for (int i = 0; i < MAX_DEVICES; i++) {
      if (!devs[i].used) { if (freeSlot < 0) freeSlot = i; continue; }
      if (memcmp(devs[i].mac, mac, 6) == 0) { slot = i; break; }
      if (devs[i].lastSeen < oldestT) { oldestT = devs[i].lastSeen; oldest = i; }
    }
    bool isNew = false;
    if (slot < 0) {
      slot = (freeSlot >= 0) ? freeSlot : oldest;    // 滿咗就頂走最耐冇見嗰部
      isNew = true;
    }
    if (slot >= 0) {
      Dev &d = devs[slot];
      if (isNew) {
        memset(&d, 0, sizeof(d));
        d.used = true;
        memcpy(d.mac, mac, 6);
        d.rssi = rssi;
        d.angle = hashAngle(mac);
        d.firstSeen = now;
        d.company = 0xFFFF;
      } else {
        d.rssi = d.rssi * 0.7f + rssi * 0.3f;        // 平滑，唔會跳嚟跳去
      }
      d.rssiRaw = rssi;
      d.lastSeen = now;
      d.addrType = at;
      if (nm[0]) strncpy(d.name, nm, sizeof(d.name) - 1);   // 名一旦見過就記住
      if (company != 0xFFFF) d.company = company;
    }
    portEXIT_CRITICAL(&devMux);
  }
};
RadarCallbacks radarCb;

void scanDone(BLEScanResults) { scanRunning = false; }

void startScan() {
  if (paused || scanRunning) return;
  scan->clearResults();
  scanRunning = scan->start(SCAN_TIME_S, scanDone, false);
}

// 抄一份快照出嚟畫（唔使一路鎖住）
int snapshot(Dev *out) {
  int n = 0;
  portENTER_CRITICAL(&devMux);
  for (int i = 0; i < MAX_DEVICES; i++)
    if (devs[i].used && devs[i].rssi >= RSSI_MIN_SHOW) out[n++] = devs[i];
  portEXIT_CRITICAL(&devMux);
  return n;
}

void sortByRssi(Dev *a, int n) {           // 插入排序（裝置唔多，夠快）
  for (int i = 1; i < n; i++) {
    Dev t = a[i]; int j = i - 1;
    while (j >= 0 && a[j].rssi < t.rssi) { a[j + 1] = a[j]; j--; }
    a[j + 1] = t;
  }
}

void ageOut() {
  if (paused) return;
  uint32_t now = millis();
  portENTER_CRITICAL(&devMux);
  for (int i = 0; i < MAX_DEVICES; i++)
    if (devs[i].used && now - devs[i].lastSeen > (uint32_t)AGE_OUT_S * 1000) devs[i].used = false;
  portEXIT_CRITICAL(&devMux);
}

void clearAll() {
  portENTER_CRITICAL(&devMux);
  for (int i = 0; i < MAX_DEVICES; i++) devs[i].used = false;
  portEXIT_CRITICAL(&devMux);
  listScroll = 0;
}

Dev snap[MAX_DEVICES];

// =========================================================================
// 頂部掣 / top buttons
struct Btn { int x, w; };
const Btn BTN[3] = { { 2, 72 }, { 78, 72 }, { 154, 72 } };

void drawButtons() {
  tft.fillRect(0, 0, W, TOP_H, C_BG);
  const char *lab[3] = { page == 0 ? "LIST" : "RADAR", "CLEAR", paused ? "RESUME" : "PAUSE" };
  for (int i = 0; i < 3; i++) {
    uint16_t bg = (i == 2 && paused) ? C_BTNON : C_BTN;
    tft.fillRoundRect(BTN[i].x, 2, BTN[i].w, TOP_H - 4, 5, bg);
    tft.drawRoundRect(BTN[i].x, 2, BTN[i].w, TOP_H - 4, 5, C_EDGE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT, bg);
    tft.drawString(lab[i], BTN[i].x + BTN[i].w / 2, TOP_H / 2, 2);
  }
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_BEAM, C_BG);
  tft.drawString("BLE Radar", W - 4, TOP_H / 2, 2);
}

// =========================================================================
// 雷達頁 / radar page
void radarXY(uint16_t angDeg, int r, int &x, int &y) {   // 0° = 上，順時針
  float a = angDeg * DEG_TO_RAD;
  x = RC + (int)(r * sinf(a));
  y = RC - (int)(r * cosf(a));
}

void drawRadarFrame(int n) {
  TFT_eSprite &s = radar;
  s.fillSprite(C_BG);
  // 光束同尾巴 / beam with fading trail
  const int TRAIL = 45, STEP = 3;
  for (int k = TRAIL; k >= 0; k -= STEP) {
    float f = 1.0f - (float)k / TRAIL;
    uint16_t col = dim(C_BEAM, 0.08f + 0.6f * f * f);
    float a1 = (beam - k) * DEG_TO_RAD, a2 = (beam - k + STEP + 0.5f) * DEG_TO_RAD;
    s.fillTriangle(RC, RC, RC + RR * sinf(a1), RC - RR * cosf(a1), RC + RR * sinf(a2), RC - RR * cosf(a2), col);
  }
  // 圈：RSSI 門檻 / rings at RSSI thresholds
  const int ringR[3] = { rssiToRadius(RSSI_NEAR), rssiToRadius(RSSI_MID), RR };
  for (int i = 0; i < 3; i++) s.drawCircle(RC, RC, ringR[i], C_GRID);
  s.drawFastHLine(RC - RR, RC, RR * 2, C_GRID);
  s.drawFastVLine(RC, RC - RR, RR * 2, C_GRID);
  s.setTextDatum(BL_DATUM);
  s.setTextColor(C_GRID);
  s.drawString(String(RSSI_NEAR), RC + 3, RC - ringR[0], 1);
  s.drawString(String(RSSI_MID), RC + 3, RC - ringR[1], 1);
  s.drawString(String(RSSI_WEAK), RC + 3, RC - RR + 9, 1);
  float bx = sinf(beam * DEG_TO_RAD), by = -cosf(beam * DEG_TO_RAD);
  s.drawLine(RC, RC, RC + RR * bx, RC + RR * by, C_BEAM);
  s.fillCircle(RC, RC, 3, C_BEAM);

  // 裝置點 / device dots（弱嘅先畫，強嘅喺最上）
  uint32_t now = millis();
  int labels = 0;
  for (int i = n - 1; i >= 0; i--) {
    const Dev &d = snap[i];
    int x, y;
    radarXY(d.angle, rssiToRadius(d.rssi), x, y);
    float behind = fmodf(beam - d.angle + 720.0f, 360.0f);   // 光束掃過咗幾耐
    float glow = behind < 90 ? 1.0f - behind / 180.0f : 0.5f;
    uint32_t ageMs = paused ? 0 : now - d.lastSeen;
    if (ageMs > (uint32_t)FADE_AFTER_S * 1000) glow *= 0.5f;
    uint16_t col = dim(rssiColour(d.rssi), glow);
    int dotR = d.rssi >= RSSI_NEAR ? 4 : 3;
    s.fillCircle(x, y, dotR, col);
    if (behind < 12) s.drawCircle(x, y, dotR + 3, dim(C_TEXT, 0.7f));     // 掃中嗰下「叮」一圈
  }
  for (int i = 0; i < n && labels < MAX_LABELS; i++, labels++) {           // 強嘅先有名
    const Dev &d = snap[i];
    int x, y;
    radarXY(d.angle, rssiToRadius(d.rssi), x, y);
    char lb[24]; labelOf(d, lb, sizeof(lb));
    lb[12] = 0;                                                            // 最多 12 個字
    bool right = x < RS - 70;
    s.setTextDatum(right ? ML_DATUM : MR_DATUM);
    s.setTextColor(C_LABEL);
    s.drawString(lb, right ? x + 6 : x - 6, y, 1);
  }
  if (paused) {
    s.setTextDatum(TC_DATUM);
    s.setTextColor(C_BTNON);
    s.drawString("PAUSED", RC, RC + 20, 2);
  }
  radar.pushSprite(RAD_X, RAD_Y);
}

// 右邊資料欄 / side panel（有變先畫）
String panelCache[8];
void panelField(int id, int y, const String &s, uint16_t col, int font) {
  if (panelCache[id] == s) return;
  panelCache[id] = s;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(col, C_BG);
  tft.setTextPadding(W - PX - 2);
  tft.drawString(s, PX, y, font);
  tft.setTextPadding(0);
}

void drawSideStatic() {
  tft.fillRect(RS + 2, TOP_H, W - RS - 2, H - TOP_H, C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_LABEL, C_BG);
  tft.drawString("Nearest:", PX, TOP_H + 66, 2);
  tft.drawString("Legend:", PX, TOP_H + 136, 2);
  tft.fillCircle(PX + 5, TOP_H + 161, 4, C_NEAR);
  tft.fillCircle(PX + 5, TOP_H + 177, 4, C_MIDC);
  tft.fillCircle(PX + 5, TOP_H + 193, 4, C_FAR);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(String(">= ") + RSSI_NEAR, PX + 14, TOP_H + 154, 2);
  tft.drawString(String(">= ") + RSSI_MID, PX + 14, TOP_H + 170, 2);
  tft.drawString(String("< ") + RSSI_MID, PX + 14, TOP_H + 186, 2);
  for (int i = 0; i < 8; i++) panelCache[i] = "\x01";
}

void updateSide(int n) {
  int named = 0;
  for (int i = 0; i < n; i++) if (snap[i].name[0]) named++;
  panelField(0, TOP_H + 4, String("Devices: ") + n, C_TEXT, 2);
  panelField(1, TOP_H + 22, String("Named: ") + named, C_TEXT, 2);
  panelField(2, TOP_H + 40, paused ? String("Scan: PAUSED") : String("Scan: ON"), paused ? C_BTNON : C_FAR, 2);
  if (n > 0) {
    char lb[24]; labelOf(snap[0], lb, sizeof(lb)); lb[14] = 0;
    panelField(3, TOP_H + 84, String(lb), rssiColour(snap[0].rssi), 2);
    panelField(4, TOP_H + 102, String(snap[0].rssiRaw) + " dBm", rssiColour(snap[0].rssi), 2);
  } else {
    panelField(3, TOP_H + 84, "--", C_TEXT, 2);
    panelField(4, TOP_H + 102, "", C_TEXT, 2);
  }
  panelField(5, H - 12, String("age-out ") + AGE_OUT_S + "s", C_GRID, 1);
}

// =========================================================================
// 列表頁 / list page
void drawListHeader(int n) {
  tft.fillRect(0, TOP_H, W, LIST_Y - TOP_H, C_BTN);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_LABEL, C_BTN);
  int y = TOP_H + (LIST_Y - TOP_H) / 2;
  tft.drawString("Name", 4, y, 2);
  tft.drawString("Info", 142, y, 2);
  tft.drawString("RSSI", 228, y, 2);
  tft.setTextDatum(MR_DATUM);
  char buf[20];
  if (n == 0) snprintf(buf, sizeof(buf), "0");
  else snprintf(buf, sizeof(buf), "%d-%d/%d", listScroll + 1, min(n, listScroll + ROWS), n);
  tft.setTextPadding(50);
  tft.drawString(buf, W - 3, y, 1);
  tft.setTextPadding(0);
}

template <typename GFX>
void drawRowOn(GFX &g, int oy, int idx, const Dev *d) {
  uint16_t bg = (idx % 2) ? C_ROW2 : C_ROW1;
  g.fillRect(0, oy, W, ROW_H, bg);
  if (!d) return;
  char lb[24]; labelOf(*d, lb, sizeof(lb)); lb[18] = 0;
  uint16_t col = rssiColour(d->rssi);
  g.fillCircle(6, oy + ROW_H / 2, 3, col);
  g.setTextDatum(ML_DATUM);
  g.setTextColor(C_TEXT, bg);
  g.drawString(lb, 14, oy + ROW_H / 2, 2);
  char info[24], sm[10];
  shortMac(*d, sm, sizeof(sm));
  uint32_t age = paused ? 0 : (millis() - d->lastSeen) / 1000;
  snprintf(info, sizeof(info), "%s %s %lus", sm, d->addrType == 0 ? "pub" : "rnd", (unsigned long)age);
  g.setTextColor(C_LABEL, bg);
  g.drawString(info, 142, oy + ROW_H / 2, 1);
  int bw = constrain(map((int)d->rssi, RSSI_WEAK, RSSI_STRONG, 2, 50), 2, 50);   // 訊號條
  g.fillRect(228, oy + 6, 50, ROW_H - 12, dim(C_GRID, 0.5f));
  g.fillRect(228, oy + 6, bw, ROW_H - 12, col);
  g.setTextDatum(MR_DATUM);
  g.setTextColor(col, bg);
  g.drawString(String(d->rssiRaw), W - 4, oy + ROW_H / 2, 2);
}

void drawList(int n) {
  int maxScroll = max(0, n - ROWS);
  listScroll = constrain(listScroll, 0, maxScroll);
  drawListHeader(n);
  for (int r = 0; r < ROWS; r++) {
    int idx = listScroll + r;
    const Dev *d = idx < n ? &snap[idx] : nullptr;
    if (rowOk) { drawRowOn(rowSpr, 0, idx, d); rowSpr.pushSprite(0, LIST_Y + r * ROW_H); }
    else drawRowOn(tft, LIST_Y + r * ROW_H, idx, d);
  }
  int bottom = LIST_Y + ROWS * ROW_H;
  if (bottom < H) tft.fillRect(0, bottom, W, H - bottom, C_BG);
}

// =========================================================================
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!RGB_LED_ENABLE) return;
  ledcWrite(CH_R, 255 - r);                    // active-low
  ledcWrite(CH_G, 255 - g);
  ledcWrite(CH_B, 255 - b);
}

void enterPage() {
  drawButtons();
  if (page == 0) {
    if (!radarOk) tft.fillRect(0, TOP_H, RS + 2, H - TOP_H, C_BG);
    drawSideStatic();
  } else {
    tft.fillRect(0, TOP_H, W, H - TOP_H, C_BG);
  }
}

// 觸控：撳掣 / 列表拖動
bool tDown = false, tDragged = false;
int tStartY = 0, tStartScroll = 0;
bool listDirty = true;

void handleTouch() {
  bool down = false;
  int x = 0, y = 0;
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z >= TOUCH_MIN_Z) {
      down = true;
      x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, W), 0, W - 1);
      y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, H), 0, H - 1);
    }
  }
  if (down && !tDown) {                        // 啱啱撳落
    tDown = true; tDragged = false;
    tStartY = y; tStartScroll = listScroll;
    Serial.printf("touch raw -> %d,%d\n", x, y);
    if (y < TOP_H) {
      for (int i = 0; i < 3; i++) {
        if (x >= BTN[i].x && x < BTN[i].x + BTN[i].w) {
          if (i == 0) { page = 1 - page; enterPage(); listDirty = true; }
          else if (i == 1) { clearAll(); listDirty = true; Serial.println("CLEAR"); }
          else {
            paused = !paused;
            if (paused) { scan->stop(); scanRunning = false; pausedAt = millis(); }
            else {                                     // 恢復：暫停咗嘅時間唔計入 age
              uint32_t dtp = millis() - pausedAt;
              portENTER_CRITICAL(&devMux);
              for (int k = 0; k < MAX_DEVICES; k++) if (devs[k].used) devs[k].lastSeen += dtp;
              portEXIT_CRITICAL(&devMux);
            }
            drawButtons(); listDirty = true;
            Serial.println(paused ? "PAUSE" : "RESUME");
          }
        }
      }
    }
  } else if (down && tDown && page == 1 && tStartY >= LIST_Y) {   // 列表拖動捲
    int dy = tStartY - y;
    if (abs(dy) > 8) tDragged = true;
    if (tDragged) {
      int ns = tStartScroll + dy / ROW_H;
      if (ns != listScroll) { listScroll = ns; listDirty = true; }
    }
  } else if (!down) {
    tDown = false;
  }
}

// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n12_BLE_Radar");

  if (RGB_LED_ENABLE) {
    ledcSetup(CH_R, 5000, 8); ledcAttachPin(LED_R, CH_R);
    ledcSetup(CH_G, 5000, 8); ledcAttachPin(LED_G, CH_G);
    ledcSetup(CH_B, 5000, 8); ledcAttachPin(LED_B, CH_B);
    setLed(0, 0, 0);
  }

  tft.init();
  tft.setRotation(1);                          // 橫向 320x240
  tft.fillScreen(C_BG);
  ledcSetup(CH_BL, 5000, 8);                   // 背光 PWM
  ledcAttachPin(BL_PIN, CH_BL);
  ledcWrite(CH_BL, SCREEN_BRIGHT);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 先開 sprite（BLE 開咗之後 RAM 會少咗）
  radar.setColorDepth(8);
  radarOk = radar.createSprite(RS, RS) != nullptr;     // 210x210x1 ≈ 44KB
  rowSpr.setColorDepth(16);
  rowOk = rowSpr.createSprite(W, ROW_H) != nullptr;    // 320x19x2 ≈ 12KB
  Serial.printf("sprites radar=%d row=%d, free heap %u\n", radarOk, rowOk, ESP.getFreeHeap());

  BLEDevice::init("CYD-Radar");
  scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&radarCb, true);   // true = 每個廣播都報（RSSI 會更新）
  scan->setActiveScan(ACTIVE_SCAN);
  scan->setInterval(SCAN_INTERVAL_MS);
  scan->setWindow(SCAN_WINDOW_MS);
  Serial.printf("BLE ready, free heap %u\n", ESP.getFreeHeap());

  enterPage();
  startScan();
}

void loop() {
  static uint32_t lastFrame = 0, lastSlow = 0, lastPrint = 0;
  uint32_t now = millis();

  handleTouch();
  if (!paused && !scanRunning) startScan();     // 一輪完咗再掃

  if (now - lastSlow >= 1000) {                 // 每秒：移除舊裝置、LED
    lastSlow = now;
    ageOut();
    listDirty = true;
  }

  if (page == 0) {
    uint32_t frameMs = radarOk ? 40 : 500;      // 冇 sprite 就畫慢啲
    if (now - lastFrame >= frameMs) {
      float dt = (now - lastFrame) / 1000.0f;
      if (dt > 0.5f) dt = 0.5f;
      lastFrame = now;
      if (!paused) beam = fmodf(beam + BEAM_DEG_PER_S * dt, 360.0f);
      int n = snapshot(snap);
      sortByRssi(snap, n);
      if (radarOk) drawRadarFrame(n);
      else {                                    // 後備：直接畫點（會閃少少）
        tft.fillRect(0, TOP_H, RS + 2, H - TOP_H, C_BG);
        for (int i = 0; i < n; i++) {
          int x, y; radarXY(snap[i].angle, rssiToRadius(snap[i].rssi), x, y);
          tft.fillCircle(RAD_X + x, RAD_Y + y, 3, rssiColour(snap[i].rssi));
        }
      }
      updateSide(n);
      // LED
      bool blink = (now / 500) % 2;
      if (paused) setLed(0, 0, 0);
      else if (n > 0 && snap[0].rssi >= RSSI_NEAR) setLed(RGB_LED_LEVEL, 0, 0);
      else setLed(0, 0, blink ? RGB_LED_LEVEL : 0);
    }
  } else if (listDirty) {
    listDirty = false;
    int n = snapshot(snap);
    sortByRssi(snap, n);
    drawList(n);
  }

  if (now - lastPrint >= 5000) {
    lastPrint = now;
    int n = snapshot(snap);
    Serial.printf("devices=%d adverts=%lu heap=%u\n", n, (unsigned long)advCount, ESP.getFreeHeap());
  }
  delay(1);
}
