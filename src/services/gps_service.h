#pragma once
#include "services/gps_data.h"

#ifdef CHRONVS_GPS_STANDBY_TEST
typedef enum {
    GPS_TEST_IDLE, GPS_TEST_OBSERVING, GPS_TEST_SILENT, GPS_TEST_RETURNED,
    GPS_TEST_CONTINUED, GPS_TEST_NO_RETURN, GPS_TEST_INTERRUPTED, GPS_TEST_TX_ERROR
} chronvs_gps_test_state_t;
#endif

typedef struct {
    chronvs_gps_data_t data;
    bool running, serial_fresh, error;
    unsigned baud;
    uint32_t bytes;
#ifdef CHRONVS_GPS_STANDBY_TEST
    chronvs_gps_test_state_t test;
    unsigned remaining_s;
    char reply[96];
#endif
} chronvs_gps_snapshot_t;

/* UI task only. UART1 RX44/TX43 for NMEA and shared A-GNSS. UART0 console
 * profiles keep TX untouched and disable assistance. Reception while off is
 * reserved to recording/time owners; the diagnostic app alone follows AUTO. */
void chronvs_gps_request(bool enabled);
void chronvs_gps_poll(bool display_off);
void chronvs_gps_snapshot(chronvs_gps_snapshot_t *out);
/* Independent owners: never release another client's reception request. */
void chronvs_gps_recording_request(bool enabled);
void chronvs_gps_clock_request(bool enabled);
bool chronvs_gps_background_active(void);
bool chronvs_gps_assist_request(bool force);
void chronvs_gps_workout_active(bool enabled);
bool chronvs_gps_workout_is_active(void);

#ifdef CHRONVS_GPS_STANDBY_TEST
/* Experimental UART TX on GPIO43. No persistent changes to the receiver. */
bool chronvs_gps_query_version(void);
bool chronvs_gps_test_standby(void);
#endif
