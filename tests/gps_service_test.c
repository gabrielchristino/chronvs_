#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "services/gps_service.c"

static uint64_t now_ms;
static bool installed, fail_install;
static unsigned baud_changes, reads, deletes;
static char incoming[512];
static size_t queued;
static char sent[512];
static bool fail_tx;
static bool tx_busy;
static bool assist_pending;
static unsigned binary_frames;
bool chronvs_agnss_request(bool force) { (void)force; return true; }
bool chronvs_agnss_active(void) { return false; }
void chronvs_agnss_receiver_stopped(void) {}
bool chronvs_agnss_fix_pending(void) { return assist_pending; }
void chronvs_agnss_confirm_fix(void) { assist_pending=false; }
void chronvs_agnss_poll(const chronvs_gps_data_t *data,bool ready,bool workout,chronvs_agnss_write_t write) {
    (void)data;(void)ready;(void)workout;(void)write;
}
void chronvs_agnss_receive(const uint8_t *frame,size_t size) { if(chronvs_casic_valid(frame,size))++binary_frames; }
int uart_wait_tx_done(int port,uint32_t ticks) { assert(port==UART_NUM_1 && !ticks);return tx_busy?ESP_ERR_TIMEOUT:ESP_OK; }
int uart_write_bytes(int port, const void *data, size_t size) {
    assert(port == UART_NUM_1 && installed && size < sizeof(sent));
    memcpy(sent, data, size); sent[size] = 0;
    return fail_tx ? -1 : (int)size;
}
int64_t esp_timer_get_time(void) { return now_ms * 1000; }
int uart_param_config(int port, const uart_config_t *config) {
    assert(port == UART_NUM_1 && config->baud_rate == 9600); return ESP_OK;
}
int uart_set_pin(int port, int tx, int rx, int rts, int cts) {
    assert(port == UART_NUM_1 && rx == 44 && rts == -1 && cts == -1);
    assert(tx == 43);
    return ESP_OK;
}
int uart_driver_install(int port, int rx, int tx, int queue, void *handle, int flags) {
    assert(port == UART_NUM_1 && rx == 4096 && tx==512 && !queue && !handle && !flags);
    if (fail_install) return -1;
    assert(!installed); installed = true; return ESP_OK;
}
int uart_driver_delete(int port) {
    assert(port == UART_NUM_1 && installed); installed = false; queued = 0; ++deletes; return ESP_OK;
}
int gpio_pullup_en(int pin) { assert(pin == 44); return ESP_OK; }
int uart_read_bytes(int port, void *buffer, uint32_t size, uint32_t timeout) {
    assert(port == UART_NUM_1 && installed && !timeout); ++reads;
    size_t n = queued < size ? queued : size;
    memcpy(buffer, incoming, n); memmove(incoming, incoming + n, queued - n); queued -= n;
    return (int)n;
}
int uart_set_baudrate(int port, uint32_t baud) {
    assert(port == UART_NUM_1 && installed && baud); ++baud_changes; return ESP_OK;
}
int uart_flush_input(int port) { assert(port == UART_NUM_1); queued = 0; return ESP_OK; }
static void feed(const char *text) {
    queued = strlen(text); assert(queued < sizeof(incoming)); memcpy(incoming, text, queued);
    chronvs_gps_poll(false);
}
int main(void) {
    chronvs_gps_poll(false); assert(!installed && reads == 0);
    chronvs_gps_request(true); chronvs_gps_poll(false); assert(installed && state.baud == 9600);
    now_ms = 3000; chronvs_gps_poll(false); assert(baud_changes == 1 && state.baud == 38400);
    /* Classic checksum fixture split across polls, with garbage before $. */
    feed("noise$GPGGA,123519,4807.038,N,01131.000,E,1,08,"); assert(!state.data.fix);
    feed("0.9,545.4,M,46.9,M,,*47\r\n");
    assert(state.serial_fresh && state.data.fix && locked);
#ifdef CHRONVS_GPS_STANDBY_TEST
    assert(chronvs_gps_query_version()); assert(!strcmp(sent, "$PCAS06,0*1B\r\n"));
    const char *txt = "GPTXT,01,01,02,SW=TEST-5N31";
    unsigned checksum = 0;
    for (const char *p = txt; *p; ++p) checksum ^= (unsigned char)*p;
    char answer[96]; snprintf(answer, sizeof(answer), "$%s*%02X\r\n", txt, checksum);
    feed(answer); assert(!strcmp(state.reply, "SW=TEST-5N31"));
    assert(chronvs_gps_query_version());
    now_ms += 5000; chronvs_gps_poll(false);
    assert(!strcmp(state.reply, "Sem resposta à consulta"));
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(chronvs_gps_test_standby()); assert(!strcmp(sent, "$PCAS12,60*28\r\n"));
    assert(!state.data.fix && state.test == GPS_TEST_OBSERVING);
    assert(!chronvs_gps_test_standby() && !chronvs_gps_query_version());
    now_ms += 3000; chronvs_gps_poll(false); assert(state.test == GPS_TEST_SILENT);
    now_ms += 57000;
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(state.test == GPS_TEST_RETURNED);
    assert(chronvs_gps_test_standby());
    now_ms += 1500;
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(state.test == GPS_TEST_CONTINUED);
    assert(chronvs_gps_test_standby());
    now_ms += 70000; chronvs_gps_poll(false); assert(state.test == GPS_TEST_NO_RETURN);
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(chronvs_gps_test_standby()); chronvs_gps_poll(true);
    assert(state.test == GPS_TEST_INTERRUPTED && !installed);
    chronvs_gps_poll(false);
    assert(state.test == GPS_TEST_INTERRUPTED);
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    fail_tx = true; assert(!chronvs_gps_test_standby()); assert(state.test == GPS_TEST_TX_ERROR);
    fail_tx = false;
    deletes = 0; baud_changes = 1;
#endif
    now_ms += 6000; chronvs_gps_poll(false);
    assert(!state.serial_fresh && !state.data.fix && baud_changes == 1);
    unsigned before = reads;
    chronvs_gps_poll(true); assert(!installed && !state.running && deletes == 1);
    chronvs_gps_poll(true); assert(reads == before && deletes == 1);
    chronvs_gps_poll(false); assert(installed && !state.data.fix && !state.data.sentences);
    chronvs_gps_request(false); chronvs_gps_poll(false); assert(!installed);
    fail_install = true; chronvs_gps_request(true); chronvs_gps_poll(false);
    assert(state.error && !installed);
    fail_install = false; chronvs_gps_poll(false); assert(!installed); /* No tight retry loop. */
    chronvs_gps_request(false); chronvs_gps_request(true); chronvs_gps_poll(false);
    assert(installed && !state.error);
    chronvs_gps_recording_request(true);
    chronvs_gps_request(false); chronvs_gps_poll(true);
    assert(installed && chronvs_gps_background_active());
#ifdef CHRONVS_GPS_STANDBY_TEST
    state.serial_fresh = true;
    assert(!chronvs_gps_test_standby() && !chronvs_gps_query_version());
#endif
    chronvs_gps_clock_request(true); chronvs_gps_recording_request(false);
    chronvs_gps_poll(true); assert(installed);
    chronvs_gps_clock_request(false); chronvs_gps_poll(true);
    assert(!installed && !chronvs_gps_background_active());
    chronvs_gps_request(true); chronvs_gps_poll(false);
    uint8_t ack_payload[4]={0x0B,1,0x24,0xBA}, ack[14];
    chronvs_casic_frame(ack,5,1,ack_payload,4); /* '$' and BA inside binary aren't NMEA. */
    memcpy(incoming,ack,7);queued=7;chronvs_gps_poll(false);assert(binary_frames==0);
    memcpy(incoming,ack+7,7);queued=7;chronvs_gps_poll(false);assert(binary_frames==1 && state.data.errors==0);
    assist_pending=true;
    const char *rmc="GPRMC,123520,A,4807.038,N,01131.000,E,10,0,071126,,,A";
    unsigned sum=0;for(const char *p=rmc;*p;++p)sum^=(unsigned char)*p;
    char body[180];snprintf(body,sizeof(body),"$%s*%02X\r\n",rmc,sum);
    feed(body);assert(assist_pending && !state.data.fix && !state.data.speed_valid);
    feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(!assist_pending && state.data.fix);
    tx_busy=true;assert(assist_write(ack,sizeof(ack))==0);
    tx_busy=false;assert(assist_write(ack,sizeof(ack))==(int)sizeof(ack));
    puts("GPS UART: RX/TX, mixed binary/NMEA, real-fix gate, zero-wait TX, baud, loss, sleep and retry passed");
    return 0;
}
