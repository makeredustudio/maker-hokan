/*
  CYD 靜觀呼吸機 (CYD Mindful Breathing)   — CYD ESP32-2432S028R
  ------------------------------------------------------------
  學造工作室 Maker 作品：跟住屏幕上「慢慢變大 / 變細」嘅圓圈呼吸，
  幫自己靜落嚟。單一 .ino 檔，唔使其他 config.h。

  功能：
   - 3 種呼吸法：Box 4-4-4-4、4-7-8 放鬆、Calm 5-5 平靜呼吸
   - 練習時間：1 / 3 / 5 分鐘，有倒數
   - 圓圈平滑放大縮細（用 TFT_eSprite，唔會閃）
   - 背面 RGB LED 跟住呼吸階段柔和變色（可關）
   - 喇叭 GPIO26：開始 / 完結各響一下溫柔鐘聲（音量 OFF / LOW / MED）
   - 練習中撳屏 = 暫停（可繼續或返主頁）

  Library：TFT_eSPI（User_Setup.h 要設定做 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
  屏幕文字用英文（TFT_eSPI 內置字型冇中文）
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ================== 可以改呢度 ==================
// 呼吸法：名稱、吸氣、停(吸氣後)、呼氣、停(呼氣後)  單位：秒（0 = 冇呢個階段）
struct Pattern { const char *name; const char *label; uint8_t inhale, hold1, exhale, hold2; };
const Pattern PATTERNS[] = {
  { "Box 4-4-4-4", "Box",   4, 4, 4, 4 },
  { "4-7-8 Relax", "4-7-8", 4, 7, 8, 0 },
  { "Calm 5-5",    "Calm",  5, 0, 5, 0 },
};
const int PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

// 練習時間選擇（分鐘）
const uint8_t SESSION_MINUTES[] = { 1, 3, 5 };
const int SESSION_COUNT = sizeof(SESSION_MINUTES) / sizeof(SESSION_MINUTES[0]);

// 每個階段嘅顏色（RGB565）
#define COL_INHALE  0x2E9C   // 柔和青藍
#define COL_HOLD    0x9B1C   // 淡紫
#define COL_EXHALE  0x2DCB   // 柔和青綠
#define COL_BG      0x0841   // 深夜藍黑背景
#define COL_BTN     0x2124   // 按鈕底色
#define COL_SEL     0x2E9C   // 已選按鈕
#define COL_TEXT    0xDEFB   // 柔白

// 圓圈大細（像素）
#define R_MIN  28
#define R_MAX  84

// 預設設定
int  patternIdx = 0;     // 0 = Box
int  sessionIdx = 1;     // 1 = 3 分鐘
int  volumeLvl  = 1;     // 0 = OFF, 1 = LOW, 2 = MED
bool ledOn      = true;  // 背面 RGB LED

// 觸控校正（如果撳唔準，打開 Serial Monitor 睇 raw 數值再改）
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
#define LED_R        4    // RGB LED：LOW = 著
#define LED_G        16
#define LED_B        17
#define SPEAKER_PIN  26

// LEDC 通道（ch0 喇叭；ch2/3/4 RGB LED，避開同喇叭共用 timer）
#define CH_SPK 0
#define CH_R   2
#define CH_G   3
#define CH_B   4

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);

// 圓圈畫喺 sprite 入面，再一次過推上屏，避免閃
#define SPR_W 184
#define SPR_H 184
#define SPR_X ((320 - SPR_W) / 2)
#define SPR_Y 30

struct Btn { int x, y, w, h; };   // 觸控按鈕範圍

enum Screen { HOME, READY, BREATH, PAUSED, DONE };
Screen screen = HOME;

enum Phase { PH_INHALE, PH_HOLD1, PH_EXHALE, PH_HOLD2 };
const char *PHASE_TEXT[] = { "Breathe in", "Hold", "Breathe out", "Hold" };

// 練習狀態
uint32_t sessionMs;        // 總練習時間
uint32_t elapsedMs;        // 已練習時間（唔計暫停）
uint32_t phaseStartMs;     // 目前階段開始（以 elapsedMs 計）
uint32_t lastTickMs;       // 上一次 loop 嘅 millis()
uint32_t readyStartMs;
Phase    phase;
int      breaths;          // 完成咗幾多次呼吸
bool     timeUp;
int      lastShownSec = -1;

// ---------------- 觸控 ----------------
bool getTouch(int &x, int &y) {
  if (!(ts.tirqTouched() && ts.touched())) return false;
  TS_Point p = ts.getPoint();
  x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, 320), 0, 319);
  y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, 240), 0, 239);
  // 等手指放開，避免一撳觸發幾次
  uint32_t t0 = millis();
  while (ts.touched() && millis() - t0 < 1500) delay(10);
  delay(60);
  return true;
}

bool inBtn(const Btn &b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
void drawBtn(const Btn &b, const char *txt, bool sel, uint16_t selCol = COL_SEL) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 10, sel ? selCol : COL_BTN);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 10, sel ? TFT_WHITE : 0x4208);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(sel ? TFT_BLACK : COL_TEXT);
  tft.drawString(txt, b.x + b.w / 2, b.y + b.h / 2, 2);
}

// ---------------- RGB LED ----------------
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!ledOn) r = g = b = 0;
  // active LOW：duty 255 = 熄
  ledcWrite(CH_R, 255 - r);
  ledcWrite(CH_G, 255 - g);
  ledcWrite(CH_B, 255 - b);
}
// RGB565 → 柔和 LED 光（除細啲，夜晚唔刺眼）
void ledFrom565(uint16_t c, float level) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5) & 0x3F) << 2;
  uint8_t b = (c & 0x1F) << 3;
  float k = 0.25f * level;  // 最多 25% 光
  setLed(r * k, g * k, b * k);
}

// ---------------- 鐘聲 ----------------
// ledcWriteTone 預設 50% duty 好大聲；之後改細 duty 就細聲，再慢慢減 = 鐘聲尾音
void bell(int freq, int ms) {
  if (volumeLvl == 0) return;
  int startDuty = (volumeLvl == 1) ? 6 : 30;    // 10-bit 解像度，最大 512
  ledcWriteTone(CH_SPK, freq);
  int steps = ms / 20;
  for (int i = 0; i < steps; i++) {
    float k = 1.0f - (float)i / steps;
    ledcWrite(CH_SPK, (int)(startDuty * k * k + 0.5f));
    delay(20);
  }
  ledcWrite(CH_SPK, 0);
}
void chimeStart() { bell(880, 900); bell(1175, 1200); }
void chimeEnd()   { bell(1175, 900); bell(880, 900); bell(659, 1600); }

// ---------------- 主頁 ----------------
Btn btnPat[3]  = { {8, 52, 96, 44}, {112, 52, 96, 44}, {216, 52, 96, 44} };
Btn btnLen[3]  = { {8, 126, 96, 36}, {112, 126, 96, 36}, {216, 126, 96, 36} };
Btn btnVol     = { 8, 180, 72, 50 };
Btn btnLed     = { 88, 180, 72, 50 };
Btn btnStart   = { 168, 180, 144, 50 };

void drawHome() {
  screen = HOME;
  setLed(0, 0, 0);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_TEXT);
  tft.drawString("Mindful Breathing", 160, 6, 4);
  tft.setTextColor(0x8C51);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Pattern", 10, 36, 2);
  tft.drawString("Session length", 10, 110, 2);
  for (int i = 0; i < 3 && i < PATTERN_COUNT; i++) drawBtn(btnPat[i], PATTERNS[i].label, i == patternIdx);
  char buf[12];
  for (int i = 0; i < 3 && i < SESSION_COUNT; i++) {
    snprintf(buf, sizeof(buf), "%d min", SESSION_MINUTES[i]);
    drawBtn(btnLen[i], buf, i == sessionIdx);
  }
  const char *VOL[] = { "Sound OFF", "Sound LOW", "Sound MED" };
  drawBtn(btnVol, VOL[volumeLvl], volumeLvl > 0, COL_HOLD);
  drawBtn(btnLed, ledOn ? "LED ON" : "LED OFF", ledOn, COL_HOLD);
  tft.fillRoundRect(btnStart.x, btnStart.y, btnStart.w, btnStart.h, 12, COL_EXHALE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("START", btnStart.x + btnStart.w / 2, btnStart.y + btnStart.h / 2, 4);
}

void handleHome(int x, int y) {
  for (int i = 0; i < 3 && i < PATTERN_COUNT; i++) if (inBtn(btnPat[i], x, y)) { patternIdx = i; drawHome(); return; }
  for (int i = 0; i < 3 && i < SESSION_COUNT; i++) if (inBtn(btnLen[i], x, y)) { sessionIdx = i; drawHome(); return; }
  if (inBtn(btnVol, x, y)) { volumeLvl = (volumeLvl + 1) % 3; drawHome(); bell(880, 500); return; }
  if (inBtn(btnLed, x, y)) { ledOn = !ledOn; drawHome(); if (ledOn) { setLed(40, 40, 40); delay(300); setLed(0, 0, 0); } return; }
  if (inBtn(btnStart, x, y)) startReady();
}

// ---------------- 準備 ----------------
void startReady() {
  screen = READY;
  readyStartMs = millis();
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT);
  tft.drawString("Sit comfortably", 160, 90, 4);
  tft.setTextColor(0x8C51);
  tft.drawString("Relax your shoulders, soften your gaze", 160, 130, 2);
  chimeStart();
  readyStartMs = millis();
}

// ---------------- 呼吸練習 ----------------
uint8_t phaseLen(Phase p) {
  const Pattern &pt = PATTERNS[patternIdx];
  switch (p) {
    case PH_INHALE: return pt.inhale;
    case PH_HOLD1:  return pt.hold1;
    case PH_EXHALE: return pt.exhale;
    default:        return pt.hold2;
  }
}
Phase nextPhase(Phase p) {
  // 跳過長度 0 嘅階段
  for (int i = 0; i < 4; i++) {
    p = (Phase)((p + 1) % 4);
    if (phaseLen(p) > 0) return p;
  }
  return PH_INHALE;
}
uint16_t phaseColor(Phase p) {
  if (p == PH_INHALE) return COL_INHALE;
  if (p == PH_EXHALE) return COL_EXHALE;
  return COL_HOLD;
}

void drawBreathFrame() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(0x8C51, COL_BG);
  tft.drawString(PATTERNS[patternIdx].name, 8, 6, 2);
  tft.setTextDatum(BC_DATUM);
  tft.drawString("Tap screen to pause", 160, 236, 2);
  lastShownSec = -1;
}

void startBreath() {
  screen = BREATH;
  sessionMs = (uint32_t)SESSION_MINUTES[sessionIdx] * 60000UL;
  elapsedMs = 0;
  phaseStartMs = 0;
  phase = PH_INHALE;
  breaths = 0;
  timeUp = false;
  lastTickMs = millis();
  drawBreathFrame();
}

void drawTimer() {
  int remain = timeUp ? 0 : (int)((sessionMs - elapsedMs + 999) / 1000);
  if (remain == lastShownSec) return;   // 每秒先重畫一次（局部更新）
  lastShownSec = remain;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d", remain / 60, remain % 60);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextPadding(70);
  tft.drawString(buf, 312, 4, 4);
  tft.setTextPadding(0);
  if (timeUp) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COL_HOLD, COL_BG);
    tft.drawString("last breath...", 160, 6, 2);
  }
}

void updateBreath() {
  uint32_t now = millis();
  elapsedMs += now - lastTickMs;
  lastTickMs = now;
  if (!timeUp && elapsedMs >= sessionMs) timeUp = true;

  // 階段切換
  uint32_t lenMs = phaseLen(phase) * 1000UL;
  while (elapsedMs - phaseStartMs >= lenMs) {
    phaseStartMs += lenMs;
    if (phase == PH_EXHALE) {
      breaths++;
      // 時間到：等呢一啖氣呼完先完結
      if (timeUp) { showDone(); return; }
    }
    phase = nextPhase(phase);
    if (timeUp && phase == PH_HOLD2) { showDone(); return; }
    lenMs = phaseLen(phase) * 1000UL;
  }

  float t = (float)(elapsedMs - phaseStartMs) / lenMs;   // 0 → 1
  float ease = (1.0f - cosf(PI * t)) * 0.5f;              // 慢-快-慢，似真呼吸
  float k;                                                // 0 = 最細, 1 = 最大
  switch (phase) {
    case PH_INHALE: k = ease; break;
    case PH_HOLD1:  k = 1.0f; break;
    case PH_EXHALE: k = 1.0f - ease; break;
    default:        k = 0.0f; break;
  }
  int r = R_MIN + (int)((R_MAX - R_MIN) * k);

  // 顏色喺階段開頭 0.6 秒內由上一個顏色慢慢過渡
  uint16_t col = phaseColor(phase);
  float sinceMs = elapsedMs - phaseStartMs;
  if (sinceMs < 600) {
    Phase prev = phase;
    for (int i = 0; i < 3; i++) prev = (Phase)((prev + 3) % 4), i = (phaseLen(prev) > 0) ? 3 : i;
    col = tft.alphaBlend((uint8_t)(255 * sinceMs / 600), col, phaseColor(prev));
  }

  // ---- 畫 sprite ----
  int cx = SPR_W / 2, cy = SPR_H / 2;
  spr.fillSprite(COL_BG);
  spr.drawCircle(cx, cy, R_MAX + 4, 0x2945);                 // 外圈導引線
  spr.drawCircle(cx, cy, R_MIN - 2, 0x2945);                 // 內圈導引線
  spr.fillCircle(cx, cy, r + 6, tft.alphaBlend(60, col, COL_BG));   // 光暈
  spr.fillCircle(cx, cy, r + 3, tft.alphaBlend(120, col, COL_BG));
  spr.fillCircle(cx, cy, r, col);
  // 停 (Hold) 時加一個慢慢擴散嘅波紋
  if (phase == PH_HOLD1 || phase == PH_HOLD2) {
    int rr = r + 4 + (int)(10 * fmodf(sinceMs / 1000.0f, 1.0f));
    if (rr < SPR_W / 2) spr.drawCircle(cx, cy, rr, tft.alphaBlend(140, col, COL_BG));
  }
  // 階段文字 + 秒數
  int secLeft = (int)((lenMs - (elapsedMs - phaseStartMs) + 999) / 1000);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(r > 50 ? TFT_BLACK : COL_TEXT);
  spr.drawString(PHASE_TEXT[phase], cx, cy - 12, 4);
  spr.drawNumber(secLeft, cx, cy + 16, 4);
  spr.pushSprite(SPR_X, SPR_Y);

  ledFrom565(col, 0.15f + 0.85f * k);
  drawTimer();
}

// ---------------- 暫停 ----------------
Btn btnResume = { 30, 140, 120, 50 };
Btn btnHome   = { 170, 140, 120, 50 };

void showPaused() {
  screen = PAUSED;
  setLed(0, 0, 0);
  tft.fillRoundRect(20, 50, 280, 155, 14, COL_BTN);
  tft.drawRoundRect(20, 50, 280, 155, 14, COL_HOLD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT);
  tft.drawString("Paused", 160, 80, 4);
  tft.setTextColor(0x8C51);
  tft.drawString("Take your time", 160, 110, 2);
  drawBtn(btnResume, "Resume", true, COL_EXHALE);
  drawBtn(btnHome, "Home", false);
}

// ---------------- 完成 ----------------
const char *MESSAGES[] = {
  "Well done. Carry this calm with you.",
  "Notice how you feel right now.",
  "Be kind to yourself today.",
  "Your breath is always here for you.",
};

void showDone() {
  screen = DONE;
  setLed(0, 0, 0);
  tft.fillScreen(COL_BG);
  tft.fillCircle(160, 62, 30, tft.alphaBlend(120, COL_EXHALE, COL_BG));
  tft.fillCircle(160, 62, 22, COL_EXHALE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT);
  tft.drawString("Session complete", 160, 120, 4);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d min  |  %d breaths", SESSION_MINUTES[sessionIdx], breaths);
  tft.setTextColor(COL_INHALE);
  tft.drawString(buf, 160, 152, 2);
  tft.setTextColor(0xBDF7);
  tft.drawString(MESSAGES[random(sizeof(MESSAGES) / sizeof(MESSAGES[0]))], 160, 180, 2);
  tft.setTextColor(0x8C51);
  tft.drawString("Tap to return home", 160, 222, 2);
  // 完結時 LED 慢慢光一下再熄
  for (int i = 0; i <= 20; i++) { ledFrom565(COL_EXHALE, i / 20.0f); delay(15); }
  chimeEnd();
  for (int i = 20; i >= 0; i--) { ledFrom565(COL_EXHALE, i / 20.0f); delay(30); }
}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);

  // RGB LED PWM（core 2.x API）
  ledcSetup(CH_R, 5000, 8); ledcAttachPin(LED_R, CH_R);
  ledcSetup(CH_G, 5000, 8); ledcAttachPin(LED_G, CH_G);
  ledcSetup(CH_B, 5000, 8); ledcAttachPin(LED_B, CH_B);
  setLed(0, 0, 0);
  // 喇叭
  ledcSetup(CH_SPK, 1000, 10);
  ledcAttachPin(SPEAKER_PIN, CH_SPK);
  ledcWrite(CH_SPK, 0);

  tft.init();
  tft.setRotation(1);            // 橫向 320x240

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  spr.setColorDepth(8);          // 8-bit 慳 RAM（184x184 ≈ 34KB）
  if (!spr.createSprite(SPR_W, SPR_H)) Serial.println("Sprite alloc failed!");

  randomSeed(esp_random());
  drawHome();
}

void loop() {
  int x, y;
  bool touched = getTouch(x, y);
  if (touched) Serial.printf("touch x=%d y=%d\n", x, y);

  switch (screen) {
    case HOME:
      if (touched) handleHome(x, y);
      break;
    case READY:
      if (millis() - readyStartMs > 2500) startBreath();
      break;
    case BREATH:
      if (touched) { showPaused(); break; }
      updateBreath();
      delay(16);                 // 大約 30-50 fps
      break;
    case PAUSED:
      if (touched) {
        if (inBtn(btnResume, x, y)) { drawBreathFrame(); lastTickMs = millis(); screen = BREATH; }
        else if (inBtn(btnHome, x, y)) drawHome();
      }
      break;
    case DONE:
      if (touched) drawHome();
      break;
  }
}
