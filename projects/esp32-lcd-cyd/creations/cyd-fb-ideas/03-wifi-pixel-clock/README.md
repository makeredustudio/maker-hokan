# 03 WiFi 像素時鐘 ⏰🟥

> ⚠️ **誠實聲明：** 原作者係用 ESP8266 + 實體 LED 點陣整嘅時鐘，冇公開 code。呢份係學造工作室「AI 補完」喺 CYD 上面**自己重新寫**嘅版本，用屏幕模擬 LED 點陣，**唔係原作者嘅程式**。

## 📌 原帖講咩
- **作者：** Rohit Singh（CYD - Cheap Yellow Display - ESP32 群組）
- **原帖：** https://www.facebook.com/groups/710623908028293/posts/1100310049059675/
- **影片：** https://youtube.com/shorts/vdNNB9bn1NY
- **內容：** 「Building a Smart WiFi Pixel LED Clock」系列嘅最終集，用 ESP8266 整咗個 WiFi 對時、彩色像素 LED 時鐘，仲有遙控同 RGB 效果（見 `FB_screenshot_Rohit_Singh.png`）。

## ✨ 我哋嘅 AI 補完版做咩
- 連 WiFi 後用 **NTP** 自動對時，香港時間（`HKT-8`）
- 時間用 **LED 點陣大字**（5x7 字型、每粒「LED」都係一個圓點），熄咗嘅 LED 都暗暗哋見到，好有懷舊電子鐘味道
- 中間冒號每秒閃一下；底部 60 粒小 LED 做秒針；仲有日期同星期
- **撳一下屏幕 = 轉顏色主題**（紅、Matrix 綠、冰藍、琥珀、霓虹、黑白，共 6 款）
- 用 CYD 背面嘅 **光敏電阻 LDR（GPIO34）** 自動調光：房間暗咗，屏幕都會暗啲，夜晚唔刺眼

## 🧰 需要嘅硬件
- 淨係 CYD ESP32-2432S028R 一塊！（LDR 同觸控都係板上內置）
- 一個 2.4GHz WiFi

## 📚 Library 同 Arduino IDE 設定
- **Board：** `ESP32 Dev Module`
- **Library：** Larry 嘅 `libraries.zip`（TFT_eSPI 已設定 CYD）+ **XPT2046_Touchscreen**（by Paul Stoffregen）
- NTP / 時區用 ESP32 內置功能，唔使裝其他嘢

## ✏️ 要改嘅地方（`.ino` 最頂）
1. `WIFI_SSID` / `WIFI_PASSWORD`
2. `USE_24H`：`false` 就變 12 小時制
3. `AUTO_BRIGHTNESS`：唔想自動調光就改 `false`
4. `LDR_BRIGHT` / `LDR_DARK`：每塊板嘅 LDR 有少少唔同。打開 Serial Monitor 睇 `LDR=` 數值（光猛時數字細、黑暗時數字大），再填返你自己嘅數
5. `TZ_INFO`：其他時區例如日本 `"JST-9"`

## ⬆️ 點樣上載
1. 下載 `03_WiFi_Pixel_Clock.zip` 解壓，或者將 `.ino` 放入同名 folder
2. Arduino IDE 打開 → Board `ESP32 Dev Module` → 選 Port → 上載（自動 reset）
3. 見到「Connecting WiFi...」→「Syncing time...」→ 時鐘出現 🎉
4. 一直卡喺 Connecting WiFi？檢查 WiFi 名/密碼，同埋係咪 2.4GHz

## 🤖 用 AI vibe coding 再改良
將 `.ino` 貼俾 AI，再講：
- 「每個鐘頭整點時，啲 LED 由左至右彩虹咁閃一次」
- 「加天氣：用 Open-Meteo 免費 API 顯示香港氣溫」（免 API key）
- 「加鬧鐘，用觸控設定時間，到鐘 RGB LED 閃」
- 「做倒數計時器模式，長按屏幕切換」
- 「數字轉變時加『翻牌』或者『LED 逐粒熄再著』動畫」
