# 02 Home Assistant 智能家居控制板 🏠💡

> ⚠️ **誠實聲明：** 原作者用嘅係現成工具 ESPControl（基於 ESPHome），冇公開 Arduino code。呢份係學造工作室「AI 補完」**自己用 Arduino 重新寫**嘅簡化版，**唔係原作者嘅程式**。

## 📌 原帖講咩
- **作者：** Alberto Zarza（CYD - Cheap Yellow Display - ESP32 群組）
- **原帖：** https://www.facebook.com/groups/710623908028293/posts/1099588685798478/
- **內容：** 佢用 **ESPControl**（https://jtenniswood.github.io/espcontrol/）配合 **Home Assistant**，喺一塊 ESP32 觸控屏整咗個好靚嘅智能家居面板：「All Lights」燈掣、29°C 溫度格、正在播放嘅音樂、計時等等，一格格好似手機 widget（見 `FB_screenshot_Alberto_Zarza.png`）。

## ✨ 我哋嘅 AI 補完版做咩
- 4 粒大觸控掣（2x2），撳一下就經 Home Assistant **REST API** 開/關燈、開關或者風扇
- 每 5 秒同 HA 同步狀態：用手機開咗燈，塊屏都會跟住轉橙色
- 頂部顯示溫度感應器讀數，同 WiFi 狀態小圓點
- 所有設定集中喺 `config.h`，唔使郁主程式

## 🧰 需要嘅硬件 / 軟件
- CYD ESP32-2432S028R 一塊（觸控屏已內置）
- 屋企已經裝好 **Home Assistant**，同 CYD 喺同一個 WiFi（2.4GHz）
- HA 入面最少有一盞燈或開關（未有真裝置可以喺 HA 整個 "Helper → Toggle" 嚟試）

## 📚 Library 同 Arduino IDE 設定
- **Board：** `ESP32 Dev Module`
- **Library：** 用 Larry 嘅 `libraries.zip`（TFT_eSPI 已設定 CYD）
- 另外要：**XPT2046_Touchscreen**（by Paul Stoffregen）、**ArduinoJson 第 6 版**（唔好裝 7）
    - `libraries.zip` 冇嘅話，喺「程式庫管理員」搜尋安裝；ArduinoJson 記得揀 6.21.x
- `WiFi`、`HTTPClient` 係 ESP32 內置

## ✏️ 要改嘅地方（全部喺 `config.h`）
1. `WIFI_SSID` / `WIFI_PASSWORD`：你屋企 WiFi
2. `HA_URL`：例如 `http://192.168.1.100:8123`（建議用 IP）
3. `HA_TOKEN`：喺 HA 撳左下角你個名 → **Security（安全）** → **Long-lived access tokens** → Create token，複製貼入去
4. `BUTTONS`：4 粒掣嘅名（英文）同 `entity_id`，例如 `light.living_room`（喺 HA「設定 → 裝置與服務 → 實體」搵）
5. `TEMP_SENSOR`：溫度感應器 entity_id，冇就留空 `""`

🔐 **Token 等於你屋企嘅鎖匙！** 唔好將填咗 token 嘅 `config.h` 上載去公開地方或者群組。

## ⬆️ 點樣上載
1. 下載 `02_HomeAssistant_Panel.zip`，解壓（folder 入面有 `.ino` 同 `config.h`，要放埋一齊）
2. 改好 `config.h`
3. Arduino IDE 打開 `02_HomeAssistant_Panel.ino` → Board `ESP32 Dev Module` → 選 Port → 上載（自動 reset，唔使撳掣）
4. Serial Monitor（115200）會見到 IP 同每次 toggle 嘅結果（200 = 成功，401 = token 錯）

💡 觸控唔準？改 `.ino` 入面 `map(p.x, 200, 3700, ...)` 嗰 4 個數。

## 🧩 唔想寫 code？No-code 方案
原作者用嘅 **ESPControl**（https://jtenniswood.github.io/espcontrol/）或者直接用 **ESPHome**，喺 Home Assistant 入面設定就得，面板仲靚啲。我哋呢份 Arduino 版嘅好處係**睇得明每一步點樣同 HA 傾偈**，方便學習同用 AI 改。

## 🤖 用 AI vibe coding 再改良
將 `.ino` + `config.h` 貼俾 AI，再講：
- 「加多一頁，左右掃切換，第二頁控制冷氣溫度（climate.set_temperature）」
- 「加一格顯示 HA 嘅天氣 weather.home」
- 「長按掣就調光（light.turn_on brightness_pct）」
- 「改成 3x3 九宮格，好似原作咁每格唔同顏色同 icon」
- 「夜晚 11 點之後自動調暗背光」
