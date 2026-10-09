/*
  07_CCTV多鏡頭監察屏 / CCTV Multi-Camera Viewer  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------
  靈感來自 Facebook 公開群組「CYD - Cheap Yellow Display - ESP32」
  Tee Jirayu 嘅帖子「ESP32 CYD as a CCTV server」：
  https://www.facebook.com/groups/710623908028293/posts/1096544776102869/
  原作者冇公開 code。呢份係學造工作室「AI 補完」自己重寫嘅版本，
  唔係原作者嘅程式。（未喺實機測試，只係 compile 通過）

  做法 / How it works
   - 唔解 RTSP / H.264（ESP32 做唔到），而係每隔一陣經 HTTP 攞每部鏡頭嘅
     「JPEG 快照」(snapshot)，用 ESP32 ROM 入面嘅 TJpgDec 解碼，畫落 2x2 格仔。
     Fetches small JPEG snapshots over plain HTTP and decodes them with the
     ESP32 ROM TJpgDec (no extra JPEG library needed).
   - 2x2 格 + 底部 「< PAGE」「PAGE >」掣 + 時間（NTP）
   - 撳一格 = 全屏睇嗰部鏡頭；全屏時撳中間返 2x2，撳 < > 轉上/下一部
   - DEMO：冇 WiFi / 鏡頭連唔到 / DEMO_MODE=true，格仔會顯示動畫測試畫面

  Library：TFT_eSPI（Larry 嘅 libraries.zip，已設定 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
*/

#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "esp32/rom/tjpgd.h"     // ESP32 ROM 內置 JPEG 解碼器（唔使裝 library）

// ======================= 改呢度就得 / USER SETTINGS =======================
const char *WIFI_SSID     = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// true = 唔連 WiFi，全部格仔用動畫測試畫面（冇鏡頭都可以試 UI）
#define DEMO_MODE        false

// 時區（香港 HKT-8）同 NTP 伺服器
const char *TZ_INFO     = "HKT-8";
const char *NTP_SERVER1 = "pool.ntp.org";
const char *NTP_SERVER2 = "time.google.com";

// 鏡頭清單：名稱、JPEG 快照網址、(可選) HTTP Basic 帳號密碼（唔使就留 ""）
//  - ESP32-CAM（CameraWebServer 範例）：  http://<ip>/capture
//    記得喺佢網頁將 Resolution 揀 QVGA(320x240) 或者更細！
//  - 好多 IP cam：  http://<ip>/snapshot.jpg 或 /cgi-bin/snapshot.cgi 等（睇說明書）
//  - 相片一定要細：建議 160x120 至 320x240、最好 < 30KB；
//    鏡頭只出大圖就用 sub-stream，或者用電腦/NAS 做個 proxy 縮細先。
//  - 只支援 baseline JPEG、HTTP（唔係 HTTPS）、Basic auth（唔支援 Digest）
struct Camera {
  const char *name;
  const char *url;
  const char *user;
  const char *pass;
};
Camera CAMERAS[] = {
  { "CAM1", "http://192.168.1.101/capture",      "",      ""       },
  { "CAM2", "http://192.168.1.102/capture",      "",      ""       },
  { "DOOR", "http://192.168.1.103/snapshot.jpg", "admin", "123456" },
  { "SHOP", "http://192.168.1.104/snapshot.jpg", "",      ""       },
  { "CAM5", "http://192.168.1.105/capture",      "",      ""       },
  // 想加幾多部都得，每 4 部一頁 / add as many as you like, 4 per page
};

#define HTTP_TIMEOUT_MS     2500    // 每張相最多等幾耐（毫秒）
#define REFRESH_GAP_MS      150     // 每攞完一張相休息幾耐
#define MAX_JPEG_BYTES      40000   // JPEG 最大幾多 bytes（太大會冇記憶體）
#define FAILS_BEFORE_DEMO   2       // 連續失敗幾次就當「離線」
#define RETRY_OFFLINE_MS    15000   // 離線鏡頭每隔幾耐先再試（免得拖慢成個畫面）
#define SCREEN_BRIGHT       255     // 背光 0-255
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

