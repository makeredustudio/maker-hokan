# Idea from CYD Facebook page（AI 補完）總覽 📋

呢度收集咗 Facebook 群組「**CYD - Cheap Yellow Display - ESP32**」近期最多人互動嘅作品，每個都整咗一個「學造工作室 AI 補完版」：有可以直接上載落 **CYD ESP32-2432S028R** 嘅 Arduino code、原帖截圖同廣東話說明。

> ⚠️ **誠實聲明：** 所有原作者都冇公開 code。呢度嘅程式全部係我哋**參考原帖意念自己重新寫**嘅版本（有啲仲簡化咗），唔係原作者嘅 code。每個說明都有原帖連結同作者名，多謝原作者分享 🙏
> （揀選方法：Facebook 冇得按「最熱門」排序，所以係由近期貼文入面揀讚好/留言最多嘅作品。）

| # | 作品 | 一句講晒 | 難度 | 原作者 |
|---|---|---|---|---|
| 01 | **GPS 超速警告** | 接 GPS 模組做車速錶，入咗 SD 卡設定嘅限速區超速就閃紅警告，全離線 | ⭐⭐⭐ | Tooraj Enayati（最熱門：約 140 讚 / 25 留言） |
| 02 | **Home Assistant 智能家居控制板** | 觸控掣經 HA REST API 開關屋企燈，顯示溫度 | ⭐⭐⭐ | Alberto Zarza |
| 03 | **WiFi 像素時鐘** | NTP 自動對時嘅 LED 點陣大字鐘，撳屏轉主題，自動調光 | ⭐⭐ | Rohit Singh |
| 04 | **LVGL 拖放介面入門** | 最簡單 LVGL 範例（掣 + 滑桿調背光），了解 LCD Ultra Pro 等拖放編輯器 | ⭐⭐ | Zsolt Sziklai |

**建議次序：** 03 → 04 → 02 → 01（由唔使額外硬件嘅開始）

## 💡 額外靈感（冇 code）
- **Ryan Giles 嘅 3D 打印 CYD 車用支架**：https://www.facebook.com/groups/710623908028293/posts/1100217809068899/
  可以配合 01 GPS 超速警告，打印個支架將 CYD 擺上單車/車上（純意念分享，冇 code）。

## 🛠️ 共通設定
- **Board：** `ESP32 Dev Module`；呢塊 CYD 上載會自動 reset，唔使撳 BOOT
- **Library：** 先解壓 Larry 共享 Drive 嘅 `libraries.zip` 落 `Arduino\libraries`（TFT_eSPI 已設定 CYD）
- 個別作品額外要嘅 library：TinyGPSPlus（01）、XPT2046_Touchscreen（02/03/04）、ArduinoJson **6**（02）、lvgl **8.3.x** + `lv_conf.h`（04）
- 每個 folder 有 `.zip`：解壓就係一個完整 Arduino sketch folder，最方便
- 所有 code 已經用 arduino-cli（ESP32 core 2.0.17）compile 測試通過；未喺實機逐個測試，觸控/LDR 數值可能要按你塊板微調

## 🤖 Vibe coding 小貼士
將成份 `.ino`（同其他 .h 檔）貼俾 AI，講清楚：「呢塊係 CYD ESP32-2432S028R，用 TFT_eSPI，屏幕 320x240 橫向」，再講你想加咩功能，效果會準確好多！
