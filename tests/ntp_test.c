#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
static unsigned gps_system_writes, gps_rtc_writes, observed;
static int fake_settimeofday(const struct timeval *tv, const void *zone) {
    (void)zone; assert(tv->tv_sec == 1794106799LL); ++gps_system_writes; return 0;
}
static struct tm *host_gmtime_r(const time_t *value, struct tm *out) {
    return gmtime_s(out, value) == 0 ? out : NULL;
}
#define gmtime_r host_gmtime_r
#define settimeofday fake_settimeofday
static int fake_setenv(const char *a,const char *b,int c) { (void)a;(void)b;(void)c;return 0; }
static void fake_tzset(void) {}
static struct tm *fake_localtime(const time_t *t,struct tm *out) {
    (void)t; *out=(struct tm){.tm_year=126,.tm_mon=8,.tm_mday=12,.tm_hour=9,.tm_min=34,.tm_sec=56,.tm_wday=6};return out;
}
static const char *esp_err_to_name(int err) { (void)err;return "mock"; }
#define setenv fake_setenv
#define tzset fake_tzset
#define localtime_r fake_localtime
#include "../src/services/time_sync_service.c"

static int64_t now_us;
static bool configured, task_fail, wifi_fail, timeout, rtc_fail, owns_wifi;
static unsigned created, deleted, acquired, released, ntp_starts, ntp_stops, rtc_writes;
static void (*entry)(void *);
static bool gps_requested;
static chronvs_gps_snapshot_t gps;
void chronvs_gps_clock_request(bool value) { gps_requested = value; }
void chronvs_gps_snapshot(chronvs_gps_snapshot_t *out) { *out = gps; }
void chronvs_Relogio_observe_time(const chronvs_time_t *t) {
    assert(t->valid && t->year == 26 && t->month == 11 && t->day == 8 &&
        t->hour == 0 && t->minute == 1 && t->second == 1); ++observed;
}
int64_t esp_timer_get_time(void) { return now_us; }
bool chronvs_wifi_session_configured(void) { return configured; }
int chronvs_wifi_session_acquire(void) {
    ++acquired; assert(chronvs_time_sync_active());
    /* Represents the interval waiting for Clima's exclusive session. */
    chronvs_time_sync_poll(); assert(entry);
    if (wifi_fail) return ESP_FAIL;
    owns_wifi=true; return ESP_OK;
}
void chronvs_wifi_session_release(void) { assert(owns_wifi); owns_wifi=false; ++released; }
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)name;(void)arg;(void)priority;(void)handle; assert(stack==6144); ++created;
    assert(chronvs_time_sync_active());
    if (task_fail) return 0;
    entry=fn;return pdPASS;
}
void vTaskDelete(void *task) { (void)task; assert(!owns_wifi && !chronvs_time_sync_active()); ++deleted; }
void vTaskDelay(unsigned ticks) { assert(ticks==50); now_us+=(int64_t)ticks*10000; }
void esp_sntp_setoperatingmode(int m) { (void)m; }
void esp_sntp_set_sync_mode(int m) { (void)m; }
void esp_sntp_setservername(int i,const char *name) { assert(i==0 && !strcmp(name,"pool.ntp.org")); }
void esp_sntp_init(void) { assert(owns_wifi); ++ntp_starts; }
void esp_sntp_stop(void) { assert(owns_wifi); ++ntp_stops; }
int esp_sntp_get_sync_status(void) { return timeout ? 0 : SNTP_SYNC_STATUS_COMPLETED; }
int i2c_master_write_to_device(int bus,uint8_t addr,const uint8_t *data,size_t len,unsigned ticks) {
    (void)bus; assert(addr==0x51 && len==8 && ticks==25);
    if (!owns_wifi) {
        const uint8_t expected[]={4,0x01,gps_rtc_writes ? 1 : 0,0,0x08,0,0x11,0x26};
        assert(!memcmp(data, expected, len)); ++gps_rtc_writes; return rtc_fail ? ESP_FAIL : ESP_OK;
    }
    const uint8_t expected[]={4,0x56,0x34,0x09,0x12,6,0x09,0x26};
    assert(!memcmp(data,expected,len)); ++rtc_writes;return rtc_fail ? ESP_FAIL : ESP_OK;
}
static void run_worker(void) { assert(entry); entry(NULL); entry=NULL; }
static void due(void) { now_us+=(int64_t)SYNC_PERIOD_MS*1000; chronvs_time_sync_poll(); }
int main(void) {
    chronvs_time_t result;
    chronvs_time_sync_start(); chronvs_time_sync_poll();
    int64_t trusted_utc;
    assert(!chronvs_time_sync_utc(&trusted_utc));
    assert(!created && chronvs_time_sync_next_wake_ms(300000)==300000);
    configured=true; chronvs_time_sync_start();
    assert(created==1 && chronvs_time_sync_active());
    chronvs_time_sync_start(); chronvs_time_sync_poll(); assert(created==1);
    run_worker(); assert(deleted==1 && released==1);
    assert(chronvs_time_sync_utc(&trusted_utc) && trusted_utc>=1483228800LL);
    /* Leave the successful result pending through a subsequent failure. */
    now_us+=(int64_t)SYNC_PERIOD_MS*1000-1500;
    assert(chronvs_time_sync_next_wake_ms(300000)==2);
    chronvs_time_sync_poll(); assert(created==1);
    now_us+=1500; wifi_fail=true; chronvs_time_sync_poll(); run_worker();
    assert(created==2 && deleted==2 && released==1);
    assert(chronvs_time_sync_take_update(&result) && result.valid && result.hour==9);
    assert(!chronvs_time_sync_take_update(&result));
    wifi_fail=false; timeout=true; due(); run_worker();
    assert(ntp_starts==2 && ntp_stops==2 && rtc_writes==1 && released==2);
    assert(!chronvs_time_sync_take_update(&result));
    timeout=false; rtc_fail=true; due(); run_worker();
    assert(released==3 && !chronvs_time_sync_take_update(&result));
    rtc_fail=false; task_fail=true; due();
    unsigned before=created;
    assert(!chronvs_time_sync_active() && chronvs_time_sync_next_wake_ms(300000)==60000);
    chronvs_time_sync_poll(); assert(created==before);
    now_us+=59999999; chronvs_time_sync_poll(); assert(created==before);
    task_fail=false; ++now_us; chronvs_time_sync_poll(); run_worker();
    assert(created==before+1 && chronvs_time_sync_take_update(&result));
    /* A long sleep causes one session, without catch-up bursts. */
    now_us+=(int64_t)SYNC_PERIOD_MS*1000*3; chronvs_time_sync_poll(); run_worker();
    before=created; chronvs_time_sync_poll(); assert(created==before);
    assert(chronvs_time_sync_next_wake_ms(300000)==300000);
    assert(chronvs_time_sync_next_wake_ms(0)==0 && acquired==deleted);
    chronvs_time_sync_gps_poll(false);
    assert(!gps_requested && !gps_system_writes); /* Recent NTP takes precedence. */
    now_us += (int64_t)SYNC_PERIOD_MS*1000;
    worker_active = true; chronvs_time_sync_gps_poll(false);
    assert(!gps_requested); /* Never race the NTP worker's RTC write. */
    worker_active = false;
    chronvs_time_sync_gps_poll(true); assert(gps_requested);
    now_us += 120000001; chronvs_time_sync_gps_poll(true);
    assert(!gps_requested && !gps_system_writes); /* Bounded search with no antenna/signal. */
    now_us += 3600000000LL; chronvs_time_sync_gps_poll(true); assert(gps_requested);
    gps = (chronvs_gps_snapshot_t){.running=true,.data={.fix=true,.utc_valid=true,
        .utc=1794106799LL,.utc_ms=(uint64_t)now_us/1000}};
    chronvs_time_sync_gps_poll(true);
    assert(!gps_requested && gps_system_writes == 1 && !gps_rtc_writes && !observed);
    now_us += 2000000; rtc_fail = true; chronvs_time_sync_gps_poll(false);
    assert(gps_rtc_writes == 1 && observed == 0); /* UTC-3 crosses midnight correctly. */
    chronvs_time_sync_gps_poll(false); assert(gps_rtc_writes == 1);
    now_us += 59000000; chronvs_time_sync_gps_poll(false); assert(gps_rtc_writes == 1);
    now_us += 1000000; rtc_fail = false; chronvs_time_sync_gps_poll(false);
    assert(gps_rtc_writes == 2 && observed == 1);
    chronvs_time_sync_gps_poll(false); assert(gps_system_writes == 1 && gps_rtc_writes == 2);
    puts("NTP: temporary task, deadlines/sleep, pending result, Wi-Fi/NTP/RTC failures and allocation retry passed.");
    return 0;
}