// 觸控校正（撳唔準就睇 Serial Monitor 嘅 raw 值再改）
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;

// ---- 佈局 / layout (landscape 320x240) ----
const int SCR_W = 320, SCR_H = 240;
const int BAR_H = 24;                  // 底部控制列
const int VIEW_H = SCR_H - BAR_H;      // 216
const int TILE_W = SCR_W / 2;          // 160
const int TILE_H = VIEW_H / 2;         // 108
const int BTN_W = 70;

const int NUM_CAMS = sizeof(CAMERAS) / sizeof(CAMERAS[0]);
const int NUM_PAGES = (NUM_CAMS + 3) / 4;

struct CamState {
  uint8_t fails;
  bool everOk;
  uint32_t lastOkMs;
  uint32_t lastTryMs;
  char lastErr[16];
};
CamState camState[NUM_CAMS];

int page = 0;
int fullCam = -1;          // -1 = 2x2 grid；>=0 = 全屏睇邊部
int slot = 0;              // 而家輪到邊格更新
bool wifiOk = false;
bool timeOk = false;
uint8_t *jpgBuf = nullptr;
uint32_t lastClockDraw = 0;

// ------------------------------------------------------------------
// JPEG 解碼（ROM TJpgDec）
// ------------------------------------------------------------------
struct JpgCtx {
  const uint8_t *data;
  size_t len;
  size_t pos;
  int offX, offY;          // 畫喺 viewport 入面嘅偏移（置中裁切）
};
static uint8_t tjWork[4096] __attribute__((aligned(4)));   // TJpgDec 工作記憶體
static uint16_t mcuBuf[16 * 16];

static UINT tjIn(JDEC *jd, BYTE *buf, UINT n) {
  JpgCtx *c = (JpgCtx *)jd->device;
  if (c->pos + n > c->len) n = c->len - c->pos;
  if (buf) memcpy(buf, c->data + c->pos, n);
  c->pos += n;
  return n;
}

static UINT tjOut(JDEC *jd, void *bitmap, JRECT *r) {
  JpgCtx *c = (JpgCtx *)jd->device;
  int w = r->right - r->left + 1;
  int h = r->bottom - r->top + 1;
  const uint8_t *p = (const uint8_t *)bitmap;   // RGB888
  for (int i = 0; i < w * h; i++, p += 3) {
    mcuBuf[i] = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
  }
  // viewport 會自動裁走出界部分 / viewport clips automatically
  tft.pushImage(c->offX + r->left, c->offY + r->top, w, h, mcuBuf);
  return 1;
}

// 解碼並畫入 (x,y,w,h) 方框，自動揀縮放 1, 1/2, 1/4, 1/8
bool drawJpegFit(const uint8_t *data, size_t len, int x, int y, int w, int h, char *err) {
  JDEC jd;
  JpgCtx ctx = { data, len, 0, 0, 0 };
  JRESULT res = jd_prepare(&jd, tjIn, tjWork, sizeof(tjWork), &ctx);
  if (res != JDR_OK) {
    snprintf(err, 16, "JPG ERR %d", (int)res);
    return false;
  }
  uint8_t scale = 0;   // 揀令闊度啱啱夠填滿格仔嘅最細縮放
  while (scale < 3 && (int)(jd.width >> (scale + 1)) >= w) scale++;
  int outW = jd.width >> scale;
  int outH = jd.height >> scale;
  ctx.offX = (w - outW) / 2;      // 太大就置中裁切，太細就置中
  ctx.offY = (h - outH) / 2;
  if (outW < w || outH < h) tft.fillRect(x, y, w, h, TFT_BLACK);
  tft.setViewport(x, y, w, h);
  res = jd_decomp(&jd, tjOut, scale);
  tft.resetViewport();
  if (res != JDR_OK) {
    snprintf(err, 16, "DEC ERR %d", (int)res);
    return false;
  }
  return true;
}

