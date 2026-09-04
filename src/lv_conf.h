#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1
#define LV_MEM_CUSTOM 0
// The stock 48 KiB heap is enough for a few labels, but not for a dial with
// rings, subdials and persistent tick objects.
#define LV_MEM_SIZE (128U * 1024U)
// 50 FPS is the smooth, sustainable target for this QSPI display.  It gives
// gesture updates enough time to finish without continuously queuing frames.
#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_18

#endif
#endif
