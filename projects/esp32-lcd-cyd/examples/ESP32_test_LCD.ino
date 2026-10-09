#include <WiFi.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// CYD 屏幕背光控制 pin 通常係 GPIO 21
#define TFT_BL 21

TFT_eSPI tft = TFT_eSPI();

void scanAndDisplayWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("Scanning WiFi...");

  int n = WiFi.scanNetworks();
  
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("WiFi Networks Found:");
  
  tft.setTextSize(1);
  tft.setCursor(10, 35);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (n == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("No networks found.");
  } else {
    // 屏幕高度有限，最多顯示前 12 個 AP 免得爆畫面
    int displayLimit = (n > 12) ? 12 : n;
    for (int i = 0; i < displayLimit; ++i) {
      tft.printf("%2d: %-18.18s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
    if (n > 12) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.printf("\n...plus %d more networks", n - 12);
    }
  }

  // 掃描完清理記憶體
  WiFi.scanDelete();
}

void setup() {
  Serial.begin(115200);

  // 1. 點著背光
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // 2. 初始化屏幕
  tft.init();
  tft.setRotation(1); // 橫屏模式 (0-3 視乎你習慣方向)

  // 3. 顏色切換與文字測試
  // 紅色
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(60, 100);
  tft.println("COLOR: RED");
  delay(1000);

  // 綠色
  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(45, 100);
  tft.println("COLOR: GREEN");
  delay(1000);

  // 藍色
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(55, 100);
  tft.println("COLOR: BLUE");
  delay(1000);

  // 4. 設定 WiFi 模式為 Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);

  // 進行初次掃描
  scanAndDisplayWiFi();
}

void loop() {
  // 每 10 秒重新掃描一次更新列表
  delay(10000);
  scanAndDisplayWiFi();
}