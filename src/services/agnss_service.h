#pragma once
#include "services/gps_data.h"
#include <stddef.h>
#include <stdint.h>

typedef enum { AGNSS_IDLE, AGNSS_WAIT_TIME, AGNSS_DOWNLOAD, AGNSS_READY,
    AGNSS_SENDING, AGNSS_SENT, AGNSS_ERROR, AGNSS_DEFERRED, AGNSS_FIX_READY } chronvs_agnss_state_t;
typedef enum { AGNSS_SV_UNTRIED, AGNSS_SV_WAITING, AGNSS_SV_ACCEPTED,
    AGNSS_SV_REJECTED, AGNSS_SV_UNCONFIRMED } chronvs_agnss_satellite_state_t;
typedef struct { uint8_t attempts, state; } chronvs_agnss_satellite_t;
typedef struct {
    chronvs_agnss_state_t state;
    unsigned sent, total, acknowledgements, rejections, receiver_ephemerides;
    bool receiver_status;
    uint32_t minimum_internal, minimum_dma;
    const char *message;
    const char *source;
    unsigned gps_accepted, gps_rejected, gps_unconfirmed, retries;
    unsigned gps_confirmed_total;
    unsigned current_satellite, current_attempt;
    chronvs_agnss_satellite_t gps[32];
} chronvs_agnss_snapshot_t;
/* UI-task API; the worker only downloads/validates, never touches UART or LVGL. */
bool chronvs_agnss_request(bool force);
bool chronvs_agnss_active(void);
void chronvs_agnss_snapshot(chronvs_agnss_snapshot_t *out);
const char *chronvs_agnss_reference_name(void);
typedef int (*chronvs_agnss_write_t)(const uint8_t *, size_t);
void chronvs_agnss_poll(const chronvs_gps_data_t *gps, bool serial_ready, bool workout_active,
                        chronvs_agnss_write_t write);
void chronvs_agnss_receiver_stopped(void);
void chronvs_agnss_receive(const uint8_t *frame, size_t size);
/* True until a fresh GGA with >=4 used satellites confirms an actual fix. */
bool chronvs_agnss_fix_pending(void);
void chronvs_agnss_confirm_fix(void);
