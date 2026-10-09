/*
  CYD Dino Run 恐龍遊戲   — CYD ESP32-2432S028R
  ------------------------------------------------------------
  學造工作室 Maker 作品：仿 Chrome 斷網時嗰隻「T-Rex 恐龍跳仙人掌」遊戲。
  單一 .ino 檔，唔使其他 config.h。

  玩法：
   - 撳屏幕任何位置（或者撳板上 BOOT 掣）= 跳
   - 按住右下角「DUCK」框 = 伏低（避開中間高度嘅翼龍）；喺半空按住 = 快啲落地
   - 分數越高跑得越快；300 分後有翼龍；每 700 分日夜交替
   - 最高分會儲存喺 flash（Preferences），熄機都唔會唔見
   - 標題 / Game Over 畫面撳左上角 SOUND 掣可以開關聲

  Library：TFT_eSPI（User_Setup.h 要設定做 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module（ESP32 core 2.0.17）
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

// ================== 可以改呢度（遊戲設定） ==================
#define GRAVITY        0.75f   // 重力：越大跌得越快
#define JUMP_SPEED     11.5f   // 起跳速度：越大跳得越高
#define FAST_FALL      1.8f    // 半空按住 DUCK 時額外重力
#define START_SPEED    4.0f    // 開始速度（每步移動幾多像素）
#define MAX_SPEED      12.0f   // 最高速度
#define ACCEL          0.0008f // 每步加速幾多
#define GAP_FACTOR     34      // 障礙物最少距離 = 速度 x 呢個數 + 60
#define PTERO_SCORE    300     // 幾多分之後出翼龍
#define NIGHT_EVERY    700     // 每幾多分日夜交替（0 = 唔轉）
#define SCORE_RATE     0.04f   // 分數增加速度
#define STEP_MS        16      // 物理更新間隔（約 60 次 / 秒）
#define SOUND_DUTY     8       // 聲量（10-bit duty，最大 512；細 = 細聲）

// 顏色（RGB565）
#define COL_DAY_BG     0xF7BE  // 日間背景（淺灰白）
#define COL_DAY_FG     0x52AA  // 日間前景（深灰：恐龍、仙人掌、地面）
#define COL_DAY_CLOUD  0xC618
#define COL_NIGHT_BG   0x0000  // 夜間背景
#define COL_NIGHT_FG   0xD69A  // 夜間前景
#define COL_NIGHT_CLOUD 0x4208
#define COL_DINO_DAY   0x52AA  // 想要綠色恐龍可以改做 0x2C45

// 觸控校正（撳唔準就打開 Serial Monitor 睇 touch x/y 再改）
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
#define BOOT_BTN     0     // 板上 BOOT 掣，撳落 = LOW
#define SPEAKER_PIN  26
#define CH_SPK       0     // LEDC 通道
#define LED_R 4
#define LED_G 16
#define LED_B 17

// ================== 像素畫（X = 有顏色，. = 透明） ==================
// 每粒「像素」會放大 SCALE 倍畫上屏幕；改呢度就可以畫你自己嘅角色！
#define SCALE 2

const char *const DINO_RUN1[] = {   // 20 x 18
  "..........XXXXXXXX..",
  ".........XX.XXXXXXX.",
  ".........XXXXXXXXXX.",
  ".........XXXXXXXXXX.",
  ".........XXXXX......",
  ".........XXXXXXXX...",
  "X.......XXXXX.......",
  "X......XXXXXX.......",
  "XX...XXXXXXXXXXX....",
  "XXX.XXXXXXXXX..X....",
  "XXXXXXXXXXXXX.......",
  "XXXXXXXXXXXXX.......",
  ".XXXXXXXXXXX........",
  "..XXXXXXXXX.........",
  "...XXXXXXXX.........",
  "....XXX..XX.........",
  "....XX....X.........",
  "....X.....XX........",
};
const char *const DINO_RUN2[] = {
  "..........XXXXXXXX..",
  ".........XX.XXXXXXX.",
  ".........XXXXXXXXXX.",
  ".........XXXXXXXXXX.",
  ".........XXXXX......",
  ".........XXXXXXXX...",
  "X.......XXXXX.......",
  "X......XXXXXX.......",
  "XX...XXXXXXXXXXX....",
  "XXX.XXXXXXXXX..X....",
  "XXXXXXXXXXXXX.......",
  "XXXXXXXXXXXXX.......",
  ".XXXXXXXXXXX........",
  "..XXXXXXXXX.........",
  "...XXXXXXXX.........",
  "....XXX..XX.........",
  ".....XX...X.........",
  ".........XX.........",
};
const char *const DINO_STAND[] = {
  "..........XXXXXXXX..",
  ".........XX.XXXXXXX.",
  ".........XXXXXXXXXX.",
  ".........XXXXXXXXXX.",
  ".........XXXXX......",
  ".........XXXXXXXX...",
  "X.......XXXXX.......",
  "X......XXXXXX.......",
  "XX...XXXXXXXXXXX....",
  "XXX.XXXXXXXXX..X....",
  "XXXXXXXXXXXXX.......",
  "XXXXXXXXXXXXX.......",
  ".XXXXXXXXXXX........",
  "..XXXXXXXXX.........",
  "...XXXXXXXX.........",
  "....XXX..XX.........",
  "....XX...XX.........",
  "....XXX..XXX........",
};
const char *const DINO_DUCK1[] = {  // 26 x 8
  "X.................XXXXXXX.",
  "XX...XXXXXXXXXXXXXX.XXXXXX",
  "XXXXXXXXXXXXXXXXXXXXXXXXXX",
  ".XXXXXXXXXXXXXXXXXXXXXXXXX",
  "..XXXXXXXXXXXXXXXXXXXX....",
  "...XXXXXXXXXXXXX.XXXXXX...",
  "....XXX..XX.......X.......",
  "....X.....XX..............",
};
const char *const DINO_DUCK2[] = {
  "X.................XXXXXXX.",
  "XX...XXXXXXXXXXXXXX.XXXXXX",
  "XXXXXXXXXXXXXXXXXXXXXXXXXX",
  ".XXXXXXXXXXXXXXXXXXXXXXXXX",
  "..XXXXXXXXXXXXXXXXXXXX....",
  "...XXXXXXXXXXXXX.XXXXXX...",
  "....XXX..XX.......X.......",
  ".....XX...X...............",
};
const char *const CACTUS_S[] = {    // 8 x 16
  "...XX...",
  "..XXXX..",
  "..XXXX..",
  "X.XXXX..",
  "X.XXXX.X",
  "X.XXXX.X",
  "XXXXXX.X",
  ".XXXXXXX",
  "..XXXXX.",
  "..XXXX..",
  "..XXXX..",
  "..XXXX..",
  "..XXXX..",
  "..XXXX..",
  "..XXXX..",
  "..XXXX..",
};
const char *const CACTUS_L[] = {    // 12 x 22
  ".....XX.....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "XX..XXXX....",
  "XX..XXXX..XX",
  "XX..XXXX..XX",
  "XX..XXXX..XX",
  "XX..XXXX..XX",
  "XXXXXXXX..XX",
  ".XXXXXXX..XX",
  "....XXXXXXXX",
  "....XXXXXXX.",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
  "....XXXX....",
};
const char *const PTERO1[] = {      // 18 x 11（翼向上）
  "......X...........",
  "......XX..........",
  "..X...XXX.........",
  ".XX...XXXX........",
  "XXXX..XXXXX.......",
  "XXXXXXXXXXXXXXXX..",
  "......XXXXXXXXXXXX",
  "........XXXXXXX...",
  "..................",
  "..................",
  "..................",
};
const char *const PTERO2[] = {      // 翼向下
  "..................",
  "..................",
  "..X...............",
  ".XX...............",
  "XXXX..............",
  "XXXXXXXXXXXXXXXX..",
  "......XXXXXXXXXXXX",
  "......XXXXXXX.....",
  "......XXXX........",
  "......XX..........",
  "......X...........",
};
const char *const CLOUD[] = {       // 23 x 6
  "..........XXXX.........",
  "........XX....XX.......",
  "....XXXX........XXX....",
  "..XX...............XX..",
  ".X...................X.",
  "XXXXXXXXXXXXXXXXXXXXXXX",
};
#define ART_ROWS(a) (int)(sizeof(a) / sizeof(a[0]))

// ================== 畫面配置 ==================
#define PLAY_Y   32               // 遊戲 sprite 喺屏幕嘅 y 位置
#define PLAY_W   320
#define PLAY_H   170
#define GROUND_Y 150              // 地面（腳底）喺 sprite 入面嘅 y
#define DINO_X   24
#define DINO_H   (18 * SCALE)
#define DUCK_H   (8 * SCALE)
#define DUCK_ZONE_X 200           // 右下角伏低區（屏幕座標）
#define DUCK_ZONE_Y 190

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);
Preferences prefs;

struct Box { int x, y, w, h; };   // 碰撞用嘅長方形

enum ObType { OB_CACTUS_S, OB_CACTUS_L, OB_PTERO };
struct Obstacle { bool active; ObType type; float x; int y, w, h, count; };
#define MAX_OB 4
Obstacle obs[MAX_OB];

struct Cloud { float x; int y; };
Cloud clouds[3];
float pebbles[24]; int pebbleY[24]; int pebbleW[24];

enum State { TITLE, PLAYING, GAMEOVER };
State state = TITLE;

// 遊戲變數
float dinoY, dinoVy, speed, score;
bool  onGround, ducking, night;
int   hiScore = 0, animTick = 0, flashTimer = 0, lastMilestone = 0;
float nextGap;
bool  muted = false;
uint32_t lastMs, accMs, gameOverMs;
uint16_t colBg, colFg, colCloud, colDino;

// ---------------- 聲音（唔會阻住遊戲） ----------------
uint32_t toneEnd = 0; int queuedFreq = 0, queuedMs = 0;
void beep(int freq, int ms) {
  if (muted) return;
  ledcWriteTone(CH_SPK, freq);
  ledcWrite(CH_SPK, SOUND_DUTY);
  toneEnd = millis() + ms;
}
void beep2(int f1, int ms1, int f2, int ms2) { beep(f1, ms1); queuedFreq = f2; queuedMs = ms2; }
void updateSound() {
  if (toneEnd && (int32_t)(millis() - toneEnd) >= 0) {
    ledcWrite(CH_SPK, 0); toneEnd = 0;
    if (queuedFreq) { int f = queuedFreq; queuedFreq = 0; beep(f, queuedMs); }
  }
}

// ---------------- 畫像素畫 ----------------
// 逐行搵連續嘅 X，一次過 fillRect，快好多
void drawArt(const char *const *art, int rows, int x, int y, uint16_t col) {
  for (int r = 0; r < rows; r++) {
    const char *line = art[r];
    int c = 0;
    while (line[c]) {
      if (line[c] == 'X') {
        int start = c;
        while (line[c] == 'X') c++;
        spr.fillRect(x + start * SCALE, y + r * SCALE, (c - start) * SCALE, SCALE, col);
      } else c++;
    }
  }
}
int artW(const char *const *art) { return strlen(art[0]) * SCALE; }

// ---------------- 碰撞 ----------------
bool hit(const Box &a, const Box &b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
// 恐龍用幾個細長方形，比一個大長方形公平（唔會「明明冇掂到都死」）
int dinoBoxes(Box *b) {
  int y = (int)dinoY;
  if (ducking && onGround) {
    b[0] = { DINO_X + 2, GROUND_Y - DUCK_H + 3, 48, 12 };
    return 1;
  }
  b[0] = { DINO_X + 19, y + 1, 17, 9 };    // 頭
  b[1] = { DINO_X + 3,  y + 13, 22, 17 };  // 身
  b[2] = { DINO_X + 8,  y + 30, 14, 6 };   // 腳
  return 3;
}
Box obstacleBox(const Obstacle &o) {
  if (o.type == OB_PTERO) return { (int)o.x + 2, o.y + 9, o.w - 4, 7 };  // 只計身，唔計翼
  return { (int)o.x + 4, o.y + 4, o.w - 8, o.h - 4 };
}

// ---------------- 顏色 / 日夜 ----------------
void setPalette() {
  colBg    = night ? COL_NIGHT_BG : COL_DAY_BG;
  colFg    = night ? COL_NIGHT_FG : COL_DAY_FG;
  colCloud = night ? COL_NIGHT_CLOUD : COL_DAY_CLOUD;
  colDino  = night ? COL_NIGHT_FG : COL_DINO_DAY;
}

// 上下兩條 UI 帶（唔喺 sprite 入面，只係轉畫面時先重畫）
void drawStrips() {
  tft.fillRect(0, 0, 320, PLAY_Y, colBg);
  tft.fillRect(0, PLAY_Y + PLAY_H, 320, 240 - PLAY_Y - PLAY_H, colBg);
  // SOUND 掣（左上）
  tft.drawRoundRect(4, 4, 84, 24, 6, colFg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colFg, colBg);
  tft.drawString(muted ? "SOUND OFF" : "SOUND ON", 46, 16, 2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("DINO RUN", 314, 16, 2);
  // 下面提示
  tft.setTextDatum(ML_DATUM);
  tft.drawString("TAP / BOOT = JUMP", 8, 221, 2);
  for (int i = 216; i < 316; i += 6) { tft.drawFastHLine(i, 206, 3, colFg); tft.drawFastHLine(i, 236, 3, colFg); }
  for (int j = 206; j < 236; j += 6) { tft.drawFastVLine(216, j, 3, colFg); tft.drawFastVLine(315, j, 3, colFg); }
  tft.setTextDatum(MC_DATUM);
  tft.drawString("HOLD = DUCK", 266, 221, 2);
}

// ---------------- 遊戲初始化 ----------------
void resetGame() {
  dinoY = GROUND_Y - DINO_H; dinoVy = 0; onGround = true; ducking = false;
  speed = START_SPEED; score = 0; lastMilestone = 0; flashTimer = 0;
  night = false; setPalette();
  for (int i = 0; i < MAX_OB; i++) obs[i].active = false;
  nextGap = 200;
  for (int i = 0; i < 3; i++) clouds[i] = { (float)(80 + i * 110 + random(40)), 20 + (int)random(50) };
  for (int i = 0; i < 24; i++) { pebbles[i] = random(PLAY_W); pebbleY[i] = GROUND_Y + 2 + random(10); pebbleW[i] = 1 + random(4); }
}

void spawnObstacle() {
  int slot = -1;
  for (int i = 0; i < MAX_OB; i++) if (!obs[i].active) { slot = i; break; }
  if (slot < 0) return;
  Obstacle &o = obs[slot];
  o.active = true; o.x = PLAY_W;
  if (score >= PTERO_SCORE && random(100) < 25) {
    o.type = OB_PTERO; o.count = 1;
    o.w = artW(PTERO1); o.h = ART_ROWS(PTERO1) * SCALE;
    const int heights[3] = { GROUND_Y - 24, GROUND_Y - 44, GROUND_Y - 66 };  // 低：要跳｜中：要伏｜高：唔好跳
    o.y = heights[random(3)];
  } else if (random(100) < 55) {
    o.type = OB_CACTUS_S; o.count = 1 + random(speed > 6 ? 3 : 2);
    o.w = o.count * artW(CACTUS_S) + (o.count - 1) * 2; o.h = ART_ROWS(CACTUS_S) * SCALE;
    o.y = GROUND_Y - o.h;
  } else {
    o.type = OB_CACTUS_L; o.count = 1 + (speed > 7 ? random(2) : 0);
    o.w = o.count * artW(CACTUS_L) + (o.count - 1) * 2; o.h = ART_ROWS(CACTUS_L) * SCALE;
    o.y = GROUND_Y - o.h;
  }
  float minGap = speed * GAP_FACTOR + 60;
  nextGap = minGap + random((int)(minGap * 0.8f));
}

// ---------------- 輸入 ----------------
bool touchDown = false; int touchX, touchY;
void readInput() {
  touchDown = false;
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z > 200) {
      touchX = constrain(map(p.x, TS_MINX, TS_MAXX, 0, 320), 0, 319);
      touchY = constrain(map(p.y, TS_MINY, TS_MAXY, 0, 240), 0, 239);
      touchDown = true;
    }
  }
}
bool inDuckZone() { return touchDown && touchX >= DUCK_ZONE_X && touchY >= DUCK_ZONE_Y; }
bool jumpHeld()   { return (touchDown && !inDuckZone()) || digitalRead(BOOT_BTN) == LOW; }

// ---------------- 物理（每 STEP_MS 一次） ----------------
void step() {
  animTick++;
  bool wantDuck = inDuckZone();

  // 跳：喺地上先跳得
  if (onGround && jumpHeld() && !wantDuck) {
    dinoVy = -JUMP_SPEED; onGround = false;
    beep(620, 40);
  }
  ducking = wantDuck;

  // 重力：速度加重力，位置加速度
  if (!onGround) {
    dinoVy += GRAVITY + (wantDuck ? FAST_FALL : 0);
    dinoY += dinoVy;
    if (dinoY >= GROUND_Y - DINO_H) { dinoY = GROUND_Y - DINO_H; dinoVy = 0; onGround = true; }
  }

  // 加速 + 分數
  if (speed < MAX_SPEED) speed += ACCEL;
  score += speed * SCORE_RATE;
  int s = (int)score;
  if (s / 100 > lastMilestone) {             // 每 100 分「叮叮」一聲兼閃分數
    lastMilestone = s / 100; flashTimer = 60;
    beep2(1000, 60, 1500, 90);
  }
  if (flashTimer > 0) flashTimer--;
  if (NIGHT_EVERY > 0) {
    bool shouldNight = (s / NIGHT_EVERY) % 2 == 1;
    if (shouldNight != night) { night = shouldNight; setPalette(); drawStrips(); }
  }

  // 移動障礙物
  float rightmost = -1000;
  for (int i = 0; i < MAX_OB; i++) {
    Obstacle &o = obs[i];
    if (!o.active) continue;
    o.x -= speed + (o.type == OB_PTERO ? 0.6f : 0);
    if (o.x + o.w < 0) o.active = false;
    else if (o.x + o.w > rightmost) rightmost = o.x + o.w;
  }
  if (rightmost < PLAY_W - nextGap) spawnObstacle();

  // 雲同地面碎石
  for (int i = 0; i < 3; i++) {
    clouds[i].x -= speed * 0.2f;
    if (clouds[i].x < -60) { clouds[i].x = PLAY_W + random(80); clouds[i].y = 20 + random(50); }
  }
  for (int i = 0; i < 24; i++) {
    pebbles[i] -= speed;
    if (pebbles[i] < -5) { pebbles[i] += PLAY_W + 5; pebbleY[i] = GROUND_Y + 2 + random(10); }
  }

  // 碰撞檢查
  Box db[3]; int n = dinoBoxes(db);
  for (int i = 0; i < MAX_OB; i++) {
    if (!obs[i].active) continue;
    Box ob = obstacleBox(obs[i]);
    for (int k = 0; k < n; k++) if (hit(db[k], ob)) { gameOver(); return; }
  }
}

// ---------------- 畫一格畫面 ----------------
void render() {
  spr.fillSprite(colBg);

  if (night) {                                  // 夜晚：月亮同星星
    spr.fillCircle(260, 30, 10, colFg);
    spr.fillCircle(265, 27, 9, colBg);
    for (int i = 0; i < 6; i++) spr.drawPixel((i * 53 + 17) % PLAY_W, 10 + (i * 37) % 60, colFg);
  }
  for (int i = 0; i < 3; i++) drawArt(CLOUD, ART_ROWS(CLOUD), (int)clouds[i].x, clouds[i].y, colCloud);

  // 地面
  spr.drawFastHLine(0, GROUND_Y - 2, PLAY_W, colFg);
  for (int i = 0; i < 24; i++) spr.drawFastHLine((int)pebbles[i], pebbleY[i], pebbleW[i], colFg);

  // 障礙物
  for (int i = 0; i < MAX_OB; i++) {
    Obstacle &o = obs[i];
    if (!o.active) continue;
    if (o.type == OB_PTERO) {
      bool f = (animTick / 12) % 2;
      drawArt(f ? PTERO2 : PTERO1, ART_ROWS(PTERO1), (int)o.x, o.y, colFg);
    } else {
      const char *const *art = (o.type == OB_CACTUS_S) ? CACTUS_S : CACTUS_L;
      int rows = (o.type == OB_CACTUS_S) ? ART_ROWS(CACTUS_S) : ART_ROWS(CACTUS_L);
      for (int k = 0; k < o.count; k++) drawArt(art, rows, (int)o.x + k * (artW(art) + 2), o.y, colFg);
    }
  }

  // 恐龍
  int dy = (int)dinoY;
  bool f = (animTick / 6) % 2;
  if (state == TITLE) drawArt(DINO_STAND, ART_ROWS(DINO_STAND), DINO_X, dy, colDino);
  else if (state == GAMEOVER) {
    drawArt(DINO_STAND, ART_ROWS(DINO_STAND), DINO_X, dy, colDino);
    spr.fillRect(DINO_X + 20, dy, 6, 6, colBg);         // 死咗：眼變「O」
    spr.drawRect(DINO_X + 20, dy, 6, 6, colDino);
  }
  else if (ducking && onGround) drawArt(f ? DINO_DUCK2 : DINO_DUCK1, ART_ROWS(DINO_DUCK1), DINO_X, GROUND_Y - DUCK_H, colDino);
  else if (!onGround) drawArt(DINO_STAND, ART_ROWS(DINO_STAND), DINO_X, dy, colDino);
  else drawArt(f ? DINO_RUN2 : DINO_RUN1, ART_ROWS(DINO_RUN1), DINO_X, dy, colDino);

  // 分數
  char buf[32];
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(colFg);
  bool showScore = !(flashTimer > 0 && (flashTimer / 8) % 2);
  if (showScore) snprintf(buf, sizeof(buf), "HI %05d   %05d", hiScore, (int)score);
  else           snprintf(buf, sizeof(buf), "HI %05d        ", hiScore);
  spr.drawString(buf, PLAY_W - 6, 4, 2);

  spr.setTextDatum(MC_DATUM);
  if (state == TITLE) {
    spr.drawString("TAP TO START", PLAY_W / 2, 70, 4);
  } else if (state == GAMEOVER) {
    spr.drawString("G A M E   O V E R", PLAY_W / 2, 58, 4);
    // 重新開始圖示（圓圈箭咀）
    spr.drawCircle(PLAY_W / 2, 98, 12, colFg);
    spr.drawCircle(PLAY_W / 2, 98, 11, colFg);
    spr.fillRect(PLAY_W / 2, 84, 10, 10, colBg);
    spr.fillTriangle(PLAY_W / 2, 82, PLAY_W / 2, 92, PLAY_W / 2 + 8, 87, colFg);
  }
  spr.pushSprite(0, PLAY_Y);
}

// ---------------- 狀態切換 ----------------
void gameOver() {
  state = GAMEOVER;
  gameOverMs = millis();
  beep2(300, 120, 180, 250);
  if ((int)score > hiScore) {
    hiScore = (int)score;
    prefs.putInt("hi", hiScore);           // 儲最高分落 flash
  }
  render();
}

void showTitle() {
  state = TITLE;
  resetGame();
  drawStrips();
  render();
}

void startGame() {
  resetGame();
  drawStrips();
  state = PLAYING;
  lastMs = millis(); accMs = 0;
}

// 等手指 / BOOT 掣放開
void waitRelease() {
  uint32_t t0 = millis();
  while ((ts.touched() || digitalRead(BOOT_BTN) == LOW) && millis() - t0 < 2000) delay(10);
}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  // RGB LED 預設熄（active LOW）
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);

  ledcSetup(CH_SPK, 1000, 10);
  ledcAttachPin(SPEAKER_PIN, CH_SPK);
  ledcWrite(CH_SPK, 0);

  prefs.begin("dinorun", false);
  hiScore = prefs.getInt("hi", 0);
  muted = prefs.getBool("mute", false);

  tft.init();
  tft.setRotation(1);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  spr.setColorDepth(8);                    // 8-bit：320x170 只用 ~54KB RAM
  if (!spr.createSprite(PLAY_W, PLAY_H)) Serial.println("Sprite alloc failed!");

  randomSeed(esp_random());
  showTitle();
}

void loop() {
  readInput();
  updateSound();

  if (state == PLAYING) {
    uint32_t now = millis();
    accMs += now - lastMs; lastMs = now;
    if (accMs > 100) accMs = 100;          // 唔好一次追太多步
    while (accMs >= STEP_MS && state == PLAYING) { step(); accMs -= STEP_MS; }
    if (state == PLAYING) render();
    return;
  }

  // TITLE / GAMEOVER：撳 SOUND 掣或者撳屏開始
  bool pressed = touchDown || digitalRead(BOOT_BTN) == LOW;
  if (!pressed) { delay(10); return; }
  if (state == GAMEOVER && millis() - gameOverMs < 600) return;   // 避免死嗰下誤撳即刻重新開始

  if (touchDown) Serial.printf("touch x=%d y=%d\n", touchX, touchY);
  if (touchDown && touchX < 100 && touchY < 36) {
    muted = !muted;
    prefs.putBool("mute", muted);
    drawStrips();
    beep(880, 60);
    waitRelease();
    return;
  }
  waitRelease();
  startGame();
}
