#pragma once
#ifdef CHRONVS_DISPLAY_BENCH
void chronvs_display_bench_init(void);
#else
static inline void chronvs_display_bench_init(void) {}
#endif
