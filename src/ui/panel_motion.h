#pragma once

#include "lvgl.h"

/* Shared release completion; gesture thresholds remain owned by each panel. */
#define CHRONVS_UI_PANEL_SETTLE_MS 220
#define CHRONVS_UI_PANEL_SETTLE_PATH lv_anim_path_ease_out
