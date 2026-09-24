#pragma once
#include <stdbool.h>

#ifdef CHRONVS_DISPLAY_PROFILE
void chronvs_display_profile_init(void);
void chronvs_display_profile_poll(bool display_off);
#else
static inline void chronvs_display_profile_init(void) {}
static inline void chronvs_display_profile_poll(bool display_off) { (void)display_off; }
#endif
