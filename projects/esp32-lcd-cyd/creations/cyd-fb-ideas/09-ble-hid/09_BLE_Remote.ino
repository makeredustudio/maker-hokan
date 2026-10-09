/*
  09_BLE無線遙控鍵盤滑鼠 / BLE Wireless Remote (Keyboard + Mouse + Media)  (CYD ESP32-2432S028R)
  ------------------------------------------------------------------------
  靈感來自 Facebook 私人群組「CYD - Cheap Yellow Display ESP32」
  Piotr Korona 嘅「K-OS Control」（K-OS 0.7.7 入面嘅新 app）：
  https://www.facebook.com/groups/8104937882859357/posts/29388045587455278/
  呢份係學造工作室「AI 補完」自己重寫嘅版本，唔係原作者嘅 code。
  （未喺實機測試，只係 compile 通過）

  功能 / Features
   - 用 ESP32 內置 BLE 扮一個「藍牙鍵盤 + 滑鼠 + 多媒體鍵」(BLE HID composite)
     唔使裝額外 library（用 core 自帶嘅 BLEHIDDevice / BLE2902）
   - 底部 4 個 tab 轉頁：
       PAD   觸控板：拖 = 郁滑鼠，輕撳 = 左鍵；L / HOLD / R 掣；右邊捲動條
       MEDIA 多媒體 / TV 遙控：播放/暫停、音量、靜音、上/下一首、方向鍵、OK、Back、Home
       KEYS  簡報 / 快捷鍵格仔（← → F5 Esc Ctrl+C Ctrl+V Alt+Tab Win+D Zoom 靜音…）
       ABC   簡單屏幕鍵盤：英文字母、數字、空格、Backspace、Enter
   - 頂部顯示藍牙「已連接 / 未連接」
   - 快捷鍵表 SHORTCUTS[] 喺下面設定區，好易自己改

  Library：TFT_eSPI（Larry 嘅 libraries.zip，已設定 CYD）、XPT2046_Touchscreen
           BLE 係 ESP32 core 內置
  Board：ESP32 Dev Module（ESP32 core 2.0.17）。Partition：預設 Default 4MB 就得
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <HIDTypes.h>
#include <esp_gap_ble_api.h>

// ---- HID 鍵碼（俾下面 SHORTCUTS 用；唔使改）/ HID key codes ----
#define MOD_NONE  0x00
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_WIN   0x08     // Windows 鍵 / Mac 嘅 Command 鍵
#define KEY_A     0x04     // a..z = 0x04..0x1D
#define KEY_ENTER 0x28
#define KEY_ESC   0x29
#define KEY_BKSP  0x2A
#define KEY_TAB   0x2B
#define KEY_SPACE 0x2C
#define KEY_F5    0x3E
#define KEY_RIGHT 0x4F
#define KEY_LEFT  0x50
#define KEY_DOWN  0x51
#define KEY_UP    0x52
#define K(c)      (KEY_A + ((c) - 'a'))   // 例如 K('c') = C 鍵

// ======================= 改呢度就得 / USER SETTINGS =======================
const char *DEVICE_NAME  = "CYD Remote";     // 電腦 / 手機藍牙清單見到嘅名
const char *MANUFACTURER = "XueZao Studio";

#define MAC_MODE        false   // true = Mac：快捷鍵入面嘅 Ctrl 自動換做 Command (⌘)
#define MOUSE_SPEED     1.6f    // 觸控板靈敏度（大 = 快）
#define TAP_MAX_MS      220     // 輕撳幾短先算「click」（毫秒）
#define TAP_MAX_MOVE    6       // 輕撳時最多郁幾多 pixel 都算 click
#define SCROLL_STEP_PX  14      // 捲動條拖幾多 pixel = 捲一格
#define SCROLL_INVERT   false   // true = 反轉捲動方向
#define KEY_DELAY_MS    12      // 每個按鍵報告之間等幾耐
#define REPEAT_START_MS 500     // 音量 / 方向鍵 / Backspace 長撳幾耐開始連發
#define REPEAT_EVERY_MS 120
#define FORGET_PAIRINGS false   // true = 開機清除所有已配對裝置（配對出事時用一次）
#define SCREEN_BRIGHT   200     // 背光 0–255
#define RGB_LED_ENABLE  true    // 背面 LED：連接 = 綠、未連接 = 慢閃藍
#define RGB_LED_LEVEL   30      // LED 光度 0–255

// 觸控校正（撳唔準就睇 Serial Monitor 嘅 raw 值再改）
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800
#define TOUCH_MIN_Z 300         // 壓力門檻（誤觸就調高）

// 簡報 / 快捷鍵格仔（最多 16 個，4 x 4）。{ "顯示名", 修飾鍵, 鍵 }
// 修飾鍵可以加埋：MOD_CTRL | MOD_SHIFT
struct Shortcut { const char *label; uint8_t mods; uint8_t key; };
Shortcut SHORTCUTS[] = {
  { "< Slide",   MOD_NONE,             KEY_LEFT  },   // 簡報上一頁
  { "Slide >",   MOD_NONE,             KEY_RIGHT },   // 簡報下一頁
  { "F5 Show",   MOD_NONE,             KEY_F5    },   // PowerPoint 開始放映
  { "Esc",       MOD_NONE,             KEY_ESC   },
  { "Copy",      MOD_CTRL,             K('c')    },   // Ctrl+C
  { "Paste",     MOD_CTRL,             K('v')    },   // Ctrl+V
  { "Undo",      MOD_CTRL,             K('z')    },   // Ctrl+Z
  { "Alt+Tab",   MOD_ALT,              KEY_TAB   },   // 轉視窗
  { "Desktop",   MOD_WIN,              K('d')    },   // Win+D 顯示桌面
  { "Lock PC",   MOD_WIN,              K('l')    },   // Win+L 鎖機
  { "Zoom Mic",  MOD_ALT,              K('a')    },   // Zoom 靜音 Alt+A（Mac: Cmd+Shift+A）
  { "Zoom Cam",  MOD_ALT,              K('v')    },   // Zoom 開/關鏡頭 Alt+V
  { "Teams Mic", MOD_CTRL | MOD_SHIFT, K('m')    },   // Teams 靜音 Ctrl+Shift+M
  { "OBS Rec",   MOD_CTRL | MOD_SHIFT, K('r')    },   // OBS 冇預設熱鍵：喺 OBS 設定 → 熱鍵 自己設 Ctrl+Shift+R
  { "YT Play",   MOD_NONE,             K('k')    },   // YouTube 播放/暫停（k）
  { "YT Full",   MOD_NONE,             K('f')    },   // YouTube 全螢幕（f）
};
// ============================== 設定完 =====================================

// ---- CYD pins ----
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN       21
#define LED_R        4
#define LED_G        16
#define LED_B        17

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;

// ---- 版面 / layout (landscape 320x240) ----
const int W = 320, H = 240;
const int TOP_H = 20;                 // 狀態列
const int TAB_H = 32;                 // 底部 tab
const int TAB_Y = H - TAB_H;          // 208
const int CONT_Y = TOP_H;             // 內容區 20..207
const int CONT_H = TAB_Y - TOP_H;     // 188

// 觸控板頁 / touchpad page
const int PAD_X = 0, PAD_Y = CONT_Y + 2, PAD_W = 262, PAD_H = 140;
const int SCR_X = 266, SCR_W = 54;    // 捲動條
const int MB_Y = PAD_Y + PAD_H + 4;   // 滑鼠掣
const int MB_H = TAB_Y - MB_Y - 2;

// 顏色 / colours
uint16_t C_BG, C_KEY, C_KEY2, C_KEYTXT, C_HI, C_TAB, C_TABON, C_PAD;

// ---- Consumer (多媒體) usage codes ----
#define CC_NEXT   0x00B5
#define CC_PREV   0x00B6
#define CC_STOP   0x00B7
#define CC_PLAY   0x00CD
#define CC_MUTE   0x00E2
#define CC_VOLUP  0x00E9
#define CC_VOLDN  0x00EA
#define CC_MENU   0x0040
#define CC_SEARCH 0x0221
#define CC_HOME   0x0223
#define CC_BACK   0x0224

// =========================================================================
// HID report map：Report 1 = 鍵盤，2 = 多媒體鍵，3 = 滑鼠
// =========================================================================
static const uint8_t REPORT_MAP[] = {
  // ---- Keyboard (ID 1) ----
  USAGE_PAGE(1), 0x01, USAGE(1), 0x06, COLLECTION(1), 0x01, REPORT_ID(1), 0x01,
  USAGE_PAGE(1), 0x07, USAGE_MINIMUM(1), 0xE0, USAGE_MAXIMUM(1), 0xE7,
  LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(1), 0x01, REPORT_SIZE(1), 0x01, REPORT_COUNT(1), 0x08,
  HIDINPUT(1), 0x02,                                   // 8 個修飾鍵 bit
  REPORT_COUNT(1), 0x01, REPORT_SIZE(1), 0x08, HIDINPUT(1), 0x01,   // reserved
  REPORT_COUNT(1), 0x05, REPORT_SIZE(1), 0x01, USAGE_PAGE(1), 0x08,
  USAGE_MINIMUM(1), 0x01, USAGE_MAXIMUM(1), 0x05, HIDOUTPUT(1), 0x02,  // LEDs
  REPORT_COUNT(1), 0x01, REPORT_SIZE(1), 0x03, HIDOUTPUT(1), 0x01,
  REPORT_COUNT(1), 0x06, REPORT_SIZE(1), 0x08, LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(1), 0x65,
  USAGE_PAGE(1), 0x07, USAGE_MINIMUM(1), 0x00, USAGE_MAXIMUM(1), 0x65, HIDINPUT(1), 0x00,  // 6 個鍵
  END_COLLECTION(0),
  // ---- Consumer control (ID 2)：一個 16-bit usage ----
  USAGE_PAGE(1), 0x0C, USAGE(1), 0x01, COLLECTION(1), 0x01, REPORT_ID(1), 0x02,
  LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(2), 0xFF, 0x03,
  USAGE_MINIMUM(1), 0x00, USAGE_MAXIMUM(2), 0xFF, 0x03,
  REPORT_SIZE(1), 0x10, REPORT_COUNT(1), 0x01, HIDINPUT(1), 0x00,
  END_COLLECTION(0),
  // ---- Mouse (ID 3)：5 個掣 + X + Y + 滾輪 + 橫向滾輪 ----
  USAGE_PAGE(1), 0x01, USAGE(1), 0x02, COLLECTION(1), 0x01, USAGE(1), 0x01, COLLECTION(1), 0x00,
  REPORT_ID(1), 0x03,
  USAGE_PAGE(1), 0x09, USAGE_MINIMUM(1), 0x01, USAGE_MAXIMUM(1), 0x05,
  LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(1), 0x01, REPORT_SIZE(1), 0x01, REPORT_COUNT(1), 0x05,
  HIDINPUT(1), 0x02,
  REPORT_SIZE(1), 0x03, REPORT_COUNT(1), 0x01, HIDINPUT(1), 0x03,
  USAGE_PAGE(1), 0x01, USAGE(1), 0x30, USAGE(1), 0x31, USAGE(1), 0x38,
  LOGICAL_MINIMUM(1), 0x81, LOGICAL_MAXIMUM(1), 0x7F, REPORT_SIZE(1), 0x08, REPORT_COUNT(1), 0x03,
  HIDINPUT(1), 0x06,
  USAGE_PAGE(1), 0x0C, USAGE(2), 0x38, 0x02,
  LOGICAL_MINIMUM(1), 0x81, LOGICAL_MAXIMUM(1), 0x7F, REPORT_SIZE(1), 0x08, REPORT_COUNT(1), 0x01,
  HIDINPUT(1), 0x06,
  END_COLLECTION(0), END_COLLECTION(0),
};

BLEHIDDevice *hid = nullptr;
BLECharacteristic *kbInput = nullptr, *kbOutput = nullptr, *ccInput = nullptr, *mouseInput = nullptr;
BLEServer *bleServer = nullptr;
volatile bool bleConnected = false;
volatile bool bleChanged = true;
uint8_t mouseButtons = 0;            // 而家撳住邊個滑鼠掣（HOLD 用）

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    bleConnected = true; bleChanged = true;
  }
  void onDisconnect(BLEServer *s) override {
    bleConnected = false; bleChanged = true;
  }
};

void enableNotify(BLECharacteristic *c) {
  BLE2902 *d = (BLE2902 *)c->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  if (d) d->setNotifications(true);
}

void forgetPairings() {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return;
  esp_ble_bond_dev_t *list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * n);
  if (!list) return;
  esp_ble_get_bond_device_list(&n, list);
  for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
  free(list);
  Serial.printf("Removed %d bonded device(s)\n", n);
}

void startBLE() {
  BLEDevice::init(DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCB());
  hid = new BLEHIDDevice(bleServer);
  kbInput    = hid->inputReport(1);
  kbOutput   = hid->outputReport(1);   // Caps Lock 等燈號（我哋唔用，但要有）
  ccInput    = hid->inputReport(2);
  mouseInput = hid->inputReport(3);
  hid->manufacturer()->setValue(MANUFACTURER);
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  BLESecurity *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);    // 「Just Works」配對，唔使入 PIN
  hid->reportMap((uint8_t *)REPORT_MAP, sizeof(REPORT_MAP));
  hid->startServices();
  hid->setBatteryLevel(100);
  if (FORGET_PAIRINGS) forgetPairings();
  BLEAdvertising *adv = bleServer->getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  adv->start();
  Serial.printf("BLE advertising as \"%s\"\n", DEVICE_NAME);
}

// ---- 發送報告 / send reports ----
void notify(BLECharacteristic *c, uint8_t *data, size_t len) {
  c->setValue(data, len);
  c->notify();
  delay(KEY_DELAY_MS);
}

uint8_t fixMods(uint8_t mods) {
  if (MAC_MODE && (mods & MOD_CTRL)) mods = (mods & ~MOD_CTRL) | MOD_WIN;   // Ctrl → Command
  return mods;
}

void sendKey(uint8_t mods, uint8_t key) {
  if (!bleConnected) return;
  uint8_t r[8] = { mods, 0, 0, 0, 0, 0, 0, 0 };
  if (mods) notify(kbInput, r, 8);            // 先撳修飾鍵（Alt+Tab 之類穩陣啲）
  r[2] = key;
  notify(kbInput, r, 8);
  r[2] = 0;
  if (mods) notify(kbInput, r, 8);
  memset(r, 0, 8);
  notify(kbInput, r, 8);                      // 全部放手
  Serial.printf("KEY mods=0x%02X key=0x%02X\n", mods, key);
}

void sendMedia(uint16_t code) {
  if (!bleConnected) return;
  uint8_t r[2] = { (uint8_t)(code & 0xFF), (uint8_t)(code >> 8) };
  notify(ccInput, r, 2);
  r[0] = r[1] = 0;
  notify(ccInput, r, 2);
  Serial.printf("MEDIA 0x%04X\n", code);
}

void sendMouse(int dx, int dy, int wheel, uint8_t buttons) {
  if (!bleConnected) return;
  uint8_t r[5] = { buttons, (uint8_t)(int8_t)constrain(dx, -127, 127),
                   (uint8_t)(int8_t)constrain(dy, -127, 127),
                   (uint8_t)(int8_t)constrain(wheel, -127, 127), 0 };
  mouseInput->setValue(r, 5);
  mouseInput->notify();
}

void mouseClick(uint8_t b) {
  if (!bleConnected) return;
  sendMouse(0, 0, 0, mouseButtons | b);
  delay(KEY_DELAY_MS * 2);
  sendMouse(0, 0, 0, mouseButtons);
  Serial.printf("CLICK %s\n", b == 1 ? "L" : "R");
}

// 字元 → HID 鍵碼（只做鍵盤頁用到嘅字）
bool charToKey(char c, uint8_t &mods, uint8_t &key) {
  mods = 0;
  if (c >= 'a' && c <= 'z') { key = K(c); return true; }
  if (c >= 'A' && c <= 'Z') { key = K(c - 'A' + 'a'); mods = MOD_SHIFT; return true; }
  if (c >= '1' && c <= '9') { key = 0x1E + (c - '1'); return true; }
  if (c == '0') { key = 0x27; return true; }
  switch (c) {
    case ' ': key = KEY_SPACE; return true;
    case ',': key = 0x36; return true;
    case '.': key = 0x37; return true;
    case '-': key = 0x2D; return true;
    case '?': key = 0x38; mods = MOD_SHIFT; return true;
    case '!': key = 0x1E; mods = MOD_SHIFT; return true;
    case '@': key = 0x1F; mods = MOD_SHIFT; return true;
  }
  return false;
}

// =========================================================================
// 掣 / buttons
// =========================================================================
enum Act : uint8_t { A_NONE, A_KEY, A_MEDIA, A_CHAR, A_CAPS, A_BKSP };
struct Btn {
  int16_t x, y, w, h;
  const char *label;
  Act act;
  uint16_t code;          // HID key / consumer code / char
  uint8_t mods;
  bool repeat;            // 長撳連發
  uint16_t color;
};
// 自己寫定 prototype（Arduino IDE 自動生成嘅會放喺 struct 前面，compile 唔過）
void drawBtn(const Btn &b, bool pressed);
void fireBtn(const Btn &b);
void addBtn(int x, int y, int w, int h, const char *l, Act a, uint16_t code, uint8_t mods,
            bool rep, uint16_t col);
#define MAX_BTN 48
Btn btns[MAX_BTN];
int nBtn = 0;

enum Page { P_PAD, P_MEDIA, P_KEYS, P_ABC, P_COUNT };
const char *TAB_NAMES[P_COUNT] = { "PAD", "MEDIA", "KEYS", "ABC" };
int page = P_PAD;
bool capsOn = false;
char statusMsg[40] = "";
uint32_t statusMsgMs = 0;

void addBtn(int x, int y, int w, int h, const char *l, Act a, uint16_t code, uint8_t mods = 0,
            bool rep = false, uint16_t col = 0);
void addBtn(int x, int y, int w, int h, const char *l, Act a, uint16_t code, uint8_t mods,
            bool rep, uint16_t col) {
  if (nBtn >= MAX_BTN) return;
  btns[nBtn++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, l, a, code, mods, rep,
                   col ? col : C_KEY };
}

void drawBtn(const Btn &b, bool pressed) {
  uint16_t bg = pressed ? C_HI : b.color;
  if (b.act == A_CAPS && capsOn) bg = pressed ? C_HI : TFT_ORANGE;
  tft.fillRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 6, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(pressed ? TFT_BLACK : C_KEYTXT, bg);
  char tmp[12];
  const char *txt = b.label;
  if (b.act == A_CHAR && !b.label) {           // 字母掣：跟 Caps 顯示大細楷
    char c = (char)b.code;
    if (capsOn && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    tmp[0] = c; tmp[1] = 0;
    txt = tmp;
  }
  int font = (b.act == A_CHAR && !b.label) ? 4 : 2;
  tft.drawString(txt, b.x + b.w / 2, b.y + b.h / 2 + 1, font);
}

int findBtn(int x, int y) {
  for (int i = 0; i < nBtn; i++) {
    const Btn &b = btns[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
  }
  return -1;
}

void fireBtn(const Btn &b) {
  switch (b.act) {
    case A_KEY: sendKey(fixMods(b.mods), (uint8_t)b.code); break;
    case A_MEDIA: sendMedia(b.code); break;
    case A_BKSP: sendKey(0, KEY_BKSP); break;
    case A_CHAR: {
      char c = (char)b.code;
      if (capsOn && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
      uint8_t m, k;
      if (charToKey(c, m, k)) sendKey(m, k);
      break;
    }
    case A_CAPS: {
      capsOn = !capsOn;
      for (int i = 0; i < nBtn; i++) if (btns[i].act == A_CHAR || btns[i].act == A_CAPS) drawBtn(btns[i], false);
      break;
    }
    default: break;
  }
  if (b.act != A_CAPS) {
    if (!bleConnected) snprintf(statusMsg, sizeof(statusMsg), "Not connected!");
    else if (b.act == A_CHAR) snprintf(statusMsg, sizeof(statusMsg), "Typed '%c'", capsOn && b.code >= 'a' && b.code <= 'z' ? (char)(b.code - 32) : (char)b.code);
    else snprintf(statusMsg, sizeof(statusMsg), "Sent %s", b.label ? b.label : "");
    statusMsgMs = millis();
  }
}

// ---- 每頁嘅掣 / build each page ----
void buildMedia() {
  const int cw = 80, rh = CONT_H / 4;   // 4 x 4
  struct M { const char *l; Act a; uint16_t c; bool rep; };
  const M items[16] = {
    { "|<< PREV", A_MEDIA, CC_PREV, false }, { ">|| PLAY", A_MEDIA, CC_PLAY, false },
    { "NEXT >>|", A_MEDIA, CC_NEXT, false }, { "MUTE", A_MEDIA, CC_MUTE, false },
    { "VOL -", A_MEDIA, CC_VOLDN, true },    { "UP", A_KEY, KEY_UP, true },
    { "VOL +", A_MEDIA, CC_VOLUP, true },    { "HOME", A_MEDIA, CC_HOME, false },
    { "LEFT", A_KEY, KEY_LEFT, true },       { "OK", A_KEY, KEY_ENTER, false },
    { "RIGHT", A_KEY, KEY_RIGHT, true },     { "BACK", A_MEDIA, CC_BACK, false },
    { "STOP", A_MEDIA, CC_STOP, false },     { "DOWN", A_KEY, KEY_DOWN, true },
    { "MENU", A_MEDIA, CC_MENU, false },     { "SEARCH", A_MEDIA, CC_SEARCH, false },
  };
  for (int i = 0; i < 16; i++) {
    int col = i % 4, row = i / 4;
    bool dpad = (items[i].a == A_KEY);
    addBtn(col * cw, CONT_Y + row * rh, cw, rh, items[i].l, items[i].a, items[i].c, 0, items[i].rep,
           dpad ? C_KEY2 : 0);
  }
}

void buildKeys() {
  const int n = min((int)(sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0])), 16);
  const int cw = 80, rh = CONT_H / 4;
  for (int i = 0; i < n; i++) {
    addBtn((i % 4) * cw, CONT_Y + (i / 4) * rh, cw, rh, SHORTCUTS[i].label, A_KEY, SHORTCUTS[i].key,
           SHORTCUTS[i].mods, SHORTCUTS[i].key == KEY_LEFT || SHORTCUTS[i].key == KEY_RIGHT,
           (i / 4) % 2 ? C_KEY2 : 0);
  }
}

void buildAbc() {
  const int rh = CONT_H / 5;   // 37
  const char *rows[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
  for (int r = 0; r < 4; r++) {
    int n = strlen(rows[r]);
    int kw = 32;
    int x0 = (r == 2) ? 16 : (r == 3) ? 48 : 0;
    int y = CONT_Y + r * rh;
    if (r == 3) addBtn(0, y, 48, rh, "CAPS", A_CAPS, 0, 0, false, C_KEY2);
    for (int i = 0; i < n; i++) addBtn(x0 + i * kw, y, kw, rh, nullptr, A_CHAR, rows[r][i]);
    if (r == 3) addBtn(272, y, 48, rh, "DEL", A_BKSP, 0, 0, true, C_KEY2);
  }
  int y = CONT_Y + 4 * rh;
  addBtn(0, y, 40, rh, nullptr, A_CHAR, ',');
  addBtn(40, y, 160, rh, "SPACE", A_CHAR, ' ', 0, false, C_KEY2);
  addBtn(200, y, 40, rh, nullptr, A_CHAR, '.');
  addBtn(240, y, 80, rh, "ENTER", A_KEY, KEY_ENTER, 0, false, C_KEY2);
}

// =========================================================================
// 畫面 / drawing
// =========================================================================
void drawStatus() {
  tft.fillRect(0, 0, W, TOP_H, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(DEVICE_NAME, 4, TOP_H / 2, 2);
  // 中間：最近動作 / last action
  if (statusMsg[0] && millis() - statusMsgMs < 2500) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(strcmp(statusMsg, "Not connected!") == 0 ? TFT_RED : TFT_YELLOW, TFT_BLACK);
    tft.drawString(statusMsg, 168, TOP_H / 2, 2);
  }
  // 右邊：BLE 狀態
  const char *s = bleConnected ? "BLE CONNECTED" : "PAIR ME (BLE)";
  uint16_t bg = bleConnected ? TFT_GREEN : TFT_RED;
  int w = tft.textWidth(s, 1) + 10;
  tft.fillRoundRect(W - w - 2, 3, w, TOP_H - 6, 4, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, bg);
  tft.drawString(s, W - 2 - w / 2, TOP_H / 2, 1);
}

void drawTabs() {
  const int tw = W / P_COUNT;
  for (int i = 0; i < P_COUNT; i++) {
    uint16_t bg = (i == page) ? C_TABON : C_TAB;
    tft.fillRect(i * tw, TAB_Y, tw, TAB_H, bg);
    tft.drawRect(i * tw, TAB_Y, tw, TAB_H, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(i == page ? TFT_BLACK : TFT_WHITE, bg);
    tft.drawString(TAB_NAMES[i], i * tw + tw / 2, TAB_Y + TAB_H / 2 + 1, 4);
  }
}

void drawPadPage() {
  tft.fillRoundRect(PAD_X + 2, PAD_Y, PAD_W - 4, PAD_H, 10, C_PAD);
  tft.drawRoundRect(PAD_X + 2, PAD_Y, PAD_W - 4, PAD_H, 10, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_DARKGREY, C_PAD);
  tft.drawString("TOUCHPAD", PAD_X + PAD_W / 2, PAD_Y + PAD_H / 2 - 10, 4);
  tft.drawString("drag = move   tap = click", PAD_X + PAD_W / 2, PAD_Y + PAD_H / 2 + 16, 2);
  // 捲動條 / scroll strip
  tft.fillRoundRect(SCR_X, PAD_Y, SCR_W - 2, PAD_H, 10, C_PAD);
  tft.drawRoundRect(SCR_X, PAD_Y, SCR_W - 2, PAD_H, 10, TFT_DARKGREY);
  tft.setTextColor(TFT_LIGHTGREY, C_PAD);
  tft.drawString("^", SCR_X + SCR_W / 2 - 1, PAD_Y + 14, 4);
  tft.drawString("SCROLL", SCR_X + SCR_W / 2 - 1, PAD_Y + PAD_H / 2, 1);
  tft.drawString("v", SCR_X + SCR_W / 2 - 1, PAD_Y + PAD_H - 16, 4);
}

void drawHoldBtn() {
  for (int i = 0; i < nBtn; i++)
    if (btns[i].act == A_NONE && btns[i].code == 'H') {
      Btn b = btns[i];
      if (mouseButtons & 1) b.color = TFT_ORANGE;
      drawBtn(b, false);
    }
}

void buildPad() {
  // 滑鼠掣用 A_NONE + code 'L' / 'H' / 'R'，喺 onTouch 特別處理
  addBtn(0, MB_Y, 120, MB_H, "LEFT", A_NONE, 'L');
  addBtn(120, MB_Y, 80, MB_H, "HOLD", A_NONE, 'H', 0, false, C_KEY2);
  addBtn(200, MB_Y, 120, MB_H, "RIGHT", A_NONE, 'R');
}

void buildPage() {
  nBtn = 0;
  if (page == P_PAD) buildPad();
  else if (page == P_MEDIA) buildMedia();
  else if (page == P_KEYS) buildKeys();
  else buildAbc();
}

void drawPage() {
  tft.fillRect(0, CONT_Y, W, CONT_H, C_BG);
  buildPage();
  if (page == P_PAD) drawPadPage();
  for (int i = 0; i < nBtn; i++) drawBtn(btns[i], false);
  if (page == P_PAD) drawHoldBtn();
  drawTabs();
}

// =========================================================================
// 觸控 / touch
// =========================================================================
bool readTouch(int &tx, int &ty) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < TOUCH_MIN_Z) return false;
  tx = constrain(map(p.x, TS_MINX, TS_MAXX, 0, W), 0, W - 1);
  ty = constrain(map(p.y, TS_MINY, TS_MAXY, 0, H), 0, H - 1);
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 300) {
    lastPrint = millis();
    Serial.printf("touch raw x=%d y=%d z=%d -> %d,%d\n", p.x, p.y, p.z, tx, ty);
  }
  return true;
}

// 觸控狀態 / gesture state
bool wasDown = false;
int startX, startY, lastX, lastY;
uint32_t downMs, lastRepeatMs;
int activeBtn = -1;
enum Zone { Z_NONE, Z_PAD, Z_SCROLL, Z_BTN, Z_TAB } zone = Z_NONE;
float accX = 0, accY = 0;
int scrollAcc = 0;
uint8_t upCount = 0;

void onTouchStart(int x, int y) {
  startX = lastX = x; startY = lastY = y;
  downMs = lastRepeatMs = millis();
  accX = accY = 0; scrollAcc = 0;
  activeBtn = -1;
  if (y >= TAB_Y) {                            // 轉頁
    zone = Z_TAB;
    int p = x / (W / P_COUNT);
    if (p != page) { page = p; drawPage(); }
    return;
  }
  if (page == P_PAD) {
    if (x >= PAD_X && x < PAD_X + PAD_W && y >= PAD_Y && y < PAD_Y + PAD_H) { zone = Z_PAD; return; }
    if (x >= SCR_X && y >= PAD_Y && y < PAD_Y + PAD_H) { zone = Z_SCROLL; return; }
  }
  activeBtn = findBtn(x, y);
  if (activeBtn < 0) { zone = Z_NONE; return; }
  zone = Z_BTN;
  Btn &b = btns[activeBtn];
  drawBtn(b, true);
  if (b.act == A_NONE) {                       // 觸控板頁嘅滑鼠掣
    if (b.code == 'L') mouseClick(1);
    else if (b.code == 'R') mouseClick(2);
    else if (b.code == 'H') {                  // HOLD：左鍵一直撳住（拖曳 / 揀字用）
      mouseButtons ^= 1;
      sendMouse(0, 0, 0, mouseButtons);
      snprintf(statusMsg, sizeof(statusMsg), (mouseButtons & 1) ? "Left HOLD on" : "Left HOLD off");
      statusMsgMs = millis();
    }
    if (!bleConnected) { strcpy(statusMsg, "Not connected!"); statusMsgMs = millis(); }
  } else {
    fireBtn(b);
  }
  drawStatus();
}

void onTouchMove(int x, int y) {
  int dx = x - lastX, dy = y - lastY;
  if (zone == Z_PAD) {
    // 細過 1 pixel 嘅抖動唔理 / ignore jitter
    if (abs(dx) <= 1 && abs(dy) <= 1) return;
    accX += dx * MOUSE_SPEED;
    accY += dy * MOUSE_SPEED;
    // 快拖加速 / simple acceleration
    if (abs(dx) + abs(dy) > 12) { accX *= 1.5f; accY *= 1.5f; }
    int mx = (int)accX, my = (int)accY;
    if (mx || my) {
      sendMouse(mx, my, 0, mouseButtons);
      accX -= mx; accY -= my;
    }
    lastX = x; lastY = y;
  } else if (zone == Z_SCROLL) {
    scrollAcc += dy;
    while (abs(scrollAcc) >= SCROLL_STEP_PX) {
      int step = (scrollAcc > 0) ? 1 : -1;
      // 手指向下拖 = 內容向上（好似手機），SCROLL_INVERT 反轉
      int wheel = SCROLL_INVERT ? step : -step;
      sendMouse(0, 0, wheel, mouseButtons);
      scrollAcc -= step * SCROLL_STEP_PX;
    }
    lastX = x; lastY = y;
  } else if (zone == Z_BTN && activeBtn >= 0) {
    Btn &b = btns[activeBtn];
    if (b.repeat && millis() - downMs > REPEAT_START_MS && millis() - lastRepeatMs > REPEAT_EVERY_MS) {
      lastRepeatMs = millis();
      fireBtn(b);
    }
  }
}

void onTouchEnd() {
  if (zone == Z_PAD) {
    bool tap = (millis() - downMs < TAP_MAX_MS) &&
               abs(lastX - startX) <= TAP_MAX_MOVE && abs(lastY - startY) <= TAP_MAX_MOVE;
    if (tap) {
      mouseClick(1);
      strcpy(statusMsg, bleConnected ? "Click" : "Not connected!");
      statusMsgMs = millis();
      drawStatus();
    }
  }
  if (zone == Z_BTN && activeBtn >= 0) {        // 放手：掣變返原色
    if (btns[activeBtn].act == A_NONE && btns[activeBtn].code == 'H') drawHoldBtn();
    else drawBtn(btns[activeBtn], false);
  }
  zone = Z_NONE;
  activeBtn = -1;
}

void handleTouch() {
  int x, y;
  bool down = readTouch(x, y);
  if (down) {
    upCount = 0;
    if (!wasDown) { wasDown = true; onTouchStart(x, y); }
    else onTouchMove(x, y);
  } else if (wasDown) {
    // 電阻式觸控會「甩」一兩下，連續 3 次冇撳先算放手 / debounce release
    if (++upCount >= 3) { wasDown = false; upCount = 0; onTouchEnd(); }
  }
}

// =========================================================================
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (!RGB_LED_ENABLE) return;
  ledcWrite(1, 255 - r);                       // active-low
  ledcWrite(2, 255 - g);
  ledcWrite(3, 255 - b);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n09 BLE Remote starting...");
  if (RGB_LED_ENABLE) {
    ledcSetup(1, 5000, 8); ledcAttachPin(LED_R, 1);
    ledcSetup(2, 5000, 8); ledcAttachPin(LED_G, 2);
    ledcSetup(3, 5000, 8); ledcAttachPin(LED_B, 3);
    setLed(0, 0, 0);
  } else {
    pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
    digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
  }

  tft.init();
  tft.setRotation(1);                          // 橫向 320x240
  tft.fillScreen(TFT_BLACK);
  pinMode(BL_PIN, OUTPUT);
  analogWrite(BL_PIN, SCREEN_BRIGHT);

  C_BG     = tft.color565(18, 12, 40);         // 深紫底
  C_KEY    = tft.color565(92, 70, 210);        // 紫藍色鍵（似原帖片入面）
  C_KEY2   = tft.color565(50, 90, 200);        // 藍色功能鍵
  C_KEYTXT = TFT_WHITE;
  C_HI     = TFT_WHITE;
  C_TAB    = tft.color565(40, 40, 70);
  C_TABON  = tft.color565(0, 200, 255);
  C_PAD    = tft.color565(35, 30, 70);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("BLE Remote", W / 2, H / 2 - 14, 4);
  tft.drawString("starting Bluetooth...", W / 2, H / 2 + 14, 2);

  startBLE();
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
  tft.fillScreen(C_BG);
  drawStatus();
  drawPage();
}

void loop() {
  handleTouch();

  if (bleChanged) {                            // 連接 / 斷線
    bleChanged = false;
    if (bleConnected) {
      Serial.println("BLE connected");
      enableNotify(kbInput); enableNotify(ccInput); enableNotify(mouseInput);
      setLed(0, RGB_LED_LEVEL, 0);
    } else {
      Serial.println("BLE disconnected -> advertising again");
      mouseButtons = 0;
      delay(300);
      bleServer->startAdvertising();
    }
    drawStatus();
  }

  // 未連接：LED 慢閃藍；狀態訊息過時就清走
  static uint32_t lastTick = 0;
  if (millis() - lastTick > 500) {
    lastTick = millis();
    if (!bleConnected) setLed(0, 0, (millis() / 500) % 2 ? RGB_LED_LEVEL : 0);
    static bool msgShown = false;
    bool show = statusMsg[0] && millis() - statusMsgMs < 2500;
    if (msgShown && !show) { statusMsg[0] = 0; drawStatus(); }
    msgShown = show;
  }
  delay(8);
}