// ------------------------------------------------------------------
// HTTP 攞快照 / fetch one snapshot into jpgBuf
// ------------------------------------------------------------------
int fetchSnapshot(int cam, char *err) {
  HTTPClient http;
  http.useHTTP10(true);                 // 避免 chunked 傳送，易處理
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(CAMERAS[cam].url)) {
    strcpy(err, "BAD URL");
    return -1;
  }
  if (CAMERAS[cam].user[0]) http.setAuthorization(CAMERAS[cam].user, CAMERAS[cam].pass);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code == 401) strcpy(err, "AUTH 401");
    else if (code < 0) strcpy(err, "NO CONN");
    else snprintf(err, 16, "HTTP %d", code);
    http.end();
    return -1;
  }
  int total = http.getSize();          // -1 = 唔知大細
  if (total > MAX_JPEG_BYTES) {
    snprintf(err, 16, "BIG %dKB", (total / 1024) % 10000);
    http.end();
    return -1;
  }
  WiFiClient *s = http.getStreamPtr();
  int got = 0;
  uint32_t t0 = millis();
  while (http.connected() || s->available()) {
    int avail = s->available();
    if (avail > 0) {
      if (got + avail > MAX_JPEG_BYTES) {
        strcpy(err, "TOO BIG");
        http.end();
        return -1;
      }
      got += s->readBytes(jpgBuf + got, avail);
      if (total > 0 && got >= total) break;
      t0 = millis();
    } else {
      if (millis() - t0 > HTTP_TIMEOUT_MS) break;
      delay(1);
    }
  }
  http.end();
  if (got < 4 || jpgBuf[0] != 0xFF || jpgBuf[1] != 0xD8) {
    strcpy(err, "NOT JPEG");
    return -1;
  }
  return got;
}

// ------------------------------------------------------------------
// 畫面元素
// ------------------------------------------------------------------
void tileRect(int s, int &x, int &y, int &w, int &h) {
  if (fullCam >= 0) { x = 0; y = 0; w = SCR_W; h = VIEW_H; return; }
  x = (s % 2) * TILE_W;
  y = (s / 2) * TILE_H;
  w = TILE_W;
  h = TILE_H;
}

