#include "services/gps_service.h"
#include "services/agnss_service.h"
#include "services/agnss_data.h"
#include <string.h>
#include <stdio.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#ifdef CHRONVS_GPS_ASSIST_DIAGNOSTIC
#include "esp_log.h"
#endif

#if defined(CHRONVS_GPS_STANDBY_TEST) && (defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM))
#error GPS standby test needs UART console disabled: GPIO43 is GPS TX
#endif

enum { GPS_RX = 44, RX_BUFFER = 4096 };
static const unsigned rates[] = {9600, 38400, 115200, 4800, 19200, 57600};
static chronvs_gps_snapshot_t state;
static bool requested, locked;
static bool recording_requested, clock_requested;
static bool workout_active;
static uint8_t binary[CHRONVS_CASIC_FRAME_LIMIT];
static size_t binary_length, binary_target;
static uint64_t binary_since, uart_since;
#if !defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) && !defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#define CHRONVS_GPS_ASSIST_TX 1
#endif
void chronvs_gps_workout_active(bool enabled) { workout_active=enabled; }
bool chronvs_gps_workout_is_active(void) { return workout_active; }
bool chronvs_gps_assist_request(bool force) {
#ifdef CHRONVS_GPS_ASSIST_TX
    if (workout_active) return false;
#ifdef CHRONVS_GPS_STANDBY_TEST
    /* The test's full state guard is also applied at the UART send site. */
    if (state.test==GPS_TEST_OBSERVING || state.test==GPS_TEST_SILENT) return false;
#endif
    return chronvs_agnss_request(force);
#else
    (void)force; return false; /* UART0 diagnostics own GPIO43. */
#endif
}
static int assist_write(const uint8_t *bytes, size_t size) {
#ifdef CHRONVS_GPS_ASSIST_TX
    if (workout_active) return 0;
#ifdef CHRONVS_GPS_STANDBY_TEST
    if (state.test==GPS_TEST_OBSERVING || state.test==GPS_TEST_SILENT) return 0;
#endif
    /* One small frame at a time. Zero-tick completion probes never wait for
     * the serial wire. IDF 5.3.1's free-size API does not count all ring items. */
    esp_err_t ready=uart_wait_tx_done(UART_NUM_1,0);
    if (ready==ESP_ERR_TIMEOUT) return 0;
    if (ready!=ESP_OK) return -1;
    return uart_write_bytes(UART_NUM_1,bytes,size);
#else
    (void)bytes; (void)size; return -1;
#endif
}

void chronvs_gps_recording_request(bool enabled) {
    if (enabled && !recording_requested) state.error = false;
    recording_requested = enabled;
}
void chronvs_gps_clock_request(bool enabled) {
    if (enabled && !clock_requested) state.error = false;
    clock_requested = enabled;
}
bool chronvs_gps_background_active(void) { return recording_requested || clock_requested || chronvs_agnss_active(); }
static unsigned candidate;
static uint64_t since;
static char line[160];
static size_t length;
static uint64_t milliseconds(void) { return (uint64_t)esp_timer_get_time() / 1000; }

