/*
  04_LVGL拖放介面入門  (CYD ESP32-2432S028R)
  ------------------------------------------------------------
  靈感來自 CYD Facebook 群組 Zsolt Sziklai 介紹嘅 LCD Ultra Pro（LVGL 8.4 視覺化拖放編輯器）：
  https://www.facebook.com/groups/710623908028293/posts/1098149399275740/
  https://www.lcdultrapro.com/
  呢份係學造工作室「AI 補完」自己寫嘅最簡單 LVGL 入門範例，唔係 LCD Ultra Pro 產生嘅 code。
  佢示範咗拖放編輯器背後其實做緊乜：建立 widget、設定位置、綁定事件。

  畫面：
   - 標題 Label
   - 一粒 Button：撳一下計數 +1，同時切換 RGB LED
   - 一條 Slider：拉嚟調背光光暗（GPIO21 PWM）
   - 一個 Label 顯示 slider 數值

  ⚠️ 必須：將同一個 folder 嘅 lv_conf.h 放入 Arduino\libraries\（同 lvgl 資料夾並排）
  Library：lvgl 8.3.x、TFT_eSPI（已設定 CYD）、XPT2046_Touchscreen
  Board：ESP32 Dev Module
*/

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// CYD 觸控腳位
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BL_PIN 21
#define LED_R 4
#define LED_G 16
#define LED_B 17

static const uint16_t SCREEN_W = 320;
static const uint16_t SCREEN_H = 240;

TFT_eSPI tft;
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_W * 20];     // 20 行嘅畫圖緩衝

lv_obj_t *countLabel;
lv_obj_t *sliderLabel;
int clickCount = 0;

// ---------- LVGL <-> 硬件 嘅兩條「橋」 ----------
// 1) LVGL 畫好一塊，就交俾 TFT_eSPI 推上屏幕
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// 2) LVGL 問「有冇人撳屏幕？撳咗邊？」
void my_touch_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    data->point.x = constrain(map(p.x, 200, 3700, 0, SCREEN_W), 0, SCREEN_W - 1);
    data->point.y = constrain(map(p.y, 240, 3800, 0, SCREEN_H), 0, SCREEN_H - 1);
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---------- 事件（拖放編輯器入面「On Click」「On Value Changed」就係呢啲） ----------
static void button_event_cb(lv_event_t *e) {
  clickCount++;
  lv_label_set_text_fmt(countLabel, "Clicked: %d", clickCount);
  bool on = clickCount % 2;
  digitalWrite(LED_G, on ? LOW : HIGH);   // RGB LED 低電平亮
}

static void slider_event_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int v = lv_slider_get_value(slider);
  lv_label_set_text_fmt(sliderLabel, "Backlight: %d%%", v);
  ledcWrite(0, map(v, 0, 100, 10, 255));
}

// ---------- 起畫面（SquareLine / LCD Ultra Pro 匯出嘅 ui_init() 就係做呢啲） ----------
void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x102030), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "Hello CYD + LVGL");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFD000), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 160, 50);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, -25);
  lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *btnLabel = lv_label_create(btn);
  lv_label_set_text(btnLabel, LV_SYMBOL_POWER "  Press me");
  lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_20, 0);
  lv_obj_center(btnLabel);

  countLabel = lv_label_create(scr);
  lv_label_set_text(countLabel, "Clicked: 0");
  lv_obj_set_style_text_color(countLabel, lv_color_white(), 0);
  lv_obj_align(countLabel, LV_ALIGN_CENTER, 0, 20);

  lv_obj_t *slider = lv_slider_create(scr);
  lv_obj_set_width(slider, 240);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, 100, LV_ANIM_OFF);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sliderLabel = lv_label_create(scr);
  lv_label_set_text(sliderLabel, "Backlight: 100%");
  lv_obj_set_style_text_color(sliderLabel, lv_color_white(), 0);
  lv_obj_align(sliderLabel, LV_ALIGN_BOTTOM_MID, 0, -15);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);  // 全熄

  tft.init();
  tft.setRotation(1);                 // 橫向 320x240
  ledcSetup(0, 5000, 8);
  ledcAttachPin(BL_PIN, 0);
  ledcWrite(0, 255);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_W * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  build_ui();
}

void loop() {
  lv_timer_handler();   // LVGL 處理畫面同觸控
  delay(5);
}
