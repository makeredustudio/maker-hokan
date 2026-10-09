/*
  CYD 持續注意力示範（CPT 風格・教學用）
  CYD Continuous Attention Demo (CPT-style · Educational)
  ------------------------------------------------------------
  板：CYD ESP32-2432S028R（2.8" ILI9341 320x240 觸控）

  ⚠️ 重要聲明 / CRITICAL DISCLAIMER
  ------------------------------------------------------------
  - 呢份程式 **唔係** Conners CPT 3 / Conners CPT™ 3，亦唔係任何
    Multi-Health Systems (MHS) 產品。Conners CPT 3 係 MHS 嘅專有臨床軟體。
  - 呢個只係一個 **簡化嘅教學示範**，參考「持續表現測試（Continuous
    Performance Test, CPT）」嘅一般公開原理，畀 Maker / 老師喺課堂介紹
    「持續注意力、衝動抑制」等概念。
  - **唔會** 畀嚟做臨床診斷、篩查、治療決定或者任何醫療用途。
  - 屏幕上嘅分數只係 **原始計數**（撳對、漏撳、誤撳、平均反應時間），
    **冇** 常模 T 分數、**冇** ADHD 臨界值、**冇** 臨床解釋。
  - 請唔好將本作品標成「Conners CPT 3」。正式臨床工具請查 MHS：
    https://www.mhs.com/
  ------------------------------------------------------------

  玩法（教學用 go/no-go）：
   - 屏幕隨機顯示英文字母 A–Z
   - 見到 **唔係 X** → 盡快撳屏幕或 BOOT 掣（正確反應 = Hit）
   - 見到 **X** → 唔好撳（撳咗 = Commission / 誤撳）
   - 應該撳但冇撳 = Omission / 漏撳
   - 先練習 20 次，再正式約 3–5 分鐘（可改設定）

  Library：TFT_eSPI（User_Setup.h 要設定做 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
  單一 .ino，冇 config.h

  v1.3：Practice 10、Main 45、字母最少顯示 1.5 秒、ISI 縮短\n  v1.2 修正：\n   - 黃底黑字 + drawChar 手動置中（解決仍然見唔到字母）\n  v1.1：
   - 大字母改用 font 1 + setTextSize（FONT 8 只有數字，字母會空白）
   - 字母喺成個反應窗口都顯示（唔會 250ms 就變 +）
   - 觸控改邊緣觸發 + drainInputs，避免「撳開始」殘留變成永遠 Hit
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ================== 可以改呢度（教學參數） ==================
#define PRACTICE_TRIALS   10      // 練習次數
#define MAIN_TRIALS       45      // 正式次數（縮短課堂用）
#define TARGET_PCT        10      // X（非目標）大約佔百分之幾（其餘係目標字母）
#define STIM_MS           1500    // 最短顯示時間（字母會留到 ISI 完，唔好閃完即冇）
const uint16_t ISI_MS[] = { 1500, 2000, 2500 };  // 刺激間隔（最短跟 STIM_MS，唔再有 4 秒咁慢）
const int ISI_COUNT = sizeof(ISI_MS) / sizeof(ISI_MS[0]);
#define FIXATION_MS       400     // 兩次之間「+」注視點時間
#define READY_HOLD_MS     2000    // 「準備」畫面停留
#define FEEDBACK_PRACTICE true    // 練習模式顯示對/錯提示
#define LETTER_SIZE       7       // setTextSize：大字母（font 1 放大）
#define TOUCH_Z_MIN       400     // 壓力門檻（太低會誤觸）

// 顏色（RGB565）
#define COL_BG      0x0841
#define COL_PANEL   0x2124
#define COL_TEXT    0xDEFB
#define COL_ACCENT  0x07FF
#define COL_WARN    0xFD20
#define COL_OK      0x07E0
#define COL_BAD     0xF800
#define COL_MUTE    0x7BEF

// 觸控校正
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// ================== 硬件腳位 ==================
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21
#define BOOT_BTN     0     // 板上 BOOT：撳落 = LOW = 反應
#define LED_R 4
#define LED_G 16
#define LED_B 17

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;

enum Screen { HOME, READY, FIXATION, TRIAL, FEEDBACK, DONE };
Screen screen = HOME;

bool practiceMode = true;
int  trialIdx = 0;
int  totalTrials = PRACTICE_TRIALS;
char stimLetter = 'A';
bool isNonTarget = false;   // true = 見到 X，唔該撳
uint16_t isiMs = 1000;
uint32_t stimOnMs = 0;
uint32_t phaseMs = 0;
bool responded = false;
uint32_t responseMs = 0;

// ---- 原始計數（只係教學用，唔係臨床分數）----
int hits = 0;
int omissions = 0;
int commissions = 0;
int correctRejects = 0;
uint32_t rtSum = 0;
uint32_t rtSumSq = 0;
int rtCount = 0;
int isiHits[3] = {0, 0, 0};
int isiCount[3] = {0, 0, 0};
int isiIdx = 0;

// ---- 邊緣觸發輸入（避免一直按住 / 殘留觸控）----
bool prevDown = false;

bool rawDown() {
  if (digitalRead(BOOT_BTN) == LOW) return true;
  // 唔好淨靠 tirqTouched（有啲板 IRQ 會黏）；一定要讀壓力
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z >= TOUCH_Z_MIN) return true;
  }
  return false;
}

// 上升沿：上一幀冇撳、而家撳住 → 一次有效撳
bool pressEdge() {
  bool now = rawDown();
  bool edge = (now && !prevDown);
  prevDown = now;
  return edge;
}

// 清空殘留觸控 / BOOT，並將邊緣狀態重置為「已按住」以免下一幀誤觸
void drainInputs(uint16_t ms = 250) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    rawDown();   // 讀走緩衝
    delay(5);
  }
  // 等完全鬆手（最多再 800ms）
  t0 = millis();
  while (rawDown() && millis() - t0 < 800) delay(5);
  delay(30);
  prevDown = rawDown();   // 如果仲掂住，當「已按住」，唔會出 edge
}

bool waitTapOrBoot() {
  if (!pressEdge()) return false;
  drainInputs(150);
  return true;
}

void ledOff() {
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
}
void ledOk()  { digitalWrite(LED_R, HIGH); digitalWrite(LED_G, LOW);  digitalWrite(LED_B, HIGH); }
void ledBad() { digitalWrite(LED_R, LOW);  digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH); }

// ---------------- UI ----------------
void drawDisclaimerBanner() {
  tft.fillRect(0, 220, 320, 20, 0x4000);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, 0x4000);
  tft.drawString("TEACHING DEMO ONLY - NOT clinical / NOT Conners CPT 3", 160, 230, 1);
}

void drawHome() {
  screen = HOME;
  ledOff();
  tft.setTextSize(1);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString("CYD Attention Demo", 160, 8, 4);
  tft.setTextColor(COL_WARN, COL_BG);
  tft.drawString("CPT-style · Educational", 160, 40, 2);

  tft.fillRoundRect(10, 62, 300, 72, 8, COL_PANEL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_PANEL);
  tft.drawString("NOT Conners CPT 3 (MHS proprietary).", 18, 70, 2);
  tft.drawString("NOT for diagnosis / screening / medical use.", 18, 90, 2);
  tft.drawString("Raw counts only. No T-scores. No ADHD cutoff.", 18, 110, 2);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_MUTE, COL_BG);
  tft.drawString("Press if letter is NOT X. Do NOT press if X.", 160, 145, 2);
  tft.setTextColor(COL_OK, COL_BG);
  tft.drawString("TAP SCREEN / BOOT to start  (practice 10)", 160, 175, 2);
  tft.setTextColor(COL_MUTE, COL_BG);
  char buf[48];
  snprintf(buf, sizeof(buf), "Main block: %d trials  ISI %d/%d/%d ms",
           MAIN_TRIALS, ISI_MS[0], ISI_MS[1], ISI_MS[2]);
  tft.drawString(buf, 160, 198, 1);
  drawDisclaimerBanner();
}

void drawReady() {
  screen = READY;
  phaseMs = millis();
  tft.setTextSize(1);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(practiceMode ? "PRACTICE" : "MAIN BLOCK", 160, 80, 4);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Get ready...", 160, 120, 2);
  tft.setTextColor(COL_MUTE, COL_BG);
  tft.drawString("Press ONLY when letter is NOT X", 160, 155, 2);
  drawDisclaimerBanner();
  drainInputs(200);   // 清走「撳開始」殘留
}

void resetStats() {
  hits = omissions = commissions = correctRejects = 0;
  rtSum = rtSumSq = 0; rtCount = 0;
  for (int i = 0; i < 3; i++) { isiHits[i] = 0; isiCount[i] = 0; }
}

void pickStimulus() {
  isNonTarget = (random(100) < TARGET_PCT);
  if (isNonTarget) {
    stimLetter = 'X';
  } else {
    do { stimLetter = 'A' + random(26); } while (stimLetter == 'X');
  }
  isiIdx = random(ISI_COUNT);
  isiMs = ISI_MS[isiIdx];
  // 反應窗口至少要同顯示時間一樣長
  if (isiMs < STIM_MS) isiMs = STIM_MS;
  responded = false;
  responseMs = 0;
}

void drawFixation() {
  screen = FIXATION;
  phaseMs = millis();
  tft.setTextSize(1);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_MUTE, COL_BG);
  tft.setTextSize(4);
  tft.drawString("+", 160, 110, 1);   // font 1 放大；FONT 8 冇「+」
  tft.setTextSize(1);
  drawDisclaimerBanner();
  drainInputs(80);
}

// 用黃底 + GLCD drawChar 手動置中（MC_DATUM + setTextSize 喺一啲 CYD 會畫唔出）
void drawBigLetter(char c) {
  const int boxW = 140;
  const int boxH = 140;
  const int boxX = (320 - boxW) / 2;
  const int boxY = 42;
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 12, TFT_YELLOW);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 12, TFT_WHITE);

  // GLCD font 1：每字 6x8，再 * LETTER_SIZE
  const int cw = 6 * LETTER_SIZE;
  const int ch = 8 * LETTER_SIZE;
  const int cx = boxX + (boxW - cw) / 2;
  const int cy = boxY + (boxH - ch) / 2;
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(LETTER_SIZE);
  tft.setTextColor(TFT_BLACK, TFT_YELLOW);
  tft.drawChar(c, cx, cy, 1);

  // 備援：font 4 細字標喺黃框底（證明字型正常）
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_YELLOW);
  char s[2] = { c, 0 };
  tft.drawString(s, 160, boxY + boxH - 18, 4);
}

void drawStimulus() {
  screen = TRIAL;
  stimOnMs = millis();
  tft.fillScreen(COL_BG);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s  %d / %d", practiceMode ? "Practice" : "Main", trialIdx + 1, totalTrials);
  tft.setTextColor(COL_MUTE, COL_BG);
  tft.drawString(buf, 160, 14, 2);

  drawBigLetter(stimLetter);

  snprintf(buf, sizeof(buf), "ISI %d ms  |  press if NOT X", isiMs);
  tft.setTextColor(COL_MUTE, COL_BG);
  tft.drawString(buf, 160, 198, 1);
  drawDisclaimerBanner();

  Serial.printf("[trial %d/%d] letter=%c nontarget=%d isi=%u\n",
                trialIdx + 1, totalTrials, stimLetter, isNonTarget ? 1 : 0, isiMs);
  drainInputs(50);
  prevDown = rawDown();
}

void drawFeedback(bool ok, const char *msg) {
  screen = FEEDBACK;
  phaseMs = millis();
  tft.setTextSize(1);
  tft.fillRoundRect(40, 70, 240, 80, 10, COL_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ok ? COL_OK : COL_BAD, COL_PANEL);
  tft.drawString(msg, 160, 110, 4);
  if (ok) ledOk(); else ledBad();
}

void finishTrial() {
  Serial.printf("  -> responded=%d rt=%lu ms  => ",
                responded ? 1 : 0, (unsigned long)responseMs);

  if (!isNonTarget) {
    isiCount[isiIdx]++;
    if (responded) {
      hits++;
      isiHits[isiIdx]++;
      rtSum += responseMs;
      rtSumSq += responseMs * responseMs;
      rtCount++;
      Serial.println("HIT");
      if (practiceMode && FEEDBACK_PRACTICE) { drawFeedback(true, "Hit"); return; }
    } else {
      omissions++;
      Serial.println("MISS");
      if (practiceMode && FEEDBACK_PRACTICE) { drawFeedback(false, "Miss"); return; }
    }
  } else {
    if (responded) {
      commissions++;
      Serial.println("FALSE PRESS");
      if (practiceMode && FEEDBACK_PRACTICE) { drawFeedback(false, "False press"); return; }
    } else {
      correctRejects++;
      Serial.println("CORRECT HOLD");
      if (practiceMode && FEEDBACK_PRACTICE) { drawFeedback(true, "Correct hold"); return; }
    }
  }
  nextAfterFeedback();
}

void nextAfterFeedback() {
  ledOff();
  trialIdx++;
  if (trialIdx >= totalTrials) {
    if (practiceMode) {
      practiceMode = false;
      trialIdx = 0;
      totalTrials = MAIN_TRIALS;
      resetStats();
      drawReady();
    } else {
      showDone();
    }
    return;
  }
  if (FIXATION_MS > 0) drawFixation();
  else { pickStimulus(); drawStimulus(); }
}

void showDone() {
  screen = DONE;
  ledOff();
  tft.setTextSize(1);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString("Session complete", 160, 4, 4);

  tft.fillRoundRect(6, 32, 308, 28, 4, 0x8000);
  tft.setTextColor(TFT_WHITE, 0x8000);
  tft.drawString("TEACHING DEMO · NOT clinical diagnosis", 160, 46, 2);

  int meanRt = rtCount ? (int)(rtSum / rtCount) : 0;
  int sdRt = 0;
  if (rtCount >= 2) {
    float mean = (float)rtSum / rtCount;
    float var = ((float)rtSumSq - (float)rtSum * mean) / (rtCount - 1);
    if (var > 0) sdRt = (int)sqrtf(var);
  }

  char buf[48];
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  int y = 68;
  auto line = [&](const char *label, int val, const char *unit = "") {
    snprintf(buf, sizeof(buf), "%-16s %4d %s", label, val, unit);
    tft.drawString(buf, 16, y, 2); y += 16;
  };
  line("Hits", hits);
  line("Omissions", omissions);
  line("Commissions", commissions);
  line("Correct reject", correctRejects);
  line("Mean RT", meanRt, "ms");
  line("RT SD (simple)", sdRt, "ms");

  tft.setTextColor(COL_MUTE, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Raw counts only. No norms. No ADHD label.", 160, 188, 1);
  tft.setTextColor(COL_OK, COL_BG);
  tft.drawString("TAP / BOOT = home", 160, 206, 2);
  drawDisclaimerBanner();
  drainInputs(200);
}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  ledOff();

  tft.init();
  tft.setRotation(1);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  randomSeed(esp_random());
  Serial.println(F("CYD CPT-style Attention Demo v1.1 — EDUCATIONAL ONLY, NOT Conners CPT 3"));
  Serial.println(F("Serial 115200: watch letter / responded / RT each trial"));
  drawHome();
  drainInputs(100);
}

void loop() {
  switch (screen) {
    case HOME:
      if (waitTapOrBoot()) {
        practiceMode = true;
        trialIdx = 0;
        totalTrials = PRACTICE_TRIALS;
        resetStats();
        drawReady();
      }
      break;

    case READY:
      // READY 期間唔接受反應；完咗再 drain 一次
      pressEdge();   // 更新 prevDown，吞掉誤觸
      if (millis() - phaseMs >= READY_HOLD_MS) {
        drainInputs(150);
        if (FIXATION_MS > 0) drawFixation();
        else { pickStimulus(); drawStimulus(); }
      }
      break;

    case FIXATION:
      pressEdge();   // 吞掉誤觸，唔計分
      if (millis() - phaseMs >= FIXATION_MS) {
        pickStimulus();
        drawStimulus();
      }
      break;

    case TRIAL: {
      // 字母留喺屏幕成個 ISI；只接受上升沿
      if (!responded && pressEdge()) {
        responded = true;
        responseMs = millis() - stimOnMs;
        Serial.printf("  press at %lu ms\n", (unsigned long)responseMs);
        // 唔好 while(buttonDown) 卡住；交俾 pressEdge 處理鬆手
      }
      if (millis() - stimOnMs >= isiMs) {
        finishTrial();
      }
      break;
    }

    case FEEDBACK:
      pressEdge();
      if (millis() - phaseMs >= 700) nextAfterFeedback();
      break;

    case DONE:
      if (waitTapOrBoot()) drawHome();
      break;
  }
}
