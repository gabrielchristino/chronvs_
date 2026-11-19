#pragma once
#include <stdbool.h>

/* Isolated display experiments; the normal firmware has no hooks or ISR. */
#ifdef CHRONVS_DISPLAY_SCAN
void chronvs_display_scan_init(void);
void chronvs_display_scan_poll(bool display_off);
#else
static inline void chronvs_display_scan_init(void) {}
static inline void chronvs_display_scan_poll(bool display_off) { (void)display_off; }
#endif
