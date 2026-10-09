/*
  01_GPS超速警告  (CYD ESP32-2432S028R)
  ------------------------------------------------------------
  靈感來自 CYD Facebook 群組 Tooraj Enayati 嘅 GPS 超速警告作品：
  https://www.facebook.com/groups/710623908028293/posts/1099562032467810/
  呢份係學造工作室「AI 補完」自行重寫嘅簡化版本，唔係原作者嘅 code。

  功能：
   - NEO-6M GPS 讀取速度 (km/h)，大字車速錶顯示
   - 由 SD 卡讀取 /zones.csv（限速區：lat,lon,radius_m,limit,name）
   - 超速時：畫面閃紅 + RGB LED 閃紅；正常時 LED 綠色
   - 全離線運作，唔需要 WiFi

  接線（CYD CN1 擴展插座：GND / GPIO22 / GPIO27 / 3.3V）
   GPS VCC -> 3.3V
   GPS GND -> GND
   GPS TX  -> GPIO27  (ESP32 RX)
   GPS RX  -> GPIO22  (ESP32 TX，可以唔接)

  需要嘅 library：TFT_eSPI（已設定 CYD）、TinyGPSPlus
  Board：ESP32 Dev Module
*/

#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>

// ===== 可以改嘅設定 =====
#define GPS_RX_PIN   27      // 接 GPS 模組 TX
#define GPS_TX_PIN   22      // 接 GPS 模組 RX
#define GPS_BAUD     9600
#define DEFAULT_LIMIT 50     // 唔喺任何限速區時嘅預設限速 (km/h)，香港市區一般 50
#define OVER_MARGIN   3      // 超過限速幾多 km/h 先警告（避免 GPS 跳動誤報）
#define MAX_ZONES     200

// CYD 內置硬件
#define SD_CS   5
#define LED_R   4
#define LED_G  16
#define LED_B  17           // RGB LED：LOW = 亮

TFT_eSPI tft;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
SPIClass sdSPI(VSPI);        // SD 卡用 VSPI：SCK18 MISO19 MOSI23 CS5

struct Zone {
  double lat, lon;
  float radius;
  int limit;
  char name[24];
};
Zone zones[MAX_ZONES];
int zoneCount = 0;
bool sdOK = false;

// ---------- 工具 ----------
void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// 兩點之間距離（米），Haversine 公式
double distanceM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(lat1)) * cos(radians(lat2)) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

// 讀 SD 卡 /zones.csv
void loadZones() {
  sdSPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("SD 卡讀取失敗，只用預設限速");
    return;
  }
  sdOK = true;
  File f = SD.open("/zones.csv");
  if (!f) {
    Serial.println("搵唔到 /zones.csv");
    return;
  }
  while (f.available() && zoneCount < MAX_ZONES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#' || line.startsWith("lat")) continue;
    // 格式：lat,lon,radius_m,limit,name
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    int c4 = line.indexOf(',', c3 + 1);
    if (c1 < 0 || c2 < 0 || c3 < 0) continue;
    Zone &z = zones[zoneCount];
    z.lat = line.substring(0, c1).toDouble();
    z.lon = line.substring(c1 + 1, c2).toDouble();
    z.radius = line.substring(c2 + 1, c3).toFloat();
    z.limit = (c4 > 0 ? line.substring(c3 + 1, c4) : line.substring(c3 + 1)).toInt();
    String nm = (c4 > 0) ? line.substring(c4 + 1) : String("Zone");
    strncpy(z.name, nm.c_str(), sizeof(z.name) - 1);
    z.name[sizeof(z.name) - 1] = 0;
    zoneCount++;
  }
  f.close();
  Serial.printf("載入咗 %d 個限速區\n", zoneCount);
}

// 搵目前位置嘅限速：喺多個區入面就揀最近中心嗰個
int currentLimit(double lat, double lon, const char **zoneName) {
  int best = -1;
  double bestD = 1e12;
  for (int i = 0; i < zoneCount; i++) {
    double d = distanceM(lat, lon, zones[i].lat, zones[i].lon);
    if (d <= zones[i].radius && d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best >= 0) {
    *zoneName = zones[best].name;
    return zones[best].limit;
  }
  *zoneName = "Default";
  return DEFAULT_LIMIT;
}

// ---------- 畫面 ----------
void drawStatic() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("GPS Speed Warning", 8, 6, 2);
  tft.drawString("km/h", 230, 150, 4);
}

void drawLimitSign(int limit) {
  // 限速牌：白底紅圈
  int cx = 270, cy = 70, r = 38;
  tft.fillCircle(cx, cy, r, TFT_RED);
  tft.fillCircle(cx, cy, r - 7, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawNumber(limit, cx, cy + 2, 4);
}

void drawSpeed(int speed, uint16_t bg) {
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextPadding(tft.textWidth("888", 8));
  tft.drawNumber(speed, 220, 120, 8);   // Font 8 = 75px 大數字
  tft.setTextPadding(0);
}

void drawStatus(const char *zoneName, int sats, bool fix) {
  tft.fillRect(0, 200, 320, 40, TFT_NAVY);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  char buf[64];
  if (fix) snprintf(buf, sizeof(buf), "Zone: %s", zoneName);
  else     snprintf(buf, sizeof(buf), "Searching GPS...");
  tft.drawString(buf, 6, 204, 2);
  snprintf(buf, sizeof(buf), "Sat:%d  SD:%s  Zones:%d", sats, sdOK ? "OK" : "--", zoneCount);
  tft.drawString(buf, 6, 222, 2);
}

// ---------- 主程式 ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, true);   // 藍色 = 開機中

  tft.init();
  tft.setRotation(1);           // 橫向 320x240
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);       // 背光

  drawStatic();
  loadZones();

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  drawLimitSign(DEFAULT_LIMIT);
  drawSpeed(0, TFT_BLACK);
  drawStatus("-", 0, false);
}

int lastSpeed = -1, lastLimit = -1;
bool warning = false, flashOn = false;
unsigned long lastUi = 0, lastFlash = 0;

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  unsigned long now = millis();
  if (now - lastUi >= 500) {
    lastUi = now;
    bool fix = gps.location.isValid() && gps.location.age() < 3000;
    int speed = (fix && gps.speed.isValid()) ? (int)round(gps.speed.kmph()) : 0;
    if (speed < 2) speed = 0;   // 停車時 GPS 會飄，當 0
    const char *zoneName = "-";
    int limit = fix ? currentLimit(gps.location.lat(), gps.location.lng(), &zoneName) : DEFAULT_LIMIT;

    if (limit != lastLimit) { drawLimitSign(limit); lastLimit = limit; }
    warning = fix && speed > limit + OVER_MARGIN;
    if (!warning) {
      if (speed != lastSpeed || flashOn) {
        if (flashOn) { drawStatic(); drawLimitSign(limit); flashOn = false; }
        drawSpeed(speed, TFT_BLACK);
      }
      setLed(false, fix, !fix);   // 綠 = 正常，藍 = 未定位
    }
    lastSpeed = speed;
    drawStatus(zoneName, gps.satellites.isValid() ? gps.satellites.value() : 0, fix);
  }

  // 超速：每 300ms 閃一次紅
  if (warning && now - lastFlash >= 300) {
    lastFlash = now;
    flashOn = !flashOn;
    uint16_t bg = flashOn ? TFT_RED : TFT_BLACK;
    tft.fillRect(0, 26, 225, 172, bg);
    drawSpeed(lastSpeed, bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_YELLOW, bg);
    tft.drawString("SLOW DOWN!", 20, 170, 4);
    setLed(flashOn, false, false);
  }
}
