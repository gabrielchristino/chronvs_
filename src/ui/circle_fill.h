#pragma once
#include "lvgl.h"

/* One 412 px launcher background, recorded as bounded RLE in PSRAM from the
 * ordinary partial render. Reuse only while settled over the same backdrop;
 * preserve the original vector path during motion or incompatible drawing.
 * Optional reference switch is used only by the hardware comparison. */
void chronvs_circle_fill_bind(lv_obj_t *object, const bool *reference);
#ifdef CHRONVS_LAUNCHER_SCROLL_AUTOTEST
bool chronvs_circle_fill_info(lv_obj_t *object,uint32_t *bytes,uint32_t *hits);
#endif
