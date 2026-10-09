# 06 CYD 電子琴 🎹🔊

> ⚠️ **誠實聲明：** 原作者冇公開 code。呢份係學造工作室「AI 補完」參考原帖意念、喺 CYD 上面**自己重新寫**嘅版本，**唔係原作者嘅程式**。只係 compile 測試通過，未喺實機測試。

## 📌 原帖講咩
- **作者：** Fred Cabor（私人群組「CYD - Cheap Yellow Display ESP32」）
- **原帖：** https://www.facebook.com/groups/8104937882859357/posts/29365162043076966/
- **內容：** 「Electronic keyboard from a cheap yellow display」— 片入面 CYD 屏幕顯示藍色控制掣同一排白色琴鍵，旁邊張紙寫住幾種音色/曲目名（見 `FB_screenshot_Fred_Cabor.png`）。

## ✨ 我哋嘅 AI 補完版做咩
- **觸控琴鍵**：1 個八度（8 白鍵，好撳）或 2 個八度（15 白鍵）一掣切換，黑白鍵齊
- **OCT− / OCT+**：音域 C2 至 C6
- **5 種音色**：Sine 正弦（柔和）、Square 方波（8-bit 遊戲機聲）、Saw 鋸齒（合成器味）、Triangle 三角、Organ 風琴（加泛音）
- **Attack / Release 包絡**：起音同放手都順滑，冇「咔咔」聲
- 撳住嘅琴鍵**變色**，上面顯示**音名同頻率**（例如 `A4 440.0 Hz`）；手指喺琴鍵上**滑**可以連續轉音
- **VOL** 三級音量；**DEMO** 自動彈《小星星》，琴鍵跟住亮（撳屏停止）
- 背面 RGB LED 跟音高轉顏色

## 🔊 聲音點樣出嚟？（重要）
- 用 CYD 板上嘅 **SPEAK 插座**：**GPIO26（ESP32 內置 DAC）→ 板載功放 → 喇叭**
- 喇叭要用 **8Ω（約 0.5–2W）小喇叭，配 JST 1.25mm 2-pin 插頭**（網上搜「CYD speaker」或「8 ohm speaker JST 1.25」）
- **唔係**接外置蜂鳴器；唔使額外電路
- 原理：ESP32 硬件 timer 每秒觸發 **20,000 次**中斷，每次由「波形表」攞一個數，寫入 8-bit DAC，就砌出聲波。音高 = 讀波形表嘅速度（phase accumulator）
- 板載功放細細粒，聲音唔會好大聲；VOL 3 如果有破聲就用 VOL 2

## 🧰 需要嘅硬件
- CYD ESP32-2432S028R 一塊
- 8Ω 小喇叭（JST 1.25mm 2-pin），插 SPEAK 插座

## 📚 Library 同 Arduino IDE 設定
- **Board：** `ESP32 Dev Module`（ESP32 core **2.0.17**——用咗 2.x 嘅 `timerBegin(0, 80, true)` 寫法，3.x 要改）
- **Library：** Larry 嘅 `libraries.zip`（TFT_eSPI 已設定 CYD）+ **XPT2046_Touchscreen**
- 單一 `.ino`，冇 config.h

## ✏️ 可以改嘅地方（`.ino` 最頂）
- `START_OCTAVE`（開機音域）、`TWO_OCTAVES`（開機用 2 個八度）
- `ATTACK_MS` / `RELEASE_MS`：改長 `RELEASE_MS` 好似鋼琴踩咗延音踏板
- `volumeLvl`：預設音量
- `buildWavetables()`：自己設計音色！例如改 Organ 嘅泛音比例
- `TWINKLE[]`：改 DEMO 曲（數字 = 由 C 開始數嘅半音，-1 = 休止）
- 觸控撳唔準：Serial Monitor（115200）睇 `touch x= y=`，改 `TS_MINX` 等數值

## ⬆️ 點樣上載
1. 下載 `06_CYD_Synth_Keyboard.zip` 解壓（folder 名同 .ino 名一樣）
2. 插好喇叭 → Arduino IDE 打開 → Board `ESP32 Dev Module` → 選 Port → 上載
3. 撳 DEMO 試聲，再自己彈 🎶

## 🤖 用 AI vibe coding 再改良
將 `.ino` 貼俾 AI，再講：
- 「加錄音同重播：撳 REC 記低彈過嘅音同時間，撳 PLAY 播返」
- 「加鼓機：屏幕上面 4 個鼓 pad，用噪音波形整 hi-hat 同 snare」
- 「加 vibrato（顫音）同 LFO 效果」
- 「做『跟住彈』遊戲：琴鍵亮邊個就撳邊個，計分」
- 「加 MIDI over USB / BLE MIDI，用 CYD 做 MIDI 控制器」