void drawLabel(int cam, int x, int y, int w, int h, bool live) {
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
  int tw = tft.textWidth(CAMERAS[cam].name) + 8;
  tft.fillRect(x + 2, y + 2, tw, 16, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(CAMERAS[cam].name, x + 6, y + 2);
  // 狀態點：綠 = 正常，紅 = 斷線，黃 = 測試畫面
  uint16_t dot = live ? TFT_GREEN : (camState[cam].everOk ? TFT_RED : TFT_YELLOW);
  tft.fillCircle(x + tw + 8, y + 10, 4, dot);
  // 格仔邊框
  tft.drawRect(x, y, w, h, TFT_DARKGREY);
}

// 冇鏡頭時嘅動畫測試畫面 / animated placeholder
void drawDemoTile(int cam, int x, int y, int w, int h, const char *msg) {
  uint32_t t = millis() / 100;
  uint16_t sky = tft.color565(20 + (cam * 37) % 60, 40 + (cam * 53) % 80, 90 + (cam * 29) % 100);
  int horizon = y + h * 3 / 5;
  tft.fillRect(x, y, w, horizon - y, sky);
  tft.fillRect(x, horizon, w, y + h - horizon, tft.color565(40, 70, 40));
  // 移動嘅「車」/ moving "car"
  int carW = w / 6, carH = h / 8;
  int cx = x + (int)((t * (3 + cam)) % (w + carW)) - carW;
  int cy = horizon + (y + h - horizon) / 3;
  tft.setViewport(x, y, w, h, false);
  tft.fillRoundRect(cx, cy, carW, carH, 3, TFT_ORANGE);
  tft.fillCircle(cx + carW / 4, cy + carH, 3, TFT_BLACK);
  tft.fillCircle(cx + carW * 3 / 4, cy + carH, 3, TFT_BLACK);
  // 掃描線 / scan line
  int sy = y + (int)((t * 4) % h);
  tft.drawFastHLine(x, sy, w, TFT_WHITE);
  tft.resetViewport();
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(msg, x + w / 2, y + h - 12);
}

void drawButton(int x, const char *txt) {
  tft.fillRoundRect(x + 2, VIEW_H + 3, BTN_W - 4, BAR_H - 6, 4, TFT_NAVY);
  tft.drawRoundRect(x + 2, VIEW_H + 3, BTN_W - 4, BAR_H - 6, 4, TFT_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString(txt, x + BTN_W / 2, VIEW_H + BAR_H / 2);
}

void drawBarCenter() {
  char buf[40];
  if (timeOk) {
    struct tm ti;
    getLocalTime(&ti, 10);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
  } else {
    unsigned long s = millis() / 1000;
    snprintf(buf, sizeof(buf), "%s  up %02lu:%02lu:%02lu", wifiOk ? "NO NTP" : "DEMO", s / 3600, (s / 60) % 60, s % 60);
  }
  tft.fillRect(BTN_W, VIEW_H, SCR_W - 2 * BTN_W, BAR_H, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(buf, SCR_W / 2, VIEW_H + 8);
  char info[32];
  if (fullCam >= 0) snprintf(info, sizeof(info), "%s  (%d/%d)", CAMERAS[fullCam].name, fullCam + 1, NUM_CAMS);
  else snprintf(info, sizeof(info), "PAGE %d/%d", page + 1, NUM_PAGES);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(info, SCR_W / 2, VIEW_H + 18);
}

void drawBar() {
  tft.fillRect(0, VIEW_H, SCR_W, BAR_H, TFT_BLACK);
  drawButton(0, fullCam >= 0 ? "< CAM" : "< PAGE");
  drawButton(SCR_W - BTN_W, fullCam >= 0 ? "CAM >" : "PAGE >");
  drawBarCenter();
}

void drawLayout() {
  tft.fillRect(0, 0, SCR_W, VIEW_H, TFT_BLACK);
  if (fullCam < 0) {
    for (int s = 0; s < 4; s++) {
      int cam = page * 4 + s;
      int x, y, w, h;
      tileRect(s, x, y, w, h);
      if (cam < NUM_CAMS) {
        drawDemoTile(cam, x, y, w, h, "LOADING...");
        drawLabel(cam, x, y, w, h, false);
      } else {
        tft.drawRect(x, y, w, h, TFT_DARKGREY);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("EMPTY", x + w / 2, y + h / 2, 2);
      }
    }
  } else {
    drawDemoTile(fullCam, 0, 0, SCR_W, VIEW_H, "LOADING...");
    drawLabel(fullCam, 0, 0, SCR_W, VIEW_H, false);
  }
  drawBar();
  slot = 0;
}

// 更新一格 / refresh one tile
void updateCam(int cam, int s) {
  int x, y, w, h;
  tileRect(s, x, y, w, h);
  CamState &st = camState[cam];
  bool live = false;
  bool offlineWait = (st.fails >= FAILS_BEFORE_DEMO) && (millis() - st.lastTryMs < RETRY_OFFLINE_MS);
  if (!DEMO_MODE && wifiOk && jpgBuf && !offlineWait) {
    st.lastTryMs = millis();
    char err[16] = "";
    int n = fetchSnapshot(cam, err);
    if (n > 0 && drawJpegFit(jpgBuf, n, x, y, w, h, err)) {
      st.fails = 0;
      st.everOk = true;
      st.lastOkMs = millis();
      live = true;
    } else {
      if (st.fails < 255) st.fails++;
      strncpy(st.lastErr, err, sizeof(st.lastErr) - 1);
      Serial.printf("[%s] %s  (%s)\n", CAMERAS[cam].name, err, CAMERAS[cam].url);
    }
  }
  if (!live) {
    if (DEMO_MODE || !wifiOk) {
      drawDemoTile(cam, x, y, w, h, "DEMO");
    } else if (!st.everOk || st.fails >= FAILS_BEFORE_DEMO) {
      // 保留舊相，除非從來未成功過 / keep the last frame if we had one
      if (!st.everOk) drawDemoTile(cam, x, y, w, h, st.lastErr);
      else {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawString(" OFFLINE ", x + w / 2, y + h - 12, 2);
      }
    }
  }
  drawLabel(cam, x, y, w, h, live);
}

// ------------------------------------------------------------------
// 觸控 / touch
// ------------------------------------------------------------------
bool getTouch(int &tx, int &ty) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  tx = constrain(map(p.x, TS_MINX, TS_MAXX, 0, SCR_W), 0, SCR_W - 1);
  ty = constrain(map(p.y, TS_MINY, TS_MAXY, 0, SCR_H), 0, SCR_H - 1);
  Serial.printf("touch raw x=%d y=%d -> %d,%d\n", p.x, p.y, tx, ty);
  return true;
}

void handleTouch() {
  int tx, ty;
  if (!getTouch(tx, ty)) return;
  while (ts.touched()) delay(10);   // 等放手 / wait for release
  if (ty >= VIEW_H) {                // 底部控制列
    if (tx < BTN_W) {
      if (fullCam >= 0) fullCam = (fullCam + NUM_CAMS - 1) % NUM_CAMS;
      else page = (page + NUM_PAGES - 1) % NUM_PAGES;
      drawLayout();
    } else if (tx >= SCR_W - BTN_W) {
      if (fullCam >= 0) fullCam = (fullCam + 1) % NUM_CAMS;
      else page = (page + 1) % NUM_PAGES;
      drawLayout();
    }
    return;
  }
  if (fullCam >= 0) {                // 全屏 → 返 2x2
    page = fullCam / 4;
    fullCam = -1;
    drawLayout();
    return;
  }
  int s = (ty / TILE_H) * 2 + (tx / TILE_W);
  int cam = page * 4 + s;
  if (cam < NUM_CAMS) {              // 2x2 → 全屏
    fullCam = cam;
    drawLayout();
  }
}

// ------------------------------------------------------------------
void connectWiFi() {
  if (DEMO_MODE) return;
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", SCR_W / 2, SCR_H / 2, 4);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  wifiOk = (WiFi.status() == WL_CONNECTED);
  if (wifiOk) {
    Serial.printf("WiFi OK, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
    struct tm ti;
    timeOk = getLocalTime(&ti, 5000);
  } else {
    Serial.println("WiFi FAILED -> DEMO tiles");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi failed - DEMO", SCR_W / 2, SCR_H / 2, 4);
    delay(1200);
  }
}

void setup() {
  Serial.begin(115200);
  // RGB LED 熄（active-low）
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);

  tft.init();
  tft.setRotation(1);               // 橫向 320x240
  tft.setSwapBytes(true);           // pushImage 用
  tft.fillScreen(TFT_BLACK);
  pinMode(BL_PIN, OUTPUT);
  analogWrite(BL_PIN, SCREEN_BRIGHT);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  jpgBuf = (uint8_t *)malloc(MAX_JPEG_BYTES);
  if (!jpgBuf) Serial.println("malloc jpgBuf failed! -> reduce MAX_JPEG_BYTES");
  memset(camState, 0, sizeof(camState));
  for (int i = 0; i < NUM_CAMS; i++) strcpy(camState[i].lastErr, "WAITING");

  connectWiFi();
  Serial.printf("Cameras: %d, pages: %d, free heap: %u\n", NUM_CAMS, NUM_PAGES, ESP.getFreeHeap());
  drawLayout();
}

void loop() {
  handleTouch();

  // 每秒更新時間 / clock once per second
  if (millis() - lastClockDraw >= 1000) {
    lastClockDraw = millis();
    if (!timeOk && wifiOk) {
      struct tm ti;
      timeOk = getLocalTime(&ti, 0);
    }
    drawBarCenter();
  }

  // WiFi 斷咗就試重連 / reconnect if dropped
  static uint32_t lastWifiCheck = 0;
  if (!DEMO_MODE && millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    bool now = (WiFi.status() == WL_CONNECTED);
    if (!now) WiFi.reconnect();
    if (now != wifiOk) Serial.printf("WiFi %s\n", now ? "back" : "lost");
    wifiOk = now;
  }

  // 輪流更新一格 / refresh tiles round-robin
  if (fullCam >= 0) {
    updateCam(fullCam, 0);
  } else {
    int cam = page * 4 + slot;
    if (cam < NUM_CAMS) updateCam(cam, slot);
    slot = (slot + 1) % 4;
  }
  delay((DEMO_MODE || !wifiOk) ? 60 : REFRESH_GAP_MS);
}