#ifdef CHRONVS_GPS_STANDBY_TEST
static uint64_t test_started, query_started;
static bool query_pending;
static bool testing(void) {
    return state.test == GPS_TEST_OBSERVING || state.test == GPS_TEST_SILENT;
}
static bool send_command(const char *command) {
    size_t size = strlen(command);
    return uart_write_bytes(UART_NUM_1, command, size) == (int)size;
}
bool chronvs_gps_query_version(void) {
    if (!state.running || !state.serial_fresh || testing() || recording_requested || chronvs_agnss_active()) return false;
    if (!send_command("$PCAS06,0*1B\r\n")) { state.test = GPS_TEST_TX_ERROR; return false; }
    snprintf(state.reply, sizeof(state.reply), "Aguardando resposta...");
    query_started = milliseconds(); query_pending = true;
    return true;
}
bool chronvs_gps_test_standby(void) {
    if (!state.running || !state.serial_fresh || testing() || recording_requested || chronvs_agnss_active()) return false;
    /* Already queued NMEA must not count as a response to this command. */
    uart_flush_input(UART_NUM_1); length = 0;
    if (!send_command("$PCAS12,60*28\r\n")) { state.test = GPS_TEST_TX_ERROR; return false; }
    state.test = GPS_TEST_OBSERVING; state.remaining_s = 60;
    test_started = milliseconds(); query_pending = false;
    /* Suppress pre-command coordinates immediately. */
    memset(&state.data, 0, sizeof(state.data));
    state.serial_fresh = false;
    return true;
}
static void test_sentence(uint64_t now) {
    if (query_pending && strlen(line) > 7 && !strncmp(line + 3, "TXT,", 4)) {
        const char *payload = line;
        for (unsigned i = 0; i < 4 && payload; ++i) {
            payload = strchr(payload, ','); if (payload) ++payload;
        }
        if (payload) {
            size_t count = strcspn(payload, "*");
            if (count >= sizeof(state.reply)) count = sizeof(state.reply) - 1;
            memcpy(state.reply, payload, count); state.reply[count] = 0;
            query_pending = false;
        }
    }
    uint64_t elapsed = now - test_started;
    /* Allow one second for UART/FIFO output already in flight. */
    if (testing() && elapsed >= 1000) {
        state.test = elapsed >= 55000 && state.test == GPS_TEST_SILENT ?
            GPS_TEST_RETURNED : GPS_TEST_CONTINUED;
        state.remaining_s = 0;
    }
}
static void test_poll(uint64_t now) {
    if (query_pending && now - query_started >= 5000) {
        snprintf(state.reply, sizeof(state.reply), "Sem resposta à consulta");
        query_pending = false;
    }
    if (!testing()) return;
    uint64_t elapsed = now - test_started;
    state.remaining_s = elapsed >= 60000 ? 0 : (unsigned)((60000 - elapsed + 999) / 1000);
    if (elapsed >= 70000) { state.test = GPS_TEST_NO_RETURN; state.remaining_s = 0; }
    else if (elapsed >= 3000) state.test = GPS_TEST_SILENT;
}
#endif

void chronvs_gps_request(bool enabled) {
    if (enabled && !requested) state.error = false;
    requested = enabled;
}

static void stop(void) {
#ifdef CHRONVS_GPS_STANDBY_TEST
    if (testing()) { state.test = GPS_TEST_INTERRUPTED; state.remaining_s = 0; }
    query_pending = false;
#endif
    if (state.running) uart_driver_delete(UART_NUM_1);
    chronvs_agnss_receiver_stopped(); binary_length=binary_target=0;
    state.running = state.serial_fresh = false;
    state.data.fix = state.data.altitude_valid = state.data.speed_valid = false;
    state.data.satellites_valid = state.data.hdop_valid = false;
    state.data.utc_valid = false;
    memset(state.data.sky, 0, sizeof(state.data.sky));
    length = 0;
}

