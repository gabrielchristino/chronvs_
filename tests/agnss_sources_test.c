#define main original_agnss_tests
#include "agnss_data_test.c"
#undef main
#include "services/agnss_orbits.h"
#include <io.h>
#include <time.h>
static struct tm *host_gmtime(const time_t *epoch,struct tm *out) { return gmtime_s(out,epoch)==0?out:NULL; }
#define gmtime_r host_gmtime
#define fsync _commit
static int64_t clock_us;
static time_t epoch=1794509154;
static time_t fake_time(time_t *p) { if(p)*p=epoch;return epoch; }
#define time fake_time
#define CHRONVS_AGNSS_DIRECTORY ".pio/host-tests/agnss-sd"
#include "services/agnss_sources.c"

static uint8_t response[1024];
static size_t response_size, offset;
static unsigned requests, samples, prediction_requests, igs_requests;
static bool prediction_bad, igs_bad, declared_large, chunked, oversized, eagain;
void *heap_caps_malloc(size_t n,unsigned caps) { assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return malloc(n); }
int64_t esp_timer_get_time(void) { return clock_us; }
int esp_crt_bundle_attach(void *p) { (void)p;return 0; }
static void memory_sample(const char *p) { assert(p);++samples; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    assert(c->crt_bundle_attach==esp_crt_bundle_attach && c->disable_auto_redirect);
    assert(c->buffer_size==1024 && c->buffer_size_tx==512 && c->timeout_ms==15000);
    ++requests;offset=0;eagain=true;
    bool pred=strstr(c->url,"gps-7d-casic.bin")!=NULL;
    if(pred) { ++prediction_requests;response_size=fixture(response,epoch)-26;if(prediction_bad)response[20]^=1; }
    else { ++igs_requests;assert(strstr(c->url,"BRDC00WRD_S_"));memcpy(response,igs_bad?"BAD":"IGS",3);response_size=3; }
    return (void*)1;
}
int esp_http_client_open(void *p,int n) { assert(p && !n);return ESP_OK; }
int64_t esp_http_client_fetch_headers(void *p) { (void)p;return declared_large?CHRONVS_AGNSS_GZIP_LIMIT+1:chunked?0:(int64_t)response_size; }
int esp_http_client_get_status_code(void *p) { (void)p;return 200; }
int esp_http_client_set_timeout_ms(void *p,int n) { (void)p;assert(n>0);return 0; }
int esp_http_client_read(void *p,char *out,int max) {
    (void)p;clock_us+=1000;
    if(eagain) { eagain=false;return -ESP_ERR_HTTP_EAGAIN; }
    if(oversized) { memset(out,0,max);offset+=max;return max; }
    size_t n=response_size-offset;if(n>(size_t)max)n=max;if(n>71)n=71;
    memcpy(out,response+offset,n);offset+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(void *p) { (void)p;return !oversized && offset==response_size; }
int esp_http_client_close(void *p) { (void)p;return 0; }
int esp_http_client_cleanup(void *p) { (void)p;return 0; }
bool chronvs_agnss_gzip_rinex(const uint8_t *b,size_t n,int64_t utc,uint8_t *out,size_t *size) {
    if(n!=3 || memcmp(b,"IGS",3))return false;
    *size=fixture(out,utc);return true;
}
static void clean(void) { remove(slots[0]);remove(slots[1]);remove(igs_cache);remove(temporary); }
int main(void) {
    clean();uint8_t *b=NULL;size_t n=0;const char *source=NULL;
    assert(!chronvs_agnss_sources_cached(false,false,epoch,&b,&n,&source));
    assert(!chronvs_agnss_sources_cached(true,false,epoch,&b,&n,&source));
    assert(!*chronvs_agnss_sources_fetch(true,false,&b,&n,&source,memory_sample));
    assert(n==328 && prediction_requests==1 && igs_requests==0 && strstr(source,"salva"));free(b);
    assert(chronvs_agnss_sources_cached(true,false,epoch,&b,&n,&source) && n==328 && strstr(source,"Previsão"));free(b);
    assert(requests==1); /* Offline SD reuse makes no HTTPS request. */
    assert(!chronvs_agnss_sources_cached(true,false,epoch+10800,&b,&n,&source));
    prediction_bad=true;
    assert(!*chronvs_agnss_sources_fetch(true,false,&b,&n,&source,memory_sample));
    assert(igs_requests==1 && strstr(source,"IGS"));free(b);
    assert(chronvs_agnss_sources_cached(true,false,epoch,&b,&n,&source));free(b);
    remove(slots[0]);remove(slots[1]);
    assert(chronvs_agnss_sources_cached(true,false,epoch,&b,&n,&source) && strstr(source,"IGS"));free(b);
    igs_bad=true;
    assert(*chronvs_agnss_sources_fetch(false,false,&b,&n,&source,memory_sample));
    assert(igs_requests==3); /* Today and yesterday tried. */
    declared_large=true;
    assert(*chronvs_agnss_sources_fetch(false,false,&b,&n,&source,memory_sample));
    declared_large=false;chunked=true;oversized=true;
    assert(*chronvs_agnss_sources_fetch(false,false,&b,&n,&source,memory_sample));
    oversized=false;prediction_bad=false;
    assert(!*chronvs_agnss_sources_fetch(false,false,&b,&n,&source,memory_sample));free(b);
    assert(strstr(source,"sem SD") && samples==requests);
    clean();puts("Orbit sources: verified TLS, bounds, partial/chunked reads, prediction -> IGS fallback, SD persistence and offline reuse passed");
}
