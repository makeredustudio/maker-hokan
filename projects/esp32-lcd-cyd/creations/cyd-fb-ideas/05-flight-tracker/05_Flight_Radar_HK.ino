/*
  05_飛行追蹤地圖 Flight Radar HK  (CYD ESP32-2432S028R)
  ------------------------------------------------------------
  靈感來自 CYD Facebook 群組（CYD - Cheap Yellow Display ESP32）Nick Conrad 嘅飛行追蹤地圖：
  https://www.facebook.com/groups/8104937882859357/posts/29194810513445454/
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅 code。

  功能：
   - 以香港國際機場 (HKIA) 為中心嘅雷達地圖：簡化海岸線、跑道、5/10/20 海里距離圈
   - 預設「模擬模式」：唔使 WiFi，自動產生到港、離港、飛越嘅飛機，會轉彎、爬升、降落
   - 每架飛機有呼號、高度、地速標籤同飛行軌跡
   - 撳飛機 = 睇詳細資料（航空公司、機型、航線、高度、速度、航向、爬升率、距離）
   - 右上角 + / - 掣放大縮細
   - 進階：#define USE_LIVE 1 可以改用真實 ADS-B 數據（airplanes.live 公開 API 或者自己嘅 dump1090）

  Library：TFT_eSPI（已設定 CYD）、XPT2046_Touchscreen；USE_LIVE 1 時再加 ArduinoJson 6
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ====== 改呢度 ======
#define USE_LIVE 0               // 0 = 模擬飛機（預設）；1 = 真實 ADS-B（要 WiFi + ArduinoJson）
#define SIM_SPEED 3.0f           // 模擬時間加速幾多倍（1 = 真實速度，飛機郁得好慢）
#define MAX_PLANES 12            // 同一時間最多幾多架
#define SIM_PLANES 9             // 模擬模式目標飛機數量

#if USE_LIVE
const char *WIFI_SSID     = "YourWiFiName";
const char *WIFI_PASSWORD = "YourWiFiPassword";
// 方法 A：公開 API（免 key）— 以 HKIA 為中心 60 海里
const char *LIVE_URL = "https://api.airplanes.live/v2/point/22.308/113.918/60";
// 方法 B：自己嘅 ADS-B 接收器（RTL-SDR + dump1090 / readsb），例如：
// const char *LIVE_URL = "http://192.168.1.50/dump1090-fa/data/aircraft.json";
#define LIVE_INTERVAL_MS 10000   // 幾耐攞一次數據（公開 API 唔好太密）
#endif

// 觸控校正
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// ====== 硬件腳位 ======
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21

// ====== 顏色 ======
#define C_BG     0x0002   // 深海軍藍
#define C_LAND   0x2945   // 海岸線
#define C_RING   0x0228   // 距離圈
#define C_TEXT   0x8C71
#define C_ARR    0x07E0   // 到港：綠
#define C_DEP    0x07FF   // 離港：青
#define C_OVF    0xFFE0   // 飛越：黃
#define C_SEL    0xF81F   // 已選：粉紅
#define C_RWY    0xFFFF

// ====== 地圖 ======
// 以 HKIA 為原點，x = 向東公里，y = 向北公里
const float HKIA_LAT = 22.308f, HKIA_LON = 113.918f;
const float KM_PER_DEG_LAT = 111.0f;
const float KM_PER_DEG_LON = 111.0f * 0.9252f;   // cos(22.3°)
float scalePx = 3.0f;               // 每公里幾多像素
const float ZOOMS[] = { 1.5f, 3.0f, 5.0f };
int zoomIdx = 1;
float viewX = 12.0f, viewY = 3.0f;  // 畫面中心（略為偏東，見到港島九龍）
#define CX 160
#define CY 125

// 簡化海岸線（經度, 緯度）— 只係示意，唔係精確地圖
struct LL { float lon, lat; };
const LL LANTAU[] = { {113.83,22.20},{113.86,22.25},{113.90,22.28},{113.94,22.29},{113.98,22.30},{114.03,22.30},{114.05,22.28},{114.03,22.24},{113.98,22.22},{113.93,22.21},{113.88,22.20},{113.83,22.20} };
const LL AIRPORT[] = { {113.890,22.300},{113.900,22.326},{113.950,22.330},{113.945,22.300},{113.920,22.292},{113.890,22.300} };
const LL HKISLAND[] = { {114.12,22.28},{114.15,22.29},{114.19,22.29},{114.23,22.28},{114.25,22.26},{114.22,22.21},{114.20,22.24},{114.16,22.23},{114.13,22.24},{114.12,22.28} };
const LL MAINLAND[] = { {113.86,22.62},{113.85,22.56},{113.88,22.52},{113.90,22.48},{113.93,22.50},{113.95,22.42},{113.98,22.37},{114.03,22.36},{114.08,22.35},{114.12,22.33},{114.16,22.30},{114.21,22.31},{114.25,22.32},{114.27,22.36},{114.30,22.40},{114.35,22.43} };
const LL MACAU[] = { {113.53,22.21},{113.56,22.22},{113.58,22.19},{113.55,22.15},{113.53,22.21} };
const LL HZMB[] = { {113.93,22.29},{113.85,22.28},{113.70,22.27},{113.58,22.21} };   // 港珠澳大橋
// 跑道（大約）：07L/25R、07R/25L、07C/25C
const LL RWY[][2] = { {{113.893,22.303},{113.936,22.315}}, {{113.897,22.320},{113.940,22.328}}, {{113.893,22.311},{113.938,22.322}} };

// ====== 飛機 ======
enum Kind { ARR, DEP, OVF };
#define TRAIL 8
struct Plane {
  bool active; Kind kind;
  char cs[10]; char type[6]; char route[14]; const char *airline;
  float x, y, hdg, tgtHdg, spd, alt, vs;   // km, km, 度, 度, 節, 呎, 呎/分
  int stage;                                // 模擬用：0 = 去最後進近點, 1 = 最後進近
  float tx[TRAIL], ty[TRAIL]; uint8_t tn; float trailClock;
};
Plane planes[MAX_PLANES];
int selected = -1;
char selectedCs[10] = "";

struct Airline { const char *code, *name, *types[3]; };
const Airline AIRLINES[] = {
  { "CPA", "Cathay Pacific",       { "A359", "B77W", "A333" } },
  { "HKE", "HK Express",           { "A20N", "A21N", "A320" } },
  { "CRK", "Hong Kong Airlines",   { "A333", "A320", "A21N" } },
  { "AHK", "Air Hong Kong",        { "A333", "A306", "A333" } },
  { "CES", "China Eastern",        { "A320", "A321", "B738" } },
  { "CSN", "China Southern",       { "A321", "B738", "A333" } },
  { "SIA", "Singapore Airlines",   { "A359", "B78X", "A388" } },
  { "JAL", "Japan Airlines",       { "B788", "B763", "A359" } },
  { "KAL", "Korean Air",           { "B77W", "A333", "B789" } },
  { "EVA", "EVA Air",              { "B78X", "A321", "B77W" } },
  { "UAE", "Emirates",             { "A388", "B77W", "B77W" } },
  { "FDX", "FedEx",                { "B77L", "MD11", "B763" } },
};
const int N_AIRLINES = sizeof(AIRLINES) / sizeof(AIRLINES[0]);
const char *PORTS[] = { "TPE","NRT","HND","KIX","ICN","PEK","PVG","SIN","BKK","MNL","KUL","SGN","SYD","DXB","LHR","ANC","LAX","CGK" };
const int N_PORTS = sizeof(PORTS) / sizeof(PORTS[0]);

// 25 方向運作：降落向 253°；最後進近點 (FAF) 喺機場東北偏東 16 公里
const float RWY_HDG = 253.0f;
const float FAF_X = 16.0f * sinf(73 * DEG_TO_RAD), FAF_Y = 16.0f * cosf(73 * DEG_TO_RAD);

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);

// ---------------- 小工具 ----------------
float wrap360(float a) { while (a < 0) a += 360; while (a >= 360) a -= 360; return a; }
float angDiff(float a, float b) { float d = wrap360(b - a); return d > 180 ? d - 360 : d; }
float bearingTo(float x0, float y0, float x1, float y1) { return wrap360(atan2f(x1 - x0, y1 - y0) * RAD_TO_DEG); }
float distKm(float x, float y) { return sqrtf(x * x + y * y); }
void llToKm(float lat, float lon, float &x, float &y) { x = (lon - HKIA_LON) * KM_PER_DEG_LON; y = (lat - HKIA_LAT) * KM_PER_DEG_LAT; }
int sx(float x) { return CX + (int)((x - viewX) * scalePx); }
int sy(float y) { return CY - (int)((y - viewY) * scalePx); }
uint16_t kindColor(Kind k) { return k == ARR ? C_ARR : (k == DEP ? C_DEP : C_OVF); }

// ---------------- 模擬：產生飛機 ----------------
void newPlane(Plane &p) {
  memset(&p, 0, sizeof(p));
  p.active = true;
  const Airline &al = AIRLINES[random(N_AIRLINES)];
  p.airline = al.name;
  snprintf(p.cs, sizeof(p.cs), "%s%d", al.code, (int)random(10, 999));
  strncpy(p.type, al.types[random(3)], sizeof(p.type) - 1);
  int r = random(100);
  p.kind = r < 40 ? ARR : (r < 75 ? DEP : OVF);
  const char *port = PORTS[random(N_PORTS)];
  if (p.kind == ARR) {
    float b = random(20, 200);                  // 由東北至西南方向入嚟
    p.x = 55 * sinf(b * DEG_TO_RAD); p.y = 55 * cosf(b * DEG_TO_RAD);
    p.hdg = bearingTo(p.x, p.y, FAF_X, FAF_Y);
    p.alt = random(90, 140) * 100; p.spd = random(250, 290); p.vs = -1500;
    snprintf(p.route, sizeof(p.route), "%s>HKG", port);
  } else if (p.kind == DEP) {
    p.x = 1.5f; p.y = 0.6f; p.hdg = RWY_HDG;   // 由跑道東端起飛向西
    p.alt = 0; p.spd = 150; p.vs = 2500;
    p.tgtHdg = random(150, 330);
    snprintf(p.route, sizeof(p.route), "HKG>%s", port);
  } else {
    float b = random(360);
    p.x = 60 * sinf(b * DEG_TO_RAD); p.y = 60 * cosf(b * DEG_TO_RAD);
    p.hdg = wrap360(b + 180 + random(-35, 35));
    p.alt = random(31, 40) * 1000; p.spd = random(440, 490); p.vs = 0;
    snprintf(p.route, sizeof(p.route), "%s>%s", port, PORTS[random(N_PORTS)]);
  }
  p.tgtHdg = (p.kind == DEP) ? p.tgtHdg : p.hdg;
}

void simStep(Plane &p, float dt) {        // dt = 模擬秒數
  // 導航：決定目標航向同高度
  if (p.kind == ARR) {
    if (p.stage == 0) {
      p.tgtHdg = bearingTo(p.x, p.y, FAF_X, FAF_Y);
      if (p.alt > 3000) p.vs = -1500; else { p.alt = 3000; p.vs = 0; }
      if (p.spd > 180) p.spd -= 1.0f * dt;
      if (distKm(p.x - FAF_X, p.y - FAF_Y) < 2.0f) p.stage = 1;
    } else {
      // 對準跑道，沿 3° 滑翔道落嚟（每公里大約 170 呎）
      float d = distKm(p.x - 2, p.y - 0.6f);
      p.tgtHdg = RWY_HDG + constrain(angDiff(RWY_HDG, bearingTo(p.x, p.y, 2, 0.6f)) * 2, -20.0f, 20.0f);
      float want = d * 170;
      p.vs = constrain((want - p.alt) * 2, -1200.0f, 0.0f);
      if (p.spd > 150) p.spd -= 1.0f * dt;
      if (d < 1.0f) { p.active = false; return; }   // 降落咗
    }
  } else if (p.kind == DEP) {
    if (p.alt < 3000) p.tgtHdg = RWY_HDG; else if (p.stage == 0) { p.stage = 1; p.tgtHdg = wrap360(p.tgtHdg); }
    if (p.alt >= 33000) { p.alt = 33000; p.vs = 0; }
    if (p.spd < 300) p.spd += 1.2f * dt;
  }
  // 轉彎（每秒最多 3 度）
  float d = angDiff(p.hdg, p.tgtHdg);
  float maxTurn = 3.0f * dt;
  p.hdg = wrap360(p.hdg + constrain(d, -maxTurn, maxTurn));
  // 移動：節 → 公里/秒
  float kmps = p.spd * 1.852f / 3600.0f;
  p.x += sinf(p.hdg * DEG_TO_RAD) * kmps * dt;
  p.y += cosf(p.hdg * DEG_TO_RAD) * kmps * dt;
  p.alt = max(0.0f, p.alt + p.vs / 60.0f * dt);
  if (distKm(p.x, p.y) > 65) p.active = false;      // 飛出範圍
}

void addTrail(Plane &p, float dt) {
  p.trailClock += dt;
  if (p.trailClock < 12) return;                    // 每 12 模擬秒記一點
  p.trailClock = 0;
  for (int i = TRAIL - 1; i > 0; i--) { p.tx[i] = p.tx[i - 1]; p.ty[i] = p.ty[i - 1]; }
  p.tx[0] = p.x; p.ty[0] = p.y;
  if (p.tn < TRAIL) p.tn++;
}

// ---------------- 真實 ADS-B（進階） ----------------
#if USE_LIVE
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
uint32_t lastFetch = 0;
const char *liveStatus = "WiFi...";

void fetchLive() {
  if (WiFi.status() != WL_CONNECTED) { liveStatus = "No WiFi"; return; }
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = strncmp(LIVE_URL, "https", 5) == 0;
  if (https) { secure.setInsecure(); http.begin(secure, LIVE_URL); } else http.begin(plain, LIVE_URL);
  http.useHTTP10(true);
  int code = http.GET();
  if (code != 200) { liveStatus = "HTTP err"; http.end(); return; }
  // 只抽需要嘅欄位，慳 RAM（airplanes.live 用 "ac"，dump1090 用 "aircraft"）
  StaticJsonDocument<256> filter;
  for (const char *arr : { "ac", "aircraft" }) {
    JsonObject f = filter[arr][0].to<JsonObject>();
    f["flight"] = true; f["lat"] = true; f["lon"] = true; f["alt_baro"] = true; f["altitude"] = true;
    f["gs"] = true; f["speed"] = true; f["track"] = true; f["baro_rate"] = true; f["t"] = true;
  }
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { liveStatus = "JSON err"; return; }
  JsonArray list = doc["ac"].is<JsonArray>() ? doc["ac"].as<JsonArray>() : doc["aircraft"].as<JsonArray>();
  int n = 0;
  for (JsonObject a : list) {
    if (n >= MAX_PLANES) break;
    if (!a["lat"].is<float>()) continue;
    Plane &p = planes[n];
    memset(&p, 0, sizeof(p));
    p.active = true; p.airline = "(live)";
    strlcpy(p.cs, a["flight"] | "------", sizeof(p.cs));
    for (int i = strlen(p.cs) - 1; i >= 0 && p.cs[i] == ' '; i--) p.cs[i] = 0;
    strlcpy(p.type, a["t"] | "?", sizeof(p.type));
    strlcpy(p.route, "-", sizeof(p.route));
    llToKm(a["lat"], a["lon"], p.x, p.y);
    p.alt = a["alt_baro"].is<int>() ? a["alt_baro"].as<int>() : (a["altitude"] | 0);   // "ground" 會當 0
    p.spd = a["gs"] | (a["speed"] | 0.0f);
    p.hdg = p.tgtHdg = a["track"] | 0.0f;
    p.vs = a["baro_rate"] | 0;
    p.kind = (p.alt > 20000) ? OVF : (p.vs < -300 ? ARR : DEP);
    n++;
  }
  for (int i = n; i < MAX_PLANES; i++) planes[i].active = false;
  // 保持之前揀咗嗰架
  selected = -1;
  for (int i = 0; i < n; i++) if (selectedCs[0] && strcmp(planes[i].cs, selectedCs) == 0) selected = i;
  static char buf[16]; snprintf(buf, sizeof(buf), "LIVE %d", n); liveStatus = buf;
}
#endif

// ---------------- 畫地圖 ----------------
void drawPoly(const LL *pts, int n, uint16_t col) {
  float x0, y0, x1, y1;
  llToKm(pts[0].lat, pts[0].lon, x0, y0);
  for (int i = 1; i < n; i++) {
    llToKm(pts[i].lat, pts[i].lon, x1, y1);
    spr.drawLine(sx(x0), sy(y0), sx(x1), sy(y1), col);
    x0 = x1; y0 = y1;
  }
}
#define POLY(a, c) drawPoly(a, sizeof(a) / sizeof(a[0]), c)

void drawPlane(Plane &p, bool sel) {
  uint16_t col = sel ? C_SEL : kindColor(p.kind);
  int px = sx(p.x), py = sy(p.y);
  // 軌跡
  for (int i = 0; i < p.tn; i++) spr.drawPixel(sx(p.tx[i]), sy(p.ty[i]), col);
  // 三角形飛機符號，指住航向
  float h = p.hdg * DEG_TO_RAD;
  int x1 = px + 7 * sinf(h),        y1 = py - 7 * cosf(h);
  int x2 = px + 4 * sinf(h + 2.4f), y2 = py - 4 * cosf(h + 2.4f);
  int x3 = px + 4 * sinf(h - 2.4f), y3 = py - 4 * cosf(h - 2.4f);
  spr.fillTriangle(x1, y1, x2, y2, x3, y3, col);
  if (sel) spr.drawCircle(px, py, 10, C_SEL);
  // 標籤：呼號 / 高度(百呎) 地速
  char buf[16];
  spr.setTextColor(col);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(p.cs, px + 8, py - 10, 1);
  snprintf(buf, sizeof(buf), "%03d %d", (int)(p.alt / 100), (int)p.spd);
  spr.drawString(buf, px + 8, py - 1, 1);
}

void drawDetails(Plane &p) {
  int y0 = 182;
  spr.fillRoundRect(2, y0, 316, 56, 6, 0x10A2);
  spr.drawRoundRect(2, y0, 316, 56, 6, C_SEL);
  char buf[40];
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString(p.cs, 8, y0 + 4, 4);
  spr.setTextColor(C_TEXT);
  snprintf(buf, sizeof(buf), "%s  %s", p.airline, p.type);
  spr.drawString(buf, 8, y0 + 30, 1);
  snprintf(buf, sizeof(buf), "%s  %s", p.route, p.kind == ARR ? "ARRIVING" : (p.kind == DEP ? "DEPARTING" : "OVERFLIGHT"));
  spr.drawString(buf, 8, y0 + 42, 1);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof(buf), "ALT %5d ft", (int)p.alt);           spr.drawString(buf, 180, y0 + 4, 1);
  snprintf(buf, sizeof(buf), "GS  %5d kt", (int)p.spd);           spr.drawString(buf, 180, y0 + 14, 1);
  snprintf(buf, sizeof(buf), "HDG   %03d", (int)p.hdg);             spr.drawString(buf, 180, y0 + 24, 1);
  snprintf(buf, sizeof(buf), "V/S %+5d fpm", (int)p.vs);          spr.drawString(buf, 180, y0 + 34, 1);
  snprintf(buf, sizeof(buf), "DIST %4.1f km", distKm(p.x, p.y));  spr.drawString(buf, 180, y0 + 44, 1);
}

void render() {
  spr.fillSprite(C_BG);
  // 距離圈 5 / 10 / 20 海里（以 HKIA 為中心）
  const float NM[] = { 5, 10, 20 };
  spr.setTextDatum(TL_DATUM);
  for (float nm : NM) {
    int r = nm * 1.852f * scalePx;
    spr.drawCircle(sx(0), sy(0), r, C_RING);
    char b[8]; snprintf(b, sizeof(b), "%dNM", (int)nm);
    spr.setTextColor(C_RING);
    spr.drawString(b, sx(0) + 2, sy(0) - r + 2, 1);
  }
  POLY(MAINLAND, C_LAND); POLY(LANTAU, C_LAND); POLY(HKISLAND, C_LAND);
  POLY(MACAU, C_LAND); POLY(HZMB, 0x18C3); POLY(AIRPORT, 0x4A69);
  for (auto &r : RWY) {
    float x0, y0, x1, y1;
    llToKm(r[0].lat, r[0].lon, x0, y0); llToKm(r[1].lat, r[1].lon, x1, y1);
    spr.drawLine(sx(x0), sy(y0), sx(x1), sy(y1), C_RWY);
  }
  spr.setTextColor(C_LAND);
  spr.drawString("HKG", sx(-3), sy(-1.5f), 1);
  spr.drawString("Macau", sx(-40), sy(-13), 1);
  spr.drawString("Shenzhen", sx(-2), sy(28), 1);
  spr.drawString("HK Island", sx(22), sy(-10), 1);

  int count = 0;
  for (int i = 0; i < MAX_PLANES; i++) if (planes[i].active) { count++; if (i != selected) drawPlane(planes[i], false); }
  if (selected >= 0 && planes[selected].active) drawPlane(planes[selected], true);

  // 上方資料列
  spr.fillRect(0, 0, 320, 14, 0x0841);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("HKG FLIGHT RADAR", 4, 3, 1);
  char buf[32];
#if USE_LIVE
  snprintf(buf, sizeof(buf), "%s", liveStatus);
#else
  snprintf(buf, sizeof(buf), "SIM x%.0f  %d aircraft", SIM_SPEED, count);
#endif
  spr.setTextDatum(TR_DATUM);
  spr.drawString(buf, 316, 3, 1);
  // 圖例
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ARR); spr.drawString("ARR", 4, 18, 1);
  spr.setTextColor(C_DEP); spr.drawString("DEP", 28, 18, 1);
  spr.setTextColor(C_OVF); spr.drawString("OVF", 52, 18, 1);
  // 放大縮細掣
  spr.drawRoundRect(290, 20, 26, 26, 4, C_TEXT); spr.drawRoundRect(290, 50, 26, 26, 4, C_TEXT);
  spr.setTextDatum(MC_DATUM); spr.setTextColor(TFT_WHITE);
  spr.drawString("+", 303, 33, 2); spr.drawString("-", 303, 63, 2);

  if (selected >= 0 && planes[selected].active) drawDetails(planes[selected]);
  else {
    spr.setTextDatum(BC_DATUM); spr.setTextColor(C_TEXT);
    spr.drawString("Tap a plane for details", 160, 238, 1);
  }
  spr.pushSprite(0, 0);
}

// ---------------- 觸控 ----------------
void handleTouch() {
  if (!(ts.tirqTouched() && ts.touched())) return;
  TS_Point pt = ts.getPoint();
  int x = constrain(map(pt.x, TS_MINX, TS_MAXX, 0, 320), 0, 319);
  int y = constrain(map(pt.y, TS_MINY, TS_MAXY, 0, 240), 0, 239);
  Serial.printf("touch x=%d y=%d\n", x, y);
  if (x >= 284 && y >= 16 && y < 48) { zoomIdx = min(zoomIdx + 1, 2); scalePx = ZOOMS[zoomIdx]; }
  else if (x >= 284 && y >= 48 && y < 82) { zoomIdx = max(zoomIdx - 1, 0); scalePx = ZOOMS[zoomIdx]; }
  else {
    int best = -1; long bestD = 26 * 26;                    // 26 像素內最近嗰架
    for (int i = 0; i < MAX_PLANES; i++) {
      if (!planes[i].active) continue;
      long dx = sx(planes[i].x) - x, dy = sy(planes[i].y) - y;
      if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = i; }
    }
    selected = best;
    strlcpy(selectedCs, best >= 0 ? planes[best].cs : "", sizeof(selectedCs));
  }
  uint32_t t0 = millis();
  while (ts.touched() && millis() - t0 < 1000) delay(10);
}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);
  spr.setColorDepth(8);                       // 全屏 8-bit sprite ≈ 77KB，唔閃
  if (!spr.createSprite(320, 240)) Serial.println("Sprite alloc failed!");
  randomSeed(esp_random());
#if USE_LIVE
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#else
  // 開頭預先產生飛機，再「快轉」一段時間令佢哋散開
  for (int i = 0; i < SIM_PLANES; i++) newPlane(planes[i]);
  for (int k = 0; k < 120; k++) for (int i = 0; i < SIM_PLANES; i++) if (planes[i].active) { simStep(planes[i], 2); addTrail(planes[i], 2); }
#endif
}

uint32_t lastMs = 0;
void loop() {
  handleTouch();
  uint32_t now = millis();
  if (now - lastMs < 100) return;            // 每秒 10 格
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;

#if USE_LIVE
  if (WiFi.status() == WL_CONNECTED && (lastFetch == 0 || now - lastFetch > LIVE_INTERVAL_MS)) { lastFetch = now; fetchLive(); }
  for (int i = 0; i < MAX_PLANES; i++) if (planes[i].active) {     // 兩次更新之間用航向/速度估計位置
    Plane &p = planes[i];
    float kmps = p.spd * 1.852f / 3600.0f;
    p.x += sinf(p.hdg * DEG_TO_RAD) * kmps * dt; p.y += cosf(p.hdg * DEG_TO_RAD) * kmps * dt;
  }
#else
  float sdt = dt * SIM_SPEED;
  int active = 0;
  for (int i = 0; i < MAX_PLANES; i++) {
    if (!planes[i].active) continue;
    simStep(planes[i], sdt);
    addTrail(planes[i], sdt);
    if (planes[i].active) active++;
    else if (i == selected) selected = -1;
  }
  // 唔夠飛機就隨機加一架
  if (active < SIM_PLANES && random(100) < 3) {
    for (int i = 0; i < MAX_PLANES; i++) if (!planes[i].active && i != selected) { newPlane(planes[i]); break; }
  }
#endif
  render();
}