void chronvs_gps_poll(bool display_off) {
    chronvs_agnss_poll(&state.data, state.running && state.serial_fresh &&
        milliseconds()-uart_since>=1000, workout_active, assist_write);
    if (chronvs_agnss_fix_pending()) state.data.fix=state.data.speed_valid=state.data.altitude_valid=false;
    if (!chronvs_gps_background_active() && (!requested || display_off)) { stop(); return; }
    uint64_t now = milliseconds();
    if (!state.running) {
        if (state.error) return;
#ifdef CHRONVS_GPS_STANDBY_TEST
        chronvs_gps_test_state_t previous_test = state.test;
        char previous_reply[sizeof(state.reply)]; memcpy(previous_reply, state.reply, sizeof(previous_reply));
#endif
        memset(&state, 0, sizeof(state));
#ifdef CHRONVS_GPS_STANDBY_TEST
        state.test = previous_test;
        memcpy(state.reply, previous_reply, sizeof(state.reply));
#endif
        candidate = 0; locked = false;
        uart_config_t config = {.baud_rate = rates[0], .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
#if defined(CHRONVS_GPS_STANDBY_TEST) || defined(CHRONVS_GPS_ASSIST_TX)
        const int tx = 43;
        const int tx_buffer = 512;
#else
        /* GPIO43 stays untouched, including when diagnostic UART0 is enabled. */
        const int tx = UART_PIN_NO_CHANGE;
        const int tx_buffer = 0;
#endif
        if (uart_param_config(UART_NUM_1, &config) != ESP_OK ||
            uart_set_pin(UART_NUM_1, tx, GPS_RX,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
            uart_driver_install(UART_NUM_1, RX_BUFFER, tx_buffer, 0, NULL, 0) != ESP_OK) {
            state.error = true; return;
        }
        gpio_pullup_en(GPS_RX);
        state.running = true; state.baud = rates[0]; since = now;
        uart_since=now;
    }
    char bytes[128];
    if (binary_length && now-binary_since>2000) binary_length=binary_target=0;
    /* Bounded nonblocking work; no extra task, stack or LVGL from UART callbacks. */
    for (unsigned budget = 0; budget < 1024; budget += sizeof(bytes)) {
        int count = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), 0);
        if (count <= 0) break;
        state.bytes += count;
        for (int i = 0; i < count; ++i) {
            unsigned char c = (unsigned char)bytes[i];
            if (binary_length) {
                if (binary_length==1 && c!=0xCE) { binary_length=0; }
                else {
                    binary[binary_length++]=c;
                    if (binary_length==6) {
                        binary_target=10+binary[2]+((size_t)binary[3]<<8);
                        if (binary_target>sizeof(binary) || (binary_target-10)%4) binary_length=binary_target=0;
                    }
                    if (binary_target && binary_length==binary_target) {
#ifdef CHRONVS_GPS_ASSIST_DIAGNOSTIC
                        ESP_LOGI("gps_rx","CASIC class=%02X id=%02X bytes=%u valid=%d",binary[4],binary[5],(unsigned)binary_length,chronvs_casic_valid(binary,binary_length));
#endif
                        chronvs_agnss_receive(binary,binary_length); binary_length=binary_target=0;
                    }
                    continue;
                }
            }
            if (c==0xBA) { binary[0]=c; binary_length=1; binary_target=0; binary_since=now; length=0; continue; }
            if (c == '$') { length = 0; line[length++] = c; }
            else if (c == '\n') {
                if (length) {
                    line[length] = 0;
                    if (chronvs_gps_parse(&state.data, line, now)) {
                        locked = true;
                        if (chronvs_agnss_fix_pending()) {
                            if (!strncmp(line+3,"GGA,",4) && state.data.fix &&
                                state.data.satellites_valid && state.data.satellites>=4)
                                chronvs_agnss_confirm_fix();
                            else state.data.fix=state.data.speed_valid=state.data.altitude_valid=false;
                        }
#ifdef CHRONVS_GPS_STANDBY_TEST
                        test_sentence(now);
#endif
                    }
                }
                length = 0;
            } else if (c != '\r' && length) {
                if (c < 32 || c > 126 || length >= sizeof(line) - 1) length = 0;
                else line[length++] = c;
            }
        }
    }
    if (!locked && now - since >= 3000) {
        candidate = (candidate + 1) % (sizeof(rates) / sizeof(rates[0]));
        if (uart_set_baudrate(UART_NUM_1, rates[candidate]) != ESP_OK) {
            stop(); state.error = true; return;
        }
        uart_flush_input(UART_NUM_1); length = 0;
        state.baud = rates[candidate]; since = now;
    }
    chronvs_gps_freshen(&state.data, now);
    state.serial_fresh = state.data.sentences && now - state.data.sentence_ms < 5000;
#ifdef CHRONVS_GPS_STANDBY_TEST
    test_poll(now);
#endif
}

void chronvs_gps_snapshot(chronvs_gps_snapshot_t *out) { *out = state; }
