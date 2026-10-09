/*
  06_CYD電子琴 CYD Synth Keyboard  (CYD ESP32-2432S028R)
  ------------------------------------------------------------
  靈感來自 CYD Facebook 群組（CYD - Cheap Yellow Display ESP32）Fred Cabor 嘅
  「Electronic keyboard from a cheap yellow display」：
  https://www.facebook.com/groups/8104937882859357/posts/29365162043076966/
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅 code。

  功能：
   - 觸控琴鍵：1 個八度（8 個白鍵）或 2 個八度（15 個白鍵）可切換
   - Octave - / + 掣（C2 至 C6）
   - 5 種音色：Sine 正弦、Square 方波、Saw 鋸齒、Triangle 三角、Organ 風琴
   - 有 Attack / Release 包絡，聲音唔會「咔」一聲
   - 撳住嘅琴鍵會變色，屏幕顯示音名同頻率；手指喺琴鍵上滑可以連續轉音
   - DEMO 掣：自動彈《小星星》，琴鍵會跟住亮
   - 聲音由 CYD 板上 SPEAK 插座輸出（GPIO26 → 板載功放 → 8Ω 喇叭，JST 1.25mm 2-pin）

  原理：ESP32 硬件 timer 每秒 20000 次觸發中斷，由波形表 (wavetable) 攞一個數值，
        寫入 GPIO26 內置 DAC（8-bit），經板上功放推喇叭。

  Library：TFT_eSPI（已設定 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <driver/dac.h>
#include <soc/rtc_io_reg.h>
#include <soc/sens_reg.h>

// ====== 改呢度 ======
#define SAMPLE_RATE   20000   // 每秒幾多個 sample
#define START_OCTAVE  4       // 開機時最左邊 C 係 C4（中央 C）
#define TWO_OCTAVES   false   // 開機用 1 個定 2 個八度
#define ATTACK_MS     8       // 起音時間
#define RELEASE_MS    180     // 放手後聲音慢慢消失時間
int volumeLvl = 2;            // 1 = 細, 2 = 中, 3 = 大

// 觸控校正
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// ====== 硬件腳位 ======
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21
#define LED_R 4
#define LED_G 16
#define LED_B 17
// 喇叭：GPIO26 = DAC 通道 2（CYD 板上 SPEAK 插座）

// ====== 顏色 ======
#define C_BG     0x10A2
#define C_PANEL  0x2124
#define C_BTN    0x3186
#define C_ACCENT 0xFD20   // 橙
#define C_PRESS  0x2E9C   // 撳住琴鍵顏色
#define C_TEXT   0xDEFB

struct Btn { int x, y, w, h; const char *label; };   // 觸控掣範圍

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite keys = TFT_eSprite(&tft);   // 琴鍵區用 sprite，唔閃

// ================== 合成器（中斷入面運行） ==================
int8_t wavetables[5][256];
const char *WAVE_NAMES[] = { "Sine", "Square", "Saw", "Triangle", "Organ" };
int waveIdx = 0;

volatile const int8_t *curWave = wavetables[0];
volatile uint32_t phase = 0, phaseInc = 0;
volatile int32_t  env = 0;          // 0..65535 音量包絡
volatile int32_t  envTarget = 0;
volatile int32_t  envAttackStep, envReleaseStep;
volatile int32_t  volMul = 2;

hw_timer_t *timer = nullptr;

// 直接寫 DAC2 寄存器（IRAM 安全、好快）
static inline void IRAM_ATTR dac2Write(uint8_t v) {
  SET_PERI_REG_BITS(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_DAC, v, RTC_IO_PDAC2_DAC_S);
}

void IRAM_ATTR onTimer() {
  // 包絡：慢慢升到目標 / 慢慢跌返 0
  if (env < envTarget) { env += envAttackStep; if (env > envTarget) env = envTarget; }
  else if (env > envTarget) { env -= envReleaseStep; if (env < envTarget) env = envTarget; }
  if (env == 0) { dac2Write(128); return; }
  phase += phaseInc;
  int32_t s = curWave[phase >> 24];                 // -127..127
  s = (s * (env >> 8)) >> 8;                        // 乘包絡
  s = s * volMul / 3;                               // 乘音量
  dac2Write((uint8_t)(128 + s));
}

void buildWavetables() {
  for (int i = 0; i < 256; i++) {
    float t = i / 256.0f;
    float sine = sinf(2 * PI * t);
    wavetables[0][i] = (int8_t)(127 * sine);
    wavetables[1][i] = (i < 128) ? 90 : -90;                        // 方波（細聲啲，因為好「嘈」）
    wavetables[2][i] = (int8_t)(100 * (2 * t - 1));                 // 鋸齒
    wavetables[3][i] = (int8_t)(127 * (t < 0.5f ? 4 * t - 1 : 3 - 4 * t)); // 三角
    float organ = sine + 0.5f * sinf(4 * PI * t) + 0.25f * sinf(6 * PI * t) + 0.12f * sinf(8 * PI * t);
    wavetables[4][i] = (int8_t)(127 * organ / 1.87f);               // 風琴：加泛音
  }
}

void noteOn(float freq) {
  phaseInc = (uint32_t)(freq * 4294967296.0 / SAMPLE_RATE);
  envTarget = 65535;
}
void noteOff() { envTarget = 0; }

// ================== 音名 / 頻率 ==================
const char *NOTE_NAMES[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
float midiToFreq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

// ================== 琴鍵排版 ==================
#define KEY_Y   96            // 琴鍵區喺屏幕嘅 y
#define KEY_H   144
#define BLACK_H 88
int octave = START_OCTAVE;
bool twoOct = TWO_OCTAVES;
int numWhite() { return twoOct ? 15 : 8; }
const int WHITE_SEMI[7] = { 0, 2, 4, 5, 7, 9, 11 };  // C D E F G A B 相對 C 嘅半音
int pressedMidi = -1;

int baseMidi() { return 12 * (octave + 1); }         // C4 = 60
// 白鍵 i 嘅 MIDI 音
int whiteMidi(int i) { return baseMidi() + 12 * (i / 7) + WHITE_SEMI[i % 7]; }
// 白鍵 i 右邊有冇黑鍵（E 同 B 後面冇）
bool hasBlackAfter(int i) { int k = i % 7; return (k != 2 && k != 6) && i < numWhite() - 1; }

void drawKeys() {
  int nw = numWhite();
  float ww = 320.0f / nw;
  keys.fillSprite(TFT_BLACK);
  for (int i = 0; i < nw; i++) {
    int x = (int)(i * ww);
    int w = (int)((i + 1) * ww) - x;
    int m = whiteMidi(i);
    keys.fillRoundRect(x + 1, 0, w - 2, KEY_H - 2, 4, m == pressedMidi ? C_PRESS : TFT_WHITE);
    keys.setTextDatum(BC_DATUM);
    keys.setTextColor(m == pressedMidi ? TFT_WHITE : 0x7BEF);
    char lbl[5];
    if (i % 7 == 0) snprintf(lbl, sizeof(lbl), "C%d", m / 12 - 1);
    else snprintf(lbl, sizeof(lbl), "%s", NOTE_NAMES[m % 12]);
    keys.drawString(lbl, x + w / 2, KEY_H - 6, twoOct ? 1 : 2);
  }
  for (int i = 0; i < nw; i++) {
    if (!hasBlackAfter(i)) continue;
    int bw = (int)(ww * 0.6f);
    int x = (int)((i + 1) * ww) - bw / 2;
    int m = whiteMidi(i) + 1;
    keys.fillRoundRect(x, 0, bw, BLACK_H, 3, m == pressedMidi ? C_PRESS : 0x18E3);
  }
  keys.pushSprite(0, KEY_Y);
}

// 觸控位置 → MIDI 音（-1 = 唔係琴鍵）
int keyAt(int x, int y) {
  if (y < KEY_Y) return -1;
  int ky = y - KEY_Y;
  int nw = numWhite();
  float ww = 320.0f / nw;
  if (ky < BLACK_H) {                       // 先檢查黑鍵（喺白鍵上面）
    for (int i = 0; i < nw; i++) {
      if (!hasBlackAfter(i)) continue;
      int bw = (int)(ww * 0.6f);
      int bx = (int)((i + 1) * ww) - bw / 2;
      if (x >= bx && x < bx + bw) return whiteMidi(i) + 1;
    }
  }
  int i = constrain((int)(x / ww), 0, nw - 1);
  return whiteMidi(i);
}

// ================== 上方控制區 ==================
Btn bOctDn = { 4, 54, 50, 36, "OCT-" };
Btn bOctUp = { 58, 54, 50, 36, "OCT+" };
Btn bWave  = { 112, 54, 80, 36, "" };
Btn bKeys  = { 196, 54, 40, 36, "" };
Btn bVol   = { 240, 54, 36, 36, "" };
Btn bDemo  = { 280, 54, 36, 36, "DEMO" };
bool inBtn(const Btn &b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

void drawBtn(const Btn &b, const char *txt, uint16_t col = C_BTN) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, col);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, col);
  tft.drawString(txt, b.x + b.w / 2, b.y + b.h / 2, b.w < 45 ? 1 : 2);
}

void drawControls() {
  char buf[16];
  drawBtn(bOctDn, bOctDn.label);
  drawBtn(bOctUp, bOctUp.label);
  drawBtn(bWave, WAVE_NAMES[waveIdx], 0x6118);
  drawBtn(bKeys, twoOct ? "2 OCT" : "1 OCT");
  snprintf(buf, sizeof(buf), "VOL %d", volumeLvl);
  drawBtn(bVol, buf);
  drawBtn(bDemo, bDemo.label, 0x0451);
}

// 顯示音名同頻率
void drawNoteDisplay() {
  tft.fillRect(130, 4, 186, 46, C_PANEL);
  tft.setTextDatum(ML_DATUM);
  if (pressedMidi < 0) {
    tft.setTextColor(0x7BEF, C_PANEL);
    char buf[24]; snprintf(buf, sizeof(buf), "C%d - C%d", octave, octave + (twoOct ? 2 : 1));
    tft.drawString(buf, 140, 27, 2);
    return;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%s%d", NOTE_NAMES[pressedMidi % 12], pressedMidi / 12 - 1);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString(buf, 140, 27, 4);
  snprintf(buf, sizeof(buf), "%.1f Hz", midiToFreq(pressedMidi));
  tft.setTextColor(C_TEXT, C_PANEL);
  tft.drawString(buf, 216, 27, 2);
}

void drawAll() {
  tft.fillScreen(C_BG);
  tft.fillRoundRect(4, 4, 312, 46, 8, C_PANEL);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(C_ACCENT, C_PANEL);
  tft.drawString("CYD SYNTH", 12, 27, 4);
  drawNoteDisplay();
  drawControls();
  drawKeys();
}

// RGB LED：彈琴時按音高轉顏色（active LOW）
void ledForNote(int m) {
  if (m < 0) { digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH); return; }
  int k = m % 3;
  digitalWrite(LED_R, k == 0 ? LOW : HIGH);
  digitalWrite(LED_G, k == 1 ? LOW : HIGH);
  digitalWrite(LED_B, k == 2 ? LOW : HIGH);
}

void setPressed(int m) {
  if (m == pressedMidi) return;
  pressedMidi = m;
  if (m >= 0) noteOn(midiToFreq(m)); else noteOff();
  ledForNote(m);
  drawKeys();
  drawNoteDisplay();
}

// ================== DEMO：小星星 ==================
// 相對 C 嘅半音（-1 = 停一停），每格一拍
const int8_t TWINKLE[] = { 0,0,7,7,9,9,7,-1, 5,5,4,4,2,2,0,-1, 7,7,5,5,4,4,2,-1, 7,7,5,5,4,4,2,-1, 0,0,7,7,9,9,7,-1, 5,5,4,4,2,2,0,-1 };

bool touchedNow() { return ts.tirqTouched() && ts.touched(); }

void playDemo() {
  drawBtn(bDemo, "STOP", 0xA000);
  while (touchedNow()) delay(10);
  for (unsigned i = 0; i < sizeof(TWINKLE); i++) {
    if (touchedNow()) break;                     // 撳屏停止
    if (TWINKLE[i] >= 0) setPressed(baseMidi() + TWINKLE[i]);
    else setPressed(-1);
    delay(300);
    setPressed(-1);
    delay(60);
  }
  setPressed(-1);
  while (touchedNow()) delay(10);
  drawControls();
}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  ledForNote(-1);

  buildWavetables();
  // 包絡每個 sample 行幾多步
  envAttackStep  = max(1, 65535 / (SAMPLE_RATE * ATTACK_MS / 1000));
  envReleaseStep = max(1, 65535 / (SAMPLE_RATE * RELEASE_MS / 1000));
  volMul = volumeLvl;

  // DAC2 = GPIO26（CYD SPEAK 插座）
  dac_output_enable(DAC_CHANNEL_2);
  dac_output_voltage(DAC_CHANNEL_2, 128);

  // 硬件 timer：80MHz / 80 = 1MHz，每 50 tick = 20kHz
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);
  timerAlarmEnable(timer);

  tft.init();
  tft.setRotation(1);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  keys.setColorDepth(8);
  if (!keys.createSprite(320, KEY_H)) Serial.println("Sprite alloc failed!");
  drawAll();
}

bool wasTouching = false;

void loop() {
  if (touchedNow()) {
    TS_Point p = ts.getPoint();
    int x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, 320), 0, 319);
    int y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, 240), 0, 239);
    int m = keyAt(x, y);
    if (m >= 0) { setPressed(m); wasTouching = true; }
    else if (!wasTouching) {                     // 控制掣：一撳一次
      wasTouching = true;
      Serial.printf("touch x=%d y=%d\n", x, y);
      if (inBtn(bOctDn, x, y) && octave > 2) { octave--; drawAll(); }
      else if (inBtn(bOctUp, x, y) && octave < (twoOct ? 4 : 5)) { octave++; drawAll(); }
      else if (inBtn(bWave, x, y)) { waveIdx = (waveIdx + 1) % 5; curWave = wavetables[waveIdx]; drawControls(); }
      else if (inBtn(bKeys, x, y)) { twoOct = !twoOct; if (twoOct && octave > 4) octave = 4; drawAll(); }
      else if (inBtn(bVol, x, y)) { volumeLvl = volumeLvl % 3 + 1; volMul = volumeLvl; drawControls(); }
      else if (inBtn(bDemo, x, y)) { playDemo(); wasTouching = false; }
    } else if (pressedMidi >= 0) setPressed(-1); // 手指滑出琴鍵
  } else {
    if (wasTouching) { setPressed(-1); wasTouching = false; }
  }
  delay(5);
}
