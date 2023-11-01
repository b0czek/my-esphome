#pragma once

#include "esphome.h"
#include "lvgl.h"
#include <TFT_eSPI.h>

const size_t buf_pix_count = LV_HOR_RES_MAX * LV_VER_RES_MAX / 5;

static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf[buf_pix_count];

TFT_eSPI tft = TFT_eSPI();

void IRAM_ATTR gui_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  size_t len = lv_area_get_size(area);

  /* Update TFT */
  tft.startWrite();                                      /* Start new TFT transaction */
  tft.setWindow(area->x1, area->y1, area->x2, area->y2); /* set the working window */
#ifdef USE_DMA_TO_TFT
  tft.pushPixelsDMA((uint16_t *)color_p, len); /* Write words at once */
#else
  tft.pushPixels((uint16_t *)color_p, len); /* Write words at once */
#endif
  tft.endWrite(); /* terminate TFT transaction */

  /* Tell lvgl that flushing is done */
  lv_disp_flush_ready(disp);
}

class DisplayComponent : public Component
{

public:
  void setup() override
  {

    tft.begin();
    tft.setSwapBytes(false); /* set endianess */
    tft.setRotation(TFT_ROTATION);

    delay(250);

    lv_init();

    lv_disp_draw_buf_init(&disp_buf, buf, NULL, buf_pix_count);

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    disp_drv.flush_cb = gui_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    lv_obj_set_flex_flow(lv_scr_act(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lv_scr_act(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);

    last_time = millis();
  }

  void loop() override
  {
    lv_timer_handler(); // called by dispatch_loop
    lv_tick_inc(millis() - last_time);
    last_time = millis();
  }

private:
  size_t last_time;
};
