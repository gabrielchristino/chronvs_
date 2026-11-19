#pragma once

#include "lvgl.h"

#ifdef CHRONVS_ARC_INNER_CLIP
void chronvs_ui_arc_clip_bind(lv_obj_t *arc);
#else
static inline void chronvs_ui_arc_clip_bind(lv_obj_t *arc) { (void)arc; }
#endif
