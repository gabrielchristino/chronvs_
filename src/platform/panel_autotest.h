#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef CHRONVS_PANEL_AUTOTEST
typedef struct { bool running; unsigned completed, failed, total; } chronvs_panel_test_status_t;
void chronvs_panel_autotest_init(void);
void chronvs_panel_autotest_poll(void);
chronvs_panel_test_status_t chronvs_panel_autotest_status(void);
#else
static inline void chronvs_panel_autotest_init(void) {}
static inline void chronvs_panel_autotest_poll(void) {}
#endif
