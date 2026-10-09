// =====================================================================
//  CYD（ESP32-2432S028R）九巴到站屏  v2
//  預設：九巴 271  九龍清真寺站 → 往 大埔(富亨)
//  需要：esp32 開發板 2.0.17 + libraries_v2 zip（TFT_eSPI + ArduinoJson）
// =====================================================================

// ===================== ① 你要改嘅設定（只改呢度）=====================
const char* WIFI_SSID     = "YOUR_WIFI_NAME";      // ← 改做你嘅 WiFi 名（要 2.4G）
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";  // ← 改做你嘅 WiFi 密碼

const char* ROUTE      = "271";          // 路線號碼
const char* DIRECTION  = "I";            // O = 去程(outbound)、I = 回程(inbound)
const int   STOP_SEQ   = 7;              // 呢個方向第幾個站（271 回程第 7 站 = 九龍清真寺）
const char* SERVICE_TYPE = "1";          // 服務類型，一般係 1

const char* TITLE_TEXT = "KMB 271";              // 頂部大字
const char* DEST_TEXT  = "TO: FU HENG";          // 頂部右邊（目的地）
const char* STOP_TEXT  = "STOP: KOWLOON MOSQUE"; // 第二行（車站名）
// 點樣搵站序（seq）？用瀏覽器開：
//   https://data.etabus.gov.hk/v1/transport/kmb/route-stop/271/inbound/1
//   （outbound = 去程、inbound = 回程），再用每個 stop ID 開：
//   https://data.etabus.gov.hk/v1/transport/kmb/stop/<stop ID>  睇站名
// =====================================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <time.h>

// ---------- 硬件（CYD）----------
#define LCD_BACKLIGHT_PIN 21   // 背光腳

// ---------- 顏色（RGB565）----------
#define COLOR_KMB_RED    0xB800  // 九巴紅
#define COLOR_KMB_AMBER  0xFE20  // 琥珀色
#define COLOR_BG_DARK    0x10A2  // 深灰背景
#define COLOR_WHITE      0xFFFF
#define COLOR_MUTED_GRAY 0x7BEF

// ---------- 時間設定 ----------
const unsigned long FETCH_INTERVAL_MS = 30000;  // 每 30 秒拎一次到站時間（唔好再密）
const unsigned long WIFI_TIMEOUT_MS   = 20000;  // WiFi 等 20 秒都連唔到就出提示
const unsigned long NTP_TIMEOUT_MS    = 10000;  // 對時等 10 秒都唔得就出提示
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 8 * 3600;          // 香港 UTC+8

TFT_eSPI tft = TFT_eSPI();
unsigned long lastFetchTime = 0;
bool timeReady = false;      // NTP 對時成功未？
String apiUrl;               // 由上面設定砌出嚟嘅網址

