#include <assert.h>
#include <stdlib.h>
#include <time.h>
#define main agnss_data_test_main
#include "agnss_data_test.c"
#undef main
static int64_t test_us, epoch=1794054920;
static time_t fake_time(time_t *out) { time_t n=(time_t)(epoch+test_us/1000000); if(out)*out=n;return n; }
#define time fake_time
#include "services/agnss_service.c"

static bool configured=true, trusted, wifi_fails, large, bad_base64, http_fails;
static bool owned, room=true, chunked_large;
static unsigned tasks, releases, writes, aid_writes, reads;
static unsigned queries, sv_writes[32];
static uint8_t last_frame[32][82];
static int64_t last_sent_us[32];
bool chronvs_sd_mount(void) { return false; }
bool chronvs_agnss_sources_cached(bool sd,bool igs,int64_t utc,uint8_t **out,size_t *size,const char **source) {
    (void)sd;(void)igs;(void)utc;(void)out;(void)size;(void)source;return false;
}
const char *chronvs_agnss_sources_fetch(bool sd,bool igs,uint8_t **out,size_t *size,const char **source,void(*memory_sample)(const char *)) {
    (void)sd;memory_sample("test TLS");
    if(large || chunked_large || http_fails || bad_base64) { reads=17;return "Download inválido"; }
    *out=malloc(1024);assert(*out);*size=fixture(*out,epoch+test_us/1000000);*source=igs?"IGS sem SD":"Previsão sem SD";return "";
}
static void (*task_entry)(void *);
int64_t esp_timer_get_time(void) { return test_us; }
bool chronvs_time_sync_utc(int64_t *out) { if(trusted)*out=epoch+test_us/1000000;return trusted; }
bool chronvs_wifi_session_configured(void) { return configured; }
int chronvs_wifi_session_acquire(void) { assert(chronvs_agnss_active());if(wifi_fails)return ESP_FAIL; owned=true;return ESP_OK; }
void chronvs_wifi_session_release(void) { assert(owned); owned=false;++releases; }
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)name;(void)arg;(void)priority;(void)handle;assert(stack==8192);++tasks;task_entry=entry;return pdPASS;
}
void vTaskDelete(void *task) { (void)task;assert(!owned && !atomic_load(&worker_active)); }
void *heap_caps_malloc(size_t size,unsigned caps) { assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return malloc(size); }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps;return 40000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps;return 20000; }
int esp_crt_bundle_attach(void *p) { (void)p;return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    assert(c->crt_bundle_attach==esp_crt_bundle_attach && c->disable_auto_redirect && c->buffer_size==1024);
    reads=0;return (void *)1;
}
int esp_http_client_open(void *client,int size) { (void)client;assert(!size && owned);return http_fails?ESP_FAIL:ESP_OK; }
int esp_http_client_set_timeout_ms(void *client,int ms) { (void)client;assert(ms>0);return ESP_OK; }
int64_t esp_http_client_fetch_headers(void *client) { (void)client;return chunked_large?0:large?CHRONVS_AGNSS_BODY_LIMIT+1:5; }
int esp_http_client_get_status_code(void *client) { (void)client;return 200; }
int esp_http_client_read(void *client,char *out,int max) {
    (void)client;++reads;test_us+=1000;
    if(chunked_large) { memset(out,'A',max);return max; }
    assert(max>=5);
    if(reads==1)return -ESP_ERR_HTTP_EAGAIN;
    memcpy(out,"VALID",5);return 5;
}
bool esp_http_client_is_complete_data_received(void *client) { (void)client;return !chunked_large && reads>1; }
int esp_http_client_close(void *client) { (void)client;return ESP_OK; }
int esp_http_client_cleanup(void *client) { (void)client;return ESP_OK; }
int mbedtls_base64_decode(unsigned char *out,size_t max,size_t *size,const unsigned char *input,size_t len) {
    assert(len==5 && !memcmp(input,"VALID",5) && max==CHRONVS_AGNSS_BINARY_LIMIT);
    if(bad_base64)return -1;
    *size=fixture(out,epoch+test_us/1000000);return 0;
}
static int uart(const uint8_t *bytes,size_t size) {
    assert(!owned && chronvs_casic_valid(bytes,size));
    if(!room)return 0;
    ++writes;if(bytes[4]==0x0B)++aid_writes;
    if (bytes[4]==1) ++queries;
    if (size==82 && bytes[4]==8 && bytes[5]==7) {
        unsigned sv=bytes[74];assert(sv>=1 && sv<=32);
        if (retry_pending) { assert(!memcmp(last_frame[sv-1],bytes,82));assert(test_us-last_sent_us[sv-1]>=200000); }
        memcpy(last_frame[sv-1],bytes,82);++sv_writes[sv-1];
        last_sent_us[sv-1]=test_us;
    }
    assert(bytes[4]==0x0B || bytes[4]==8 || (bytes[4]==1 && bytes[5]==0));
    return (int)size;
}
static chronvs_gps_data_t gps;
static bool auto_ack=true;
static unsigned reject_sv, recover_sv, aid_reject;
static void reply(unsigned kind,unsigned cls,unsigned id) {
    uint8_t p[4]={(uint8_t)cls,(uint8_t)id,0,0},f[14];
    chronvs_casic_frame(f,5,kind,p,4);chronvs_agnss_receive(f,14);
}
static void poll(bool workout) {
    chronvs_agnss_poll(&gps,true,workout,uart);
    if(auto_ack && waiting_ack) {
        unsigned sv=pending_satellite();
        bool reject=sv && (reject_sv==33 || reject_sv==sv || (recover_sv==sv && attempt==1));
        if (ack_class==0x0B && aid_reject) reject=true;
        reply(reject?0:1,ack_class,ack_id);
    }
    test_us+=20000;
}
static void download(void) { assert(task_entry);void(*entry)(void*)=task_entry;task_entry=NULL;entry(NULL); }
int main(void) {
    configured=false;assert(!chronvs_agnss_request(false) && snapshot.state==AGNSS_ERROR);
    configured=true;assert(chronvs_agnss_request(true));poll(false);
    assert(snapshot.state==AGNSS_WAIT_TIME && !tasks);
    trusted=true;poll(false);assert(snapshot.state==AGNSS_DOWNLOAD && tasks==1 && chronvs_agnss_active());
    assert(!chronvs_agnss_request(true));download();
    assert(chronvs_agnss_active() && !chronvs_agnss_request(true)); /* Completion not consumed yet. */
    room=false;poll(false);assert(snapshot.state==AGNSS_READY && !writes);
    room=true;poll(false);assert(snapshot.state==AGNSS_SENDING && aid_writes==1 && awaiting_fix);
    for(unsigned i=0;i<200;++i)poll(false);
    assert(snapshot.state==AGNSS_SENT && snapshot.sent==6 && writes==7 && aid_writes==1 && !chronvs_agnss_active());
    assert(releases==1 && !owned);
    assert(snapshot.minimum_internal==40000 && snapshot.minimum_dma==20000);
    assert(!chronvs_agnss_request(false)); /* Automatic requests are throttled. */
    chronvs_agnss_confirm_fix();assert(!awaiting_fix);
    assert(chronvs_agnss_request(true));poll(true);
    assert(snapshot.state==AGNSS_DEFERRED && writes==7); /* No injection into active activity. */
    assert(chronvs_agnss_request(true));poll(false);
    assert(tasks==1 && aid_writes==2); /* Fresh cache avoids another network session. */
    for(unsigned i=0;i<200;++i)poll(false);
    uint8_t status_payload[80]={0}, status_frame[90];
    status_payload[8]=status_payload[9]=status_payload[10]=status_payload[11]=3;
    chronvs_casic_frame(status_frame,1,0,status_payload,80);chronvs_agnss_receive(status_frame,90);
    assert(snapshot.receiver_status && snapshot.receiver_ephemerides==4);
    assert(snapshot.gps_accepted==4 && snapshot.gps_rejected==0);
    /* First PRN fails all three attempts; later PRNs still get sent. Query the
     * partial batch before falling back. Pause immediately when fallback queues. */
    reject_sv=1;assert(chronvs_agnss_request(true));poll(false);
    for(unsigned i=0;i<1000 && !fallback_pending;++i)poll(false);
    assert(fallback_pending && snapshot.gps_accepted==3 && snapshot.gps_rejected==1);
    assert(snapshot.gps[0].attempts==3 && snapshot.gps[0].state==AGNSS_SV_REJECTED && snapshot.retries==2);
    assert(sv_writes[0]>=3 && queries>=3);
    assert(snapshot.gps[3].state==AGNSS_SV_ACCEPTED && query_sent);
    poll(false);assert(snapshot.state==AGNSS_DOWNLOAD && igs_only);
    download();poll(false);assert(snapshot.state==AGNSS_SENDING && strstr(snapshot.source,"IGS"));
    reject_sv=33;for(unsigned i=0;i<1500 && chronvs_agnss_active();++i)poll(false);
    assert(snapshot.state==AGNSS_ERROR && !fallback_pending && snapshot.gps_rejected==4 && snapshot.retries==8 && query_sent);
    assert(snapshot.gps_confirmed_total==3); /* Forecast confirmations survive IGS fallback. */
    chronvs_agnss_receiver_stopped(); assert(awaiting_fix); /* Receiver remains powered across UART sessions. */
    chronvs_agnss_confirm_fix();
    test_us+=10800000000LL;assert(chronvs_agnss_request(true));poll(false);assert(tasks==3);
    large=true;download();poll(false);assert(snapshot.state==AGNSS_ERROR && !owned);
    large=false;chunked_large=true;assert(chronvs_agnss_request(true));poll(false);download();poll(false);
    assert(snapshot.state==AGNSS_ERROR && reads==17 && !owned);
    chunked_large=false;wifi_fails=true;assert(chronvs_agnss_request(true));poll(false);download();poll(false);
    assert(snapshot.state==AGNSS_ERROR && !chronvs_agnss_active());
    wifi_fails=false;bad_base64=true;assert(chronvs_agnss_request(true));poll(false);download();poll(false);
    assert(snapshot.state==AGNSS_ERROR && !chronvs_agnss_active());
    bad_base64=false;assert(chronvs_agnss_request(true));poll(false);unsigned before=writes;
    poll(true);assert(snapshot.state==AGNSS_DOWNLOAD && chronvs_agnss_active());
    download();poll(true);assert(snapshot.state==AGNSS_DEFERRED && writes==before && !owned);
    gps.fix=gps.satellites_valid=true;gps.satellites=8;
    assert(chronvs_agnss_request(true));poll(false);assert(snapshot.state==AGNSS_FIX_READY && writes==before);
    gps.fix=false;
    reject_sv=0;recover_sv=1;
    assert(chronvs_agnss_request(true));poll(false);
    for(unsigned i=0;i<600 && chronvs_agnss_active();++i)poll(false);
    assert(snapshot.state==AGNSS_SENT && snapshot.gps_accepted==4 && snapshot.gps_rejected==0 && snapshot.retries==1);
    assert(snapshot.gps[0].attempts==2 && snapshot.gps[0].state==AGNSS_SV_ACCEPTED);
    assert(snapshot.gps_confirmed_total==4); /* Retry never double-counts a PRN. */
    /* Starting a workout during a pending retry prevents that retry and query. */
    assert(chronvs_agnss_request(true));auto_ack=false;poll(false);reply(1,0x0B,1);
    poll(false);reply(0,8,7);assert(retry_pending);
    unsigned workout_writes=writes;test_us+=200000;poll(true);
    assert(snapshot.state==AGNSS_DEFERRED && writes==workout_writes && !chronvs_agnss_active());
    /* Unrelated and unknown ACKs never advance the pending frame. */
    recover_sv=0;assert(chronvs_agnss_request(true));auto_ack=false;poll(false);
    assert(waiting_ack);reply(1,8,7);reply(2,0x0B,1);assert(waiting_ack);
    reply(1,0x0B,1);poll(false);assert(waiting_ack && pending_satellite()==1);
    unsigned held=writes;test_us+=1500000;poll(false);
    assert(finishing && snapshot.gps_unconfirmed==1 && snapshot.gps[0].state==AGNSS_SV_UNCONFIRMED);
    for(unsigned i=0;i<200 && chronvs_agnss_active();++i)poll(false);
    assert(snapshot.state==AGNSS_ERROR && writes==held+1 && query_sent); /* Status only; no next PRN after silence. */
    assert(chronvs_agnss_request(true));auto_ack=false;poll(false);
    assert(waiting_ack);held=writes;
    for(unsigned i=0;i<100;++i)poll(false);
    for(unsigned i=0;i<200 && chronvs_agnss_active();++i)poll(false);
    assert(snapshot.state==AGNSS_ERROR && writes==held+1); /* Aid silence -> status, no GPS packet. */
    auto_ack=true;
    aid_reject=1;assert(chronvs_agnss_request(true));poll(false);
    for(unsigned i=0;i<200 && chronvs_agnss_active();++i)poll(false);
    assert(snapshot.state==AGNSS_ERROR && snapshot.gps_accepted==0 && snapshot.sent==1 && query_sent && !fallback_pending);
    aid_reject=0;
    trusted=false;assert(chronvs_agnss_request(true));test_us+=90000000;poll(false);
    assert(snapshot.state==AGNSS_ERROR && !chronvs_agnss_active());
    free(cache);cache=NULL;
    puts("AGNSS service: per-PRN bounded NACK retry, recovery, full partial/rejected batches, status before fallback, timeout correlation, workout and source errors passed");
    return 0;
}
