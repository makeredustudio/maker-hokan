# 04 LVGL 拖放介面入門 🎨🖐️

> ⚠️ **誠實聲明：** 原帖係介紹一個視覺化編輯器（LCD Ultra Pro），唔係一份 code。呢份係學造工作室「AI 補完」**自己寫**嘅最簡單 LVGL 入門範例，**唔係 LCD Ultra Pro 產生嘅程式**，目的係等大家明白拖放編輯器背後做緊乜。

## 📌 原帖講咩
- **作者：** Zsolt Sziklai（LCD Ultra Pro 創辦人，CYD - Cheap Yellow Display - ESP32 群組）
- **原帖：** https://www.facebook.com/groups/710623908028293/posts/1098149399275740/
- **工具網站：** https://www.lcdultrapro.com/
- **內容：** 宣佈 **LCD Ultra Pro 免費 Beta** 推出。佢係一個喺瀏覽器用嘅 **LVGL 8.4** 視覺化編輯器：拖放 widget（掣、圖表、儀表…）、設定事件，然後匯出一個 ZIP（有 `ui.c`、`ui.h`、`lv_conf.h`），放入 Arduino IDE 就 compile 到，唔使人手寫 UI code（見 `FB_screenshot_Zsolt_Sziklai.png`）。

## ✨ 我哋嘅 AI 補完版做咩
一個 320x240 橫向畫面：
- 黃色標題「Hello CYD + LVGL」
- 一粒 **Button**：撳一下計數 +1，同時開/關板上 RGB LED（綠色）
- 一條 **Slider**：拉嚟即時調屏幕背光（GPIO21 PWM）
- Label 顯示計數同背光 %

Code 入面有詳細註解，分清楚：
1. `my_disp_flush()`：LVGL 畫好嘅嘢點樣交俾 TFT_eSPI 推上屏
2. `my_touch_read()`：點樣將 XPT2046 觸控交俾 LVGL
3. `build_ui()`：起畫面 ← **拖放編輯器幫你自動產生嘅就係呢部份**
4. `button_event_cb()` / `slider_event_cb()`：事件 ← 編輯器入面嘅「On Click」「On Value Changed」

## 🧰 需要嘅硬件
- 淨係 CYD ESP32-2432S028R 一塊

## 📚 Library 同 Arduino IDE 設定
- **Board：** `ESP32 Dev Module`
- **Library：** Larry 嘅 `libraries.zip`（TFT_eSPI 已設定 CYD）+ **XPT2046_Touchscreen**（by Paul Stoffregen）
- **lvgl 8.3.x**（我哋用 8.3.11 測試 compile；程式庫管理員搜 lvgl，**揀 8.3 版，唔好揀 9.x**，9.x 寫法唔同）
    - LCD Ultra Pro 用 8.4，寫法同 8.3 幾乎一樣

### ⚠️ 最重要：`lv_conf.h`
LVGL **一定要**有 `lv_conf.h` 先 compile 到！
1. 將呢個 folder 嘅 `lv_conf.h` 複製到 **`Arduino\libraries\`**
2. 位置要同 `lvgl` 資料夾**並排**（唔係放入 `lvgl` 入面！）
```
Arduino\libraries\
  ├─ lvgl\
  ├─ lv_conf.h   ← 放呢度
  ├─ TFT_eSPI\
  └─ XPT2046_Touchscreen\
```
呢份 `lv_conf.h` 已經改好：開咗 `#if 1`、`LV_TICK_CUSTOM 1`（用 millis 計時）、加咗 Montserrat 20 / 28 號字。
compile 見到 `lv_conf.h: No such file` 或者一大堆 LV_ 錯誤，九成係呢個檔放錯位。

## ✏️ 要改嘅地方
- 唔使改任何設定就用得！想學嘢就改 `build_ui()`：顏色（`lv_color_hex(0x...)`）、位置（`lv_obj_align`）、字（`lv_label_set_text`）

## ⬆️ 點樣上載
1. 放好 `lv_conf.h`（見上面）
2. 下載 `04_LVGL_Starter.zip` 解壓 → Arduino IDE 打開 `04_LVGL_Starter.ino`
3. Board `ESP32 Dev Module` → 選 Port → 上載（第一次 compile LVGL 會慢少少，耐心等 1–3 分鐘）

## 🧭 視覺化編輯器點樣幫你產生呢啲 code？
- **LCD Ultra Pro**（https://www.lcdultrapro.com/）：瀏覽器開，揀屏幕尺寸 320x240，拖放 widget → Export ZIP → 得到 `ui.c` / `ui.h` / `lv_conf.h`。你喺 `setup()` 入面 call `ui_init();` 取代我哋嘅 `build_ui();` 就得，flush 同 touch 兩條「橋」照用我哋呢份。
- **SquareLine Studio**：概念一樣，匯出 `ui` folder，同樣 call `ui_init()`。
- 記住：編輯器負責「畫面長咩樣」，你仍然要自己（或者叫 AI）寫「撳咗之後做咩」，例如控制 LED、讀感應器。

## 🤖 用 AI vibe coding 再改良
將 `.ino` 貼俾 AI，再講（記得話佢知係 **LVGL 8.3**）：
- 「加一個 lv_arc 圓形儀表，顯示 LDR (GPIO34) 光度」
- 「加一個 lv_chart 每秒畫一點，畫出光度變化」
- 「整兩個 screen，撳掣用滑動動畫切換」
- 「加一個 color wheel，揀色控制 RGB LED（GPIO 4/16/17，低電平亮）」
- 「將我喺 LCD Ultra Pro 匯出嘅 ui.c 接駁落呢份 code」
