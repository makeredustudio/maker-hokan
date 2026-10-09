#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <time.h>

// ========== Wi-Fi Credentials ==========
const char* ssid     = "YOUR_WIFI_NAME";      // 填你的 WiFi 名（只支援 2.4GHz）
const char* password = "YOUR_WIFI_PASSWORD";  // 填你的 WiFi 密碼

// ========== Hardware Definitions (CYD) ==========
#define LCD_BACKLIGHT_PIN 21

// KMB Brand Color Theme (RGB565)
#define COLOR_KMB_RED    0xB800  // Deep KMB Crimson
#define COLOR_KMB_AMBER  0xFE20  // Iconic LED Matrix Amber
#define COLOR_BG_DARK    0x10A2  // Dark charcoal background
#define COLOR_WHITE      0xFFFF
#define COLOR_MUTED_GRAY 0x7BEF

// ========== KMB Route ETA API ==========
// 271 Outbound (To Fu Heng) Route ETA endpoint
const char* kmb_url = "https://data.etabus.gov.hk/v1/transport/kmb/route-eta/271/1";

TFT_eSPI tft = TFT_eSPI();

unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 30000;

// NTP Settings for HKT (UTC+8)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;

void drawHeader() {
  tft.fillRect(0, 0, 320, 42, COLOR_KMB_RED);
  tft.setTextColor(COLOR_WHITE, COLOR_KMB_RED);
  
  tft.setTextDatum(ML_DATUM);
  tft.drawString("KMB 271", 10, 21, 4);

  tft.setTextDatum(MR_DATUM);
  tft.drawString("TO: FU HENG", 310, 21, 2);

  // Sub-header for Stop name
  tft.fillRect(0, 42, 320, 24, 0x2104);
  tft.setTextColor(COLOR_MUTED_GRAY, 0x2104);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("STOP: KOWLOON MOSQUE", 160, 54, 2);
}

void parseAndDisplayETA(WiFiClient& stream) {
  // Use Filter to dramatically cut down memory usage (~5KB RAM only)
  StaticJsonDocument<256> filter;
  filter["data"][0]["stop"] = true;
  filter["data"][0]["dir"] = true;
  filter["data"][0]["seq"] = true;
  filter["data"][0]["eta"] = true;
  filter["data"][0]["dest_en"] = true;

  // Allocate 16KB for filtered data (sufficient for ~30 stops with 4 fields)
  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));

  if (error) {
    Serial.printf("[JSON] Stream Filter failed: %s\n", error.c_str());
    return;
  }

  JsonArray data = doc["data"].as<JsonArray>();
  Serial.printf("[KMB] Filtered items: %d\n", data.size());

  // Clear ETA display area
  tft.fillRect(0, 68, 320, 150, COLOR_BG_DARK);

  time_t nowSec;
  time(&nowSec);

  int slotCount = 0;
  int yPositions[3] = {88, 138, 188};

  // 271 to Fu Heng: Kowloon Mosque is typically Stop Seq 3 or 4 after West Kowloon Station
  // Check both known stop IDs or Seq for Kowloon Mosque
  for (JsonObject item : data) {
    const char* dir = item["dir"] | "";
    const char* stop = item["stop"] | "";
    int seq = item["seq"] | -1;

    // Match outbound direction (O)
    if (strcasecmp(dir, "O") == 0) {
      // Print out the first few stops to Serial so you can see the exact ID
      if (seq <= 5 && seq > 0) {
        Serial.printf("[Route Stop Debug] Seq: %d | Stop ID: %s | ETA: %s\n", 
                      seq, stop, item["eta"] | "N/A");
      }

      // Kowloon Mosque on Nathan Road is usually Stop sequence 3 (or match KO01/HA02)
      // If you see the exact ID in Serial monitor, you can lock it to: strcmp(stop, "...") == 0
      // 271 Seq 3 is Kowloon Mosque (弥敦道九龍清真寺)
      if (seq == 3 || strstr(stop, "KO01") != NULL || strstr(stop, "HA02") != NULL) {
        const char* etaStr = item["eta"] | "";

        if (etaStr != nullptr && strlen(etaStr) >= 19) {
          struct tm tm_eta;
          sscanf(etaStr, "%4d-%2d-%2dT%2d:%2d:%2d",
                 &tm_eta.tm_year, &tm_eta.tm_mon, &tm_eta.tm_mday,
                 &tm_eta.tm_hour, &tm_eta.tm_min, &tm_eta.tm_sec);
          tm_eta.tm_year -= 1900;
          tm_eta.tm_mon  -= 1;
          tm_eta.tm_isdst = 0;

          time_t etaSec = mktime(&tm_eta);
          long diffSec = etaSec - nowSec;
          int diffMin = (diffSec + 30) / 60;
          if (diffMin < 0) diffMin = 0;

          char timeBuf[8];
          snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tm_eta.tm_hour, tm_eta.tm_min);

          char countdownBuf[16];
          if (diffMin == 0) {
            snprintf(countdownBuf, sizeof(countdownBuf), "Due / Arr");
          } else {
            snprintf(countdownBuf, sizeof(countdownBuf), "%d min", diffMin);
          }

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
          if (slotCount >= 3) break;
        }
      }
    }
  }

  if (slotCount == 0) {
    tft.setTextColor(COLOR_MUTED_GRAY, COLOR_BG_DARK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No Scheduled Bus Available", 160, 140, 4);
  }

  // Footer status bar
  tft.fillRect(0, 222, 320, 18, 0x0841);
  tft.setTextColor(COLOR_MUTED_GRAY, 0x0841);
  tft.setTextDatum(MC_DATUM);

  char statusBuf[48];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    snprintf(statusBuf, sizeof(statusBuf), "Updated %02d:%02d:%02d * 30s Auto Refresh",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    snprintf(statusBuf, sizeof(statusBuf), "Updated just now * 30s Auto Refresh");
  }
  tft.drawString(statusBuf, 160, 231, 1);
}

void fetchKMBETA() {
  if (WiFi.status() != WL_CONNECTED) {
    tft.fillRect(0, 222, 320, 18, COLOR_KMB_RED);
    tft.setTextColor(COLOR_WHITE, COLOR_KMB_RED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Wi-Fi Disconnected!", 160, 231, 1);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  Serial.printf("[HTTP] Fetching route ETA: %s\n", kmb_url);
  if (http.begin(client, kmb_url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      parseAndDisplayETA(*stream);
    } else {
      Serial.printf("[HTTP] Failed, code: %d\n", httpCode);
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);

  // Turn ON Backlight
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG_DARK);

  drawHeader();

  tft.setTextColor(COLOR_KMB_AMBER, COLOR_BG_DARK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting to Wi-Fi...", 160, 130, 4);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected!");

  tft.fillRect(0, 68, 320, 150, COLOR_BG_DARK);
  tft.drawString("Syncing Time (NTP)...", 160, 130, 4);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    Serial.print("T");
  }
  Serial.println("\n[NTP] Ready!");

  fetchKMBETA();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastFetchTime >= fetchInterval) {
    lastFetchTime = currentMillis;
    fetchKMBETA();
  }
}