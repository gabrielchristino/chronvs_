#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { RUN_IDLE, RUN_READY, RUN_RECORDING, RUN_PAUSED, RUN_SAVED, RUN_ERROR } chronvs_run_state_t;
typedef struct {
    chronvs_run_state_t state;
    double distance_m, speed_kmh;
    uint32_t elapsed_s, active_s, points;
    bool fix, speed_valid;
    char file[48];
    const char *message;
} chronvs_run_snapshot_t;

/* All calls on UI task. Leaving the app never stops an activity. */
void chronvs_run_prepare(void);
void chronvs_run_release_preview(void);
bool chronvs_run_start(void);
bool chronvs_run_pause(void);
bool chronvs_run_resume(void);
bool chronvs_run_finish(void);
void chronvs_run_poll(void);
void chronvs_run_snapshot(chronvs_run_snapshot_t *out);
/* Read-only names, newest first; no automatic deletion. */
unsigned chronvs_run_files(char names[][48], unsigned capacity);
