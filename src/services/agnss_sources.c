#include "services/agnss_sources.h"
#include "services/agnss_orbits.h"
#include "services/agnss_gzip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#ifndef CHRONVS_AGNSS_DIRECTORY
#define CHRONVS_AGNSS_DIRECTORY "/sdcard/chronvs"
#endif
static const char *prediction_url="https://raw.githubusercontent.com/badriram/open-extended-ephemeris/ephemeris-data/gps-7d-casic.bin";
static const char *slots[]={CHRONVS_AGNSS_DIRECTORY "/gps-pred-a.bin",CHRONVS_AGNSS_DIRECTORY "/gps-pred-b.bin"};
static const char *igs_cache=CHRONVS_AGNSS_DIRECTORY "/gps-igs.bin";
static const char *temporary=CHRONVS_AGNSS_DIRECTORY "/gps-orbits.part";
static int preserved_slot=-1;
static void *psram(size_t n) { return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
static uint8_t *read_file(const char *path, size_t limit, size_t *length) {
    FILE *f=fopen(path,"rb"); if (!f) return NULL;
    uint8_t *b=NULL;
    if (!fseek(f,0,SEEK_END)) {
        long n=ftell(f);
        if (n>0 && (size_t)n<=limit && !fseek(f,0,SEEK_SET)) {
            b=psram((size_t)n);
            if (b && fread(b,1,(size_t)n,f)==(size_t)n && !ferror(f)) *length=(size_t)n;
            else { free(b); b=NULL; }
        }
    }
    fclose(f); return b;
}
static bool save(const char *path, const uint8_t *bytes, size_t n) {
    FILE *f=fopen(temporary,"wb"); if (!f) return false;
    bool ok=fwrite(bytes,1,n,f)==n && fflush(f)==0 && fsync(fileno(f))==0;
    if (fclose(f)) ok=false;
    if (ok) { remove(path); ok=rename(temporary,path)==0; }
    if (!ok) remove(temporary);
    return ok;
}
bool chronvs_agnss_sources_cached(bool sd, bool igs_only, int64_t utc, uint8_t **out, size_t *length, const char **source) {
    if (!sd) return false;
    uint8_t *small=psram(CHRONVS_AGNSS_BINARY_LIMIT); if (!small) return false;
    for (int i=0;!igs_only && i<2;++i) {
        size_t n=0; uint8_t *b=read_file(slots[i],CHRONVS_AGNSS_PREDICTION_LIMIT,&n);
        bool ok=b && chronvs_agnss_prediction(b,n,utc,small,length);
        free(b);
        if (ok) { preserved_slot=i; *out=small; *source="Previsão do SD"; return true; }
    }
    size_t n=0; uint8_t *b=read_file(igs_cache,CHRONVS_AGNSS_BINARY_LIMIT,&n);
    chronvs_agnss_blob_t info;
    bool ok=b && chronvs_agnss_validate(b,n,utc,&info);
    free(small);
    if (ok) { *out=b; *length=n; *source="IGS do SD"; return true; }
    free(b); return false;
}
static const char *download(const char *url, size_t limit, uint8_t **out, size_t *length, void (*memory)(const char *)) {
    /* The full mixed-constellation IGS file is much larger than the forecast.
     * Physical USB measurements showed ~15 KiB/s on this link; 60 s cut it off
     * before completion. Keep a bounded three-minute budget only for IGS. */
    uint64_t deadline=(uint64_t)esp_timer_get_time()+(limit==CHRONVS_AGNSS_GZIP_LIMIT?180000000ULL:60000000ULL);
    esp_http_client_config_t config={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_redirect=true,.timeout_ms=15000,.buffer_size=1024,.buffer_size_tx=512};
    esp_http_client_handle_t c=esp_http_client_init(&config);
    if (!c) return "Memória para HTTPS insuficiente";
    uint8_t *b=NULL; const char *error="Falha no download das órbitas";
    if (esp_http_client_open(c,0)==ESP_OK) {
        memory("TLS connected");
        int64_t declared=esp_http_client_fetch_headers(c);
        ESP_LOGI("agnss","Source HTTP status=%d length=%lld url=%s",esp_http_client_get_status_code(c),(long long)declared,url);
        if (declared>=0 && (uint64_t)declared<=limit && esp_http_client_get_status_code(c)==200) {
            size_t capacity=declared>0 ? (size_t)declared : limit;
            b=psram(capacity+1);
            if (!b) error="PSRAM para órbitas insuficiente";
            else {
                size_t used=0;
                size_t reported=0;
                while ((uint64_t)esp_timer_get_time()<deadline && used<=capacity) {
                    esp_http_client_set_timeout_ms(c,1000);
                    size_t room=capacity+1-used; if (room>4096) room=4096;
                    int got=esp_http_client_read(c,(char *)b+used,(int)room);
                    if (got==-ESP_ERR_HTTP_EAGAIN) continue;
                    if (got<0) break;
                    used+=(size_t)got;
                    if (used-reported>=65536) { ESP_LOGI("agnss","HTTP received=%u/%u",(unsigned)used,(unsigned)capacity); reported=used; }
                    if (used>capacity) break;
                    if (esp_http_client_is_complete_data_received(c)) {
                        if (used && (!declared || used==(size_t)declared)) {
                            *out=b; *length=used; b=NULL; error="";
                        }
                        break;
                    }
                    if (!got) break;
                }
                if (*error && (uint64_t)esp_timer_get_time()>=deadline) error="Tempo de download esgotado";
                ESP_LOGI("agnss","HTTP finished received=%u declared=%lld error=%s",(unsigned)used,(long long)declared,error);
            }
        } else error="Resposta de órbitas inválida";
    }
    esp_http_client_close(c); esp_http_client_cleanup(c); free(b); return error;
}
const char *chronvs_agnss_sources_fetch(bool sd, bool igs_only, uint8_t **out, size_t *length, const char **source, void (*memory)(const char *)) {
    uint8_t *small=psram(CHRONVS_AGNSS_BINARY_LIMIT); if (!small) return "PSRAM para órbitas insuficiente";
    uint8_t *b=NULL; size_t n=0;
    const char *error=igs_only ? "Previsão não solicitada" : download(prediction_url,CHRONVS_AGNSS_PREDICTION_LIMIT,&b,&n,memory);
    if (!igs_only && !*error && chronvs_agnss_prediction(b,n,(int64_t)time(NULL),small,length)) {
        if (sd && save(slots[preserved_slot==0?1:0],b,n)) *source="Previsão salva no SD";
        else *source=sd ? "Previsão; falha ao salvar SD" : "Previsão sem SD";
        free(b); *out=small; return "";
    }
    free(b); b=NULL;
    /* BKG publishes the current day's merged broadcast navigation every 15 min.
     * Try yesterday too: today's file may not exist just after UTC midnight. */
    for (unsigned day=0;day<2;++day) {
        time_t epoch=time(NULL)-(time_t)day*86400; struct tm tm;
        if (!gmtime_r(&epoch,&tm)) break;
        char url[160]; snprintf(url,sizeof(url),"https://igs.bkg.bund.de/root_ftp/IGS/BRDC/%d/%03d/BRDC00WRD_S_%d%03d0000_01D_MN.rnx.gz",
            tm.tm_year+1900,tm.tm_yday+1,tm.tm_year+1900,tm.tm_yday+1);
        n=0; error=download(url,CHRONVS_AGNSS_GZIP_LIMIT,&b,&n,memory);
        if (!*error && chronvs_agnss_gzip_rinex(b,n,(int64_t)time(NULL),small,length)) {
            if (sd && save(igs_cache,small,*length)) *source="IGS salvo no SD";
            else *source=sd ? "IGS; falha ao salvar SD" : "IGS sem SD";
            free(b); *out=small; return "";
        }
        if (!*error) error="IGS sem órbitas atuais válidas";
        free(b); b=NULL;
    }
    free(small); return error;
}
