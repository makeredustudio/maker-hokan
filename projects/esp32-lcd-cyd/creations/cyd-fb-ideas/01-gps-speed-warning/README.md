# 01 GPS 超速警告（CYD 版）🚗💨

> ⚠️ **誠實聲明：** 呢份 code 係學造工作室「AI 補完」**自己重新寫**嘅版本，靈感來自下面嗰個 Facebook 帖。原作者冇公開佢嘅 code，所以呢度**唔係原作者嘅程式**，做法亦簡化咗好多，啱初學者入門。

## 📌 原帖講咩
- **作者：** Tooraj Enayati（CYD - Cheap Yellow Display - ESP32 群組）
- **原帖：** https://www.facebook.com/groups/710623908028293/posts/1099562032467810/
- **熱度：** 群組近期最受歡迎嘅作品之一（約 140 個讚好、25 個留言）
- **內容：** 佢整咗一部 ESP32 GPS 車速錶，超速就會警告。佢喺電腦預先處理 OpenStreetMap (OSM) 嘅道路同限速 (maxspeed) 資料，切成一格格細 tile，轉做 .bin 檔放入 SD 卡。行車時 ESP32 用 GPS 位置搵返嗰格 tile、認出係邊條路、讀出限速，**全程唔使上網**。畫面有大字車速、衛星數目、方向、海拔、限速牌同「SPEEDING」警告（見 `FB_screenshot_Tooraj_Enayati.png`）。

## ✨ 我哋嘅 AI 補完版做咩
- NEO-6M GPS 讀速度，用超大數字（Font 8）顯示 km/h
- 由 SD 卡讀 `zones.csv`：每行一個**圓形限速區**（中心點 + 半徑 + 限速）
    - 呢個係原作「OSM 道路 tile」嘅**簡化版**，易明好多，自己用 Google Maps 就整到
    - 唔喺任何區入面就用預設限速（50 km/h，可以改）
- 右上角有紅圈限速牌
- 超速（超過限速 3 km/h 以上）：畫面閃紅 + 「SLOW DOWN!」+ 板上 RGB LED 閃紅
- 正常：LED 綠色；未搵到衛星：LED 藍色
- 全離線，唔使 WiFi

## 🧰 需要嘅硬件
| 物料 | 備註 |
|---|---|
| CYD ESP32-2432S028R | 2.8 吋黃色板 |
| NEO-6M GPS 模組（連天線） | 網上大約 HK$30–50 |
| Micro SD 卡 | FAT32 格式，32GB 或以下最穩陣 |
| 杜邦線 4 條 | CYD 通常有附送 CN1 插頭線 |

### 接線（用 CYD 嘅 **CN1** 插座：GND / IO22 / IO27 / 3.3V）
| GPS 模組 | CYD |
|---|---|
| VCC | 3.3V |
| GND | GND |
| TX | **IO27**（ESP32 收） |
| RX | IO22（可以唔接） |

💡 GPS 第一次定位要喺**露天**等 1–5 分鐘，室內通常搵唔到衛星。

## 📚 Library 同 Arduino IDE 設定
- **Board：** `ESP32 Dev Module`（其他保持預設）
- **Library：** 用 Larry 共享 Drive 嘅 `libraries.zip`（入面嘅 TFT_eSPI `User_Setup.h` 已經改好 CYD 設定），解壓放入 `Arduino\libraries`
- 額外要：**TinyGPSPlus**（by Mikal Hart）。如果 `libraries.zip` 冇，喺 Arduino IDE「程式庫管理員」搜尋 TinyGPSPlus 安裝
- SD 卡用 ESP32 內置嘅 `SD` library，唔使另外裝

## ✏️ 要改嘅地方
1. **`zones.csv`**：用 Google Maps 長按你想設限速嘅地方 → 抄低座標 → 填 `lat,lon,radius_m,limit,name`。檔入面嘅香港座標只係示範！
2. `.ino` 開頭可以改：
   - `DEFAULT_LIMIT`：預設限速
   - `OVER_MARGIN`：超幾多先響（預設 3 km/h）
   - `GPS_RX_PIN / GPS_TX_PIN`：如果你接咗第二隻腳

## ⬆️ 點樣上載
1. 下載 `01_GPS_Speed_Warning.zip`，解壓（會得到一個同名 folder，入面有 .ino）
2. 將 `zones.csv` 抄入 SD 卡**根目錄**，插入 CYD
3. Arduino IDE 打開 `01_GPS_Speed_Warning.ino`
4. 選 Board `ESP32 Dev Module`、選 Port
5. 撳「上載」→ 呢塊 CYD 會自動下載同 reset，唔使撳掣 🎉
6. 打開 Serial Monitor（115200）可以睇到「載入咗 X 個限速區」

## 🧪 進階（可選）：由 OpenStreetMap 自動整 zones.csv
`osm_to_zones.py` 會由 OSM 攞某個範圍嘅道路限速，每條路變成一個圓形區：
```
python3 osm_to_zones.py 22.30 114.15 22.35 114.20 > zones.csv
```
（四個數 = 南、西、北、東邊界；範圍唔好揀太大）

## 🤖 用 AI vibe coding 再改良
將成份 `.ino` 貼俾 ChatGPT / Gemini / Claude，再講你想要咩，例如：
- 「加埋指南針方向同海拔，好似原作咁顯示」
- 「超速時用 GPIO26 驅動一個蜂鳴器嗶嗶聲」
- 「將每次超速嘅時間同位置記錄落 SD 卡做 log.csv」
- 「改用原作嘅方法：將道路畫成線段，計算最近嗰條路嘅限速」
- 「加一個觸控掣，撳一下切換 km/h 同 mph」

⚠️ **安全提提你：** 揸車/踩單車時唔好望住塊屏玩，呢個係學習用嘅 project，唔可以代替留意真正嘅路牌。
