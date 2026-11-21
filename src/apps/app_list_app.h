#pragma once

#include "core/app_manager.h"

extern const chronvs_app_t chronvs_app_list_app;

#ifdef CHRONVS_LAUNCHER_SCROLL_AUTOTEST
enum {
    CHRONVS_SCROLL_NO_ICONS=1, CHRONVS_SCROLL_NO_LABELS=2,
    CHRONVS_SCROLL_NO_BADGES=4, CHRONVS_SCROLL_NO_FADE=8,
    CHRONVS_SCROLL_NO_CLIP=16, CHRONVS_SCROLL_SQUARE=32, CHRONVS_SCROLL_REFERENCE_FILL=64,
};
/* Diagnostic only: update the real list at a known phase, without rendering. */
void chronvs_app_list_cost_frame(unsigned mode, int phase);
#endif