// ---------------------------------------------------------------------
// 喺中間位置顯示一至兩行訊息（例如「連緊 WiFi」、出錯提示）
// ---------------------------------------------------------------------
void showMessage(const char* line1, const char* line2 = "", uint16_t color = COLOR_KMB_AMBER) {
  tft.fillRect(0, 68, 320, 150, COLOR_BG_DARK);
  tft.setTextColor(color, COLOR_BG_DARK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(line1, 160, 125, 4);
  if (line2[0] != '\0') {
    tft.setTextColor(COLOR_MUTED_GRAY, COLOR_BG_DARK);
    tft.drawString(line2, 160, 160, 2);
  }
}

// 底部狀態列
void showFooter(const char* text, uint16_t bg = 0x0841) {
  tft.fillRect(0, 222, 320, 18, bg);
  tft.setTextColor(bg == 0x0841 ? COLOR_MUTED_GRAY : COLOR_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(text, 160, 231, 1);
}

// 頂部：路線、目的地、車站名
void drawHeader() {
  tft.fillRect(0, 0, 320, 42, COLOR_KMB_RED);
  tft.setTextColor(COLOR_WHITE, COLOR_KMB_RED);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(TITLE_TEXT, 10, 21, 4);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(DEST_TEXT, 310, 21, 2);

  tft.fillRect(0, 42, 320, 24, 0x2104);
  tft.setTextColor(COLOR_MUTED_GRAY, 0x2104);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(STOP_TEXT, 160, 54, 2);
}

// ---------------------------------------------------------------------
// 連 WiFi：最多等 WIFI_TIMEOUT_MS；失敗會喺螢幕講原因，之後再試
// ---------------------------------------------------------------------
void connectWiFi() {
  // 未填 WiFi？直接提示
  if (strcmp(WIFI_SSID, "YOUR_WIFI_NAME") == 0) {
    showMessage("Please set Wi-Fi!", "Edit WIFI_SSID / WIFI_PASSWORD (line 8-9)", COLOR_KMB_RED);
    Serial.println("[WiFi] 你未改 WiFi 名同密碼（第 8-9 行）");
    while (true) delay(1000);   // 停喺度，等你改好再上傳
  }

  int attempt = 1;
  while (WiFi.status() != WL_CONNECTED) {
    char buf[40];
    snprintf(buf, sizeof(buf), "Attempt %d  (SSID: %.16s)", attempt, WIFI_SSID);
    showMessage("Connecting to Wi-Fi...", buf);
    Serial.printf("[WiFi] 第 %d 次嘗試連接 %s\n", attempt, WIFI_SSID);

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED) {
      // 連唔到：多數係名／密碼錯，或者係 5G WiFi
      Serial.println("\n[WiFi] 連唔到！檢查 WiFi 名、密碼（大細楷）、要 2.4G");
      showMessage("Wi-Fi failed!", "Check name / password / must be 2.4GHz", COLOR_KMB_RED);
      delay(5000);   // 俾 5 秒你睇清楚，之後再試
      attempt++;
    }
  }
  Serial.print("\n[WiFi] Connected! IP: ");
  Serial.println(WiFi.localIP());
}

// ---------------------------------------------------------------------
// NTP 對時：最多等 NTP_TIMEOUT_MS；失敗都照樣顯示到站時間（冇倒數）
// ---------------------------------------------------------------------
void syncTime() {
  showMessage("Syncing Time (NTP)...");
  configTime(GMT_OFFSET_SEC, 0, NTP_SERVER);

  struct tm timeinfo;
  unsigned long start = millis();
  while (millis() - start < NTP_TIMEOUT_MS) {
    if (getLocalTime(&timeinfo, 500)) {
      timeReady = true;
      Serial.println("[NTP] Ready!");
      return;
    }
    Serial.print("T");
  }
  timeReady = false;
  Serial.println("\n[NTP] 對時失敗（可能網絡封咗 NTP），會照顯示班次時間，但冇「幾多分鐘」");
  showMessage("NTP failed", "Showing bus times only, will retry", COLOR_KMB_RED);
  delay(3000);
}

// ---------------------------------------------------------------------
// 拆解 JSON，搵出指定方向 + 站序嘅班次，畫上螢幕
// ---------------------------------------------------------------------
void parseAndDisplayETA(Stream& stream) {
  // Filter：只保留我哋要嘅欄位，慳記憶體
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument filter;
  JsonDocument doc;
#else
  StaticJsonDocument<256> filter;
  DynamicJsonDocument doc(16384);
#endif
  filter["data"][0]["dir"] = true;
  filter["data"][0]["seq"] = true;
  filter["data"][0]["eta"] = true;
  filter["data"][0]["dest_en"] = true;

  DeserializationError error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("[JSON] 拆解失敗: %s\n", error.c_str());
    showFooter("Data error - will retry in 30s", COLOR_KMB_RED);
    return;
  }

  JsonArray data = doc["data"].as<JsonArray>();
  Serial.printf("[KMB] 收到 %d 項資料\n", data.size());

  tft.fillRect(0, 68, 320, 154, COLOR_BG_DARK);   // 清走舊畫面

  time_t nowSec;
  time(&nowSec);

  int slotCount = 0;
  int yPositions[3] = {88, 138, 188};

  for (JsonObject item : data) {
    const char* dir = item["dir"] | "";
    int seq = item["seq"] | -1;

    // 只要「指定方向」+「指定站序」
    if (strcmp(dir, DIRECTION) != 0 || seq != STOP_SEQ) continue;

    const char* etaStr = item["eta"] | "";      // 例如 2026-10-10T14:32:00+08:00
    if (strlen(etaStr) < 19) continue;          // 冇時間（例如尾班車已過）就跳過

    Serial.printf("[KMB] 第 %d 站 往 %s  ETA %s\n", seq, (const char*)(item["dest_en"] | ""), etaStr);

    // 拆時間字串
    struct tm tm_eta = {};
    sscanf(etaStr, "%4d-%2d-%2dT%2d:%2d:%2d",
           &tm_eta.tm_year, &tm_eta.tm_mon, &tm_eta.tm_mday,
           &tm_eta.tm_hour, &tm_eta.tm_min, &tm_eta.tm_sec);

    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tm_eta.tm_hour, tm_eta.tm_min);

    // 倒數分鐘：要 NTP 對時成功先計得準
    char countdownBuf[16];
    if (timeReady) {
      tm_eta.tm_year -= 1900;
      tm_eta.tm_mon  -= 1;
      tm_eta.tm_isdst = 0;
      long diffSec = (long)(mktime(&tm_eta) - nowSec);
      int diffMin = (diffSec + 30) / 60;
      if (diffMin <= 0) snprintf(countdownBuf, sizeof(countdownBuf), "Arriving");
      else              snprintf(countdownBuf, sizeof(countdownBuf), "%d min", diffMin);
    } else {
      snprintf(countdownBuf, sizeof(countdownBuf), "-- min");
    }

    // 畫一格
    int y = yPositions[slotCount];
    tft.fillRoundRect(8, y - 14, 304, 40, 4, 0x18C3);

    tft.setTextColor(COLOR_MUTED_GRAY, 0x18C3);
    tft.setTextDatum(ML_DATUM);
    char badgeBuf[4];
    snprintf(badgeBuf, sizeof(badgeBuf), "#%d", slotCount + 1);
    tft.drawString(badgeBuf, 16, y + 6, 2);

    tft.setTextColor(COLOR_WHITE, 0x18C3);
    tft.drawString(timeBuf, 50, y + 6, 4);

    tft.setTextColor(COLOR_KMB_AMBER, 0x18C3);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(countdownBuf, 300, y + 6, 4);

    slotCount++;
    if (slotCount >= 3) break;   // 最多顯示 3 班
  }

  if (slotCount == 0) {
    tft.setTextColor(COLOR_MUTED_GRAY, COLOR_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No Scheduled Bus", 160, 130, 4);
    tft.drawString("(check DIRECTION / STOP_SEQ)", 160, 165, 2);
    Serial.println("[KMB] 搵唔到班次：可能真係冇車（深夜），或者 DIRECTION / STOP_SEQ 唔啱");
  }

  // 底部狀態列：顯示更新時間
  char statusBuf[48];
  struct tm timeinfo;
  if (timeReady && getLocalTime(&timeinfo, 100)) {
    snprintf(statusBuf, sizeof(statusBuf), "Updated %02d:%02d:%02d * 30s Auto Refresh",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    snprintf(statusBuf, sizeof(statusBuf), "No clock (NTP) * 30s Auto Refresh");
  }
  showFooter(statusBuf);
}

// ---------------------------------------------------------------------
// 向九巴 API 拎資料（HTTPS）
// ---------------------------------------------------------------------
void fetchKMBETA() {
  if (WiFi.status() != WL_CONNECTED) {
    showFooter("Wi-Fi Disconnected! Reconnecting...", COLOR_KMB_RED);
    WiFi.reconnect();
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();   // 初學版：唔檢查證書

  HTTPClient http;
  Serial.printf("[HTTP] 拎緊: %s\n", apiUrl.c_str());
  if (http.begin(client, apiUrl)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      parseAndDisplayETA(*http.getStreamPtr());
    } else {
      Serial.printf("[HTTP] 失敗, code: %d\n", httpCode);
      showFooter("Network error - will retry in 30s", COLOR_KMB_RED);
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);

  // 開背光
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);           // 橫向 320x240
  tft.fillScreen(COLOR_BG_DARK);
  drawHeader();

  // 砌 API 網址，例如 .../route-eta/271/1
  apiUrl = String("https://data.etabus.gov.hk/v1/transport/kmb/route-eta/") + ROUTE + "/" + SERVICE_TYPE;

  connectWiFi();   // ② 連 WiFi（有 timeout 同提示）
  syncTime();      // ③ 對時（失敗都照行）
  fetchKMBETA();   // ④ 第一次拎到站時間
  lastFetchTime = millis();
}

void loop() {
  if (millis() - lastFetchTime >= FETCH_INTERVAL_MS) {
    lastFetchTime = millis();

    // 之前對時失敗？每次順便再試一下（唔會卡住）
    if (!timeReady) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 100)) {
        timeReady = true;
        Serial.println("[NTP] 遲咗但終於對到時！");
      }
    }
    fetchKMBETA();
  }
}
