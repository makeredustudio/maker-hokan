# 05 飛行追蹤地圖（香港雷達）✈️🗺️

> ⚠️ **誠實聲明：** 原作者冇公開 code。呢份係學造工作室「AI 補完」參考原帖意念、喺 CYD 上面**自己重新寫**嘅版本，地圖改咗以**香港國際機場**為中心，預設用**模擬飛機**（唔使 WiFi 都玩得），**唔係原作者嘅程式**。只係 compile 測試通過，未喺實機測試。

## 📌 原帖講咩
- **作者：** Nick Conrad（私人群組「CYD - Cheap Yellow Display ESP32」）
- **原帖：** https://www.facebook.com/groups/8104937882859357/posts/29194810513445454/
- **內容：** 「My most recent CYD project」— 一部放喺亞加力外殼入面嘅 CYD，顯示美國亞利桑那州一帶嘅即時飛行地圖：藍色飛機符號連呼號標籤、距離圈、航道線，底部顯示「34 aircraft」同 WiFi 狀態（見 `FB_screenshot_Nick_Conrad.png`）。

## ✨ 我哋嘅 AI 補完版做咩
- 以 **HKIA** 為中心嘅雷達地圖：簡化海岸線（大嶼山、港島、九龍新界、深圳、澳門）、港珠澳大橋、三條跑道、**5 / 10 / 20 海里距離圈**
- **模擬模式（預設）**：自動產生 **到港（綠）**、**離港（青）**、**飛越（黃）** 飛機——到港會飛去最後進近點再沿 3° 滑翔道降落 25 號跑道；離港會爬升再轉彎離開；飛越嘅喺 FL310–FL390 巡航
- 每架飛機有**三角形符號（指住航向）**、**飛行軌跡點**、標籤（呼號 + 高度百呎 + 地速），例如 `CPA123` / `045 210`
- **撳飛機 = 詳細資料**：航空公司、機型、航線、到港/離港、高度、地速、航向、爬升率、同機場距離；撳空白位置取消
- 右上角 **+ / −** 三級放大縮細；全屏 8-bit sprite，畫面唔閃
- `SIM_SPEED` 可以令模擬快幾倍（預設 3 倍），課堂示範更好睇

## 🧰 需要嘅硬件
- 淨係 CYD ESP32-2432S028R 一塊！（模擬模式唔使 WiFi）
- 進階真實模式：2.4GHz WiFi；或者自己整 ADS-B 接收器（RTL-SDR USB 手指 + 1090MHz 天線 + Raspberry Pi）

## 📚 Library 同 Arduino IDE 設定
- **Board：** `ESP32 Dev Module`（ESP32 core **2.0.17**）
- **Library：** Larry 嘅 `libraries.zip`（TFT_eSPI 已設定 CYD）+ **XPT2046_Touchscreen**
- 真實模式（`USE_LIVE 1`）先需要 **ArduinoJson 6**（libraries.zip 已有）
- 單一 `.ino`，冇 config.h

## 🛰️ 進階：改用真實 ADS-B 數據
飛機會不停廣播 **ADS-B** 訊號（1090MHz），入面有位置、高度、速度、呼號。改 `.ino` 最頂：
1. `#define USE_LIVE 1`
2. 填 `WIFI_SSID` / `WIFI_PASSWORD`
3. 揀數據來源 `LIVE_URL`：
   - **方法 A 公開 API（免 key）**：`https://api.airplanes.live/v2/point/22.308/113.918/60`（以 HKIA 為中心 60 海里）。請尊重對方：唔好少過 10 秒攞一次（`LIVE_INTERVAL_MS`），只供個人/教學用途，用前睇吓佢哋最新使用條款
   - **方法 B 自己嘅接收器**：RTL-SDR + `dump1090-fa` 或 `readsb`，網址例如 `http://192.168.1.50/dump1090-fa/data/aircraft.json`（全離線、最穩定，仲可以同學生講無線電原理）
4. 程式會用 ArduinoJson 嘅 filter 只抽需要嘅欄位（`flight`、`lat`、`lon`、`alt_baro`、`gs`、`track`、`baro_rate`、`t`），兩次更新之間用航向同速度估計位置
- 真實模式 compile 過（Flash 用 74%，因為要 WiFi + HTTPS）；未有實際連線測試，欄位名如有改動要對返 API 文件

## ✏️ 可以改嘅地方（`.ino` 最頂）
- `SIM_SPEED`、`SIM_PLANES`（幾多架飛機）、`MAX_PLANES`
- 顏色 `C_ARR` / `C_DEP` / `C_OVF`；`viewX` / `viewY` 改畫面中心；`ZOOMS[]` 改放大級數
- `AIRLINES[]` / `PORTS[]` 加你想要嘅航空公司同目的地
- 觸控撳唔準：Serial Monitor（115200）睇 `touch x= y=`，改 `TS_MINX` 等數值

## ⬆️ 點樣上載
1. 下載 `05_Flight_Radar_HK.zip` 解壓（folder 名同 .ino 名一樣）
2. Arduino IDE 打開 → Board `ESP32 Dev Module` → 選 Port → 上載（自動 reset）
3. 開機即刻見到香港地圖同模擬飛機 🎉

## 🤖 用 AI vibe coding 再改良
將 `.ino` 貼俾 AI，再講：
- 「加 METAR 天氣：顯示 VHHH 風向風速同跑道方向（07 / 25 自動轉）」
- 「飛機飛到 5 海里內 RGB LED 閃綠燈、喇叭叮一聲」
- 「長按飛機睇佢過去 10 分鐘嘅高度變化圖」
- 「用 SD 卡放一張真實地圖底圖」
- 「真實模式加篩選：淨係顯示國泰 CPA 或者 10000 呎以下嘅飛機」
