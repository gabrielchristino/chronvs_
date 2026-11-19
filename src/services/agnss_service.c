#include "services/agnss_service.h"
#include "services/agnss_data.h"
#include "services/agnss_sources.h"
#include "services/sd_service.h"
#include "services/time_sync_service.h"
#include "services/wifi_session_service.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

/* User-authorized coarse reference for tests around CEP 04686-001.
 * Not a measured location. Radius deliberately covers the postcode region. */
static const chronvs_agnss_reference_t reference={-23.668, -46.690, 5000, true};
const char *chronvs_agnss_reference_name(void) { return "CEP 04686-001 / raio 5 km"; }
static chronvs_agnss_snapshot_t snapshot={.message="Ainda não solicitada"};
static atomic_bool worker_active;
/* Worker publishes these before release-storing worker_active=false.
 * UI reads only after acquire-loading it; no shared LVGL/UART or pointer races. */
static uint8_t *downloaded;
static size_t downloaded_size;
static const char *worker_error;
static uint8_t *cache;
static size_t cache_size, cursor;
static bool transfer_started, awaiting_fix, query_sent;
static uint64_t requested_us, last_attempt_us, next_write_us, drain_until_us;
static bool attempted;
static uint32_t worker_internal, worker_dma;
static uint64_t transfer_us;
static bool sd_ready;
static const char *worker_source, *cache_source;
static bool igs_only, fallback_pending;
static bool waiting_ack;
static uint8_t ack_class, ack_id;
static uint64_t ack_deadline;
static size_t pending_offset, pending_size;
static unsigned attempt;
static bool retry_pending, finishing;
static const char *finish_error;
static uint32_t confirmed_mask;
static void wait_ack(uint8_t cls,uint8_t id) {
    waiting_ack=true; ack_class=cls; ack_id=id; ack_deadline=(uint64_t)esp_timer_get_time()+1500000;
}

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }
static void state(chronvs_agnss_state_t value, const char *message) {
    snapshot.state=value; snapshot.message=message;
#ifdef CHRONVS_GPS_ASSIST_DIAGNOSTIC
    ESP_LOGI("agnss","phase=%u %s",(unsigned)value,message);
#endif
}
static unsigned pending_satellite(void) {
    return ack_class==8 && ack_id==7 && pending_size==82 ? cache[pending_offset+6+68] : 0;
}
static void finish(const char *error) {
    waiting_ack=retry_pending=false; finishing=true; finish_error=error;
    snapshot.current_satellite=0;
    state(AGNSS_SENDING,"Consultando estado do GPS");
}
static void packet_done(bool accepted) {
    unsigned sv=pending_satellite();
    if (sv>=1 && sv<=32) {
        snapshot.gps[sv-1].state=accepted?AGNSS_SV_ACCEPTED:AGNSS_SV_REJECTED;
        if (accepted) ++snapshot.gps_accepted; else ++snapshot.gps_rejected;
        uint32_t bit=UINT32_C(1)<<(sv-1);
        if (accepted && !(confirmed_mask&bit)) { confirmed_mask|=bit; ++snapshot.gps_confirmed_total; }
    }
    cursor=pending_offset+pending_size;
    retry_pending=false; attempt=0;
}
static void memory(const char *phase) {
    uint32_t internal=(uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    uint32_t dma=(uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (!worker_internal || internal<worker_internal) worker_internal=internal;
    if (!worker_dma || dma<worker_dma) worker_dma=dma;
    ESP_LOGI("agnss", "%s internal=%u DMA-largest=%u", phase,
        (unsigned)internal,(unsigned)dma);
}
static void worker(void *unused) {
    (void)unused;
    worker_internal=worker_dma=0;
    worker_source=NULL; worker_error="";
    if (chronvs_agnss_sources_cached(sd_ready,igs_only,(int64_t)time(NULL),&downloaded,&downloaded_size,&worker_source)) goto done;
    memory("Before Wi-Fi");
    esp_err_t err=chronvs_wifi_session_acquire();
    worker_error=err==ESP_ERR_TIMEOUT ? "Tempo do Wi-Fi esgotado" : "Sem Wi-Fi";
    if (err==ESP_OK) {
        memory("Before HTTPS");
        worker_error=chronvs_agnss_sources_fetch(sd_ready,igs_only,&downloaded,&downloaded_size,&worker_source,memory);
        chronvs_wifi_session_release(); memory("Network released");
    }
done:
    atomic_store_explicit(&worker_active,false,memory_order_release);
    /* All resources except this task's stack have been released/published. */
    vTaskDelete(NULL);
}
bool chronvs_agnss_active(void) {
    return atomic_load_explicit(&worker_active,memory_order_acquire) || fallback_pending || snapshot.state==AGNSS_WAIT_TIME ||
        snapshot.state==AGNSS_DOWNLOAD || snapshot.state==AGNSS_READY || snapshot.state==AGNSS_SENDING;
}
bool chronvs_agnss_request(bool force) {
    if (chronvs_agnss_active()) return false;
    uint64_t now=now_us();
    if (!force && attempted && now-last_attempt_us<600000000) return false;
    attempted=true; last_attempt_us=requested_us=now;
    snapshot=(chronvs_agnss_snapshot_t){0}; transfer_started=query_sent=false;
    waiting_ack=false;
    retry_pending=finishing=false; finish_error=NULL; attempt=0;
    confirmed_mask=0;
    igs_only=fallback_pending=false;
    sd_ready=chronvs_sd_mount();
    if (!chronvs_wifi_session_configured() && !cache && !sd_ready) { state(AGNSS_ERROR,"Sem Wi-Fi nem cache no SD"); return false; }
    state(AGNSS_WAIT_TIME,"Aguardando hora por NTP");
    return true;
}
void chronvs_agnss_snapshot(chronvs_agnss_snapshot_t *out) { *out=snapshot; }
bool chronvs_agnss_fix_pending(void) { return awaiting_fix; }
void chronvs_agnss_confirm_fix(void) { awaiting_fix=false; }
void chronvs_agnss_receiver_stopped(void) {
    if (snapshot.state==AGNSS_SENDING) state(AGNSS_ERROR,"Envio interrompido");
}
void chronvs_agnss_receive(const uint8_t *frame, size_t size) {
    if (!transfer_started || !chronvs_casic_valid(frame,size)) return;
    const uint8_t *payload=frame+6;
    if (frame[4]==5 && (frame[5]==0 || frame[5]==1) && size==14 && waiting_ack && payload[0]==ack_class && payload[1]==ack_id) {
        waiting_ack=false;
        ESP_LOGI("agnss","ACK type=%u for %02x/%02x PRN=%u attempt=%u source=%s",frame[5],payload[0],payload[1],pending_satellite(),attempt,cache_source?cache_source:"?");
        if (frame[5]==1) {
            ++snapshot.acknowledgements;
            if (ack_class==8) packet_done(true);
            attempt=0;
        }
        if (frame[5]==0) {
            ++snapshot.rejections;
            if (ack_class==0x0B) finish("GPS rejeitou hora/posição");
            else if (attempt<3) { retry_pending=true; next_write_us=now_us()+200000; }
            else packet_done(false);
        }
    }
    if (frame[4]==1 && frame[5]==0 && size==90) {
        snapshot.receiver_ephemerides=0; snapshot.receiver_status=true;
        for (unsigned i=8;i<40;++i) if ((payload[i]&15)==3) ++snapshot.receiver_ephemerides;
    }
}
void chronvs_agnss_poll(const chronvs_gps_data_t *gps, bool serial_ready, bool workout,
                        chronvs_agnss_write_t write) {
    uint64_t now=now_us();
    if (snapshot.state==AGNSS_DOWNLOAD && !atomic_load_explicit(&worker_active,memory_order_acquire)) {
        snapshot.minimum_internal=worker_internal; snapshot.minimum_dma=worker_dma;
        if (*worker_error) state(AGNSS_ERROR,worker_error);
        else {
            free(cache); cache=downloaded; cache_size=downloaded_size; downloaded=NULL;
            cache_source=worker_source; snapshot.source=cache_source;
            state(AGNSS_READY,"Órbitas verificadas");
        }
    }
    if (workout) {
        if (fallback_pending) { fallback_pending=false; state(AGNSS_DEFERRED,"Adiada durante a corrida"); }
        if (snapshot.state==AGNSS_WAIT_TIME || snapshot.state==AGNSS_READY || snapshot.state==AGNSS_SENDING)
            state(AGNSS_DEFERRED,"Adiada durante a corrida");
        return;
    }
    if (fallback_pending) {
        fallback_pending=false; igs_only=true; transfer_started=query_sent=false;
        waiting_ack=false;
        retry_pending=finishing=false; finish_error=NULL; attempt=0;
        snapshot.sent=snapshot.total=0; snapshot.source=NULL; requested_us=now;
        state(AGNSS_WAIT_TIME,"Tentando órbitas IGS");
    }
    if (snapshot.state!=AGNSS_WAIT_TIME && snapshot.state!=AGNSS_READY && snapshot.state!=AGNSS_SENDING) return;
    if (gps->fix && gps->satellites_valid && gps->satellites>=4 && !awaiting_fix &&
        (snapshot.state==AGNSS_WAIT_TIME || snapshot.state==AGNSS_READY)) {
        state(AGNSS_FIX_READY,"GPS já tem posição"); return;
    }
    int64_t utc;
    bool clock=chronvs_time_sync_utc(&utc);
    if (snapshot.state==AGNSS_WAIT_TIME) {
        if (!clock) {
            if (now-requested_us>=90000000) state(AGNSS_ERROR,"Sem hora confiável por NTP");
            return;
        }
        chronvs_agnss_blob_t info;
        if (cache && (!igs_only || (cache_source && strstr(cache_source,"IGS"))) && chronvs_agnss_validate(cache,cache_size,utc,&info)) {
            snapshot.source=cache_source;
            state(AGNSS_READY,"Usando órbitas em cache");
        } else {
            downloaded=NULL; downloaded_size=0; worker_error="";
            state(AGNSS_DOWNLOAD,"Consultando órbitas SD/Wi-Fi");
            atomic_store_explicit(&worker_active,true,memory_order_release);
            if (xTaskCreate(worker,"agnss",8192,NULL,3,NULL)!=pdPASS) {
                atomic_store_explicit(&worker_active,false,memory_order_release);
                state(AGNSS_ERROR,"Memória para rede insuficiente");
            }
            return;
        }
    }
    if (snapshot.state!=AGNSS_READY && snapshot.state!=AGNSS_SENDING) return;
    if (snapshot.state==AGNSS_SENDING && !finishing && now-transfer_us>180000000) {
        unsigned sv=waiting_ack?pending_satellite():0;
        if (sv>=1 && sv<=32) { snapshot.gps[sv-1].state=AGNSS_SV_UNCONFIRMED; ++snapshot.gps_unconfirmed; }
        finish("Tempo do envio esgotado");
    }
    if (!clock) { state(AGNSS_ERROR,"Hora de assistência expirou"); return; }
    if (waiting_ack) {
        if (now>=ack_deadline) {
            unsigned sv=pending_satellite();
            if (sv>=1 && sv<=32) { snapshot.gps[sv-1].state=AGNSS_SV_UNCONFIRMED; ++snapshot.gps_unconfirmed; }
            ESP_LOGI("agnss","ACK timeout PRN=%u attempt=%u",sv,attempt);
            /* Class/ID ACKs carry no PRN: advancing after silence could attribute
             * a late reply to the next satellite. Stop and query instead. */
            finish("GPS não confirmou o pacote");
        }
        return;
    }
    if (!serial_ready) {
        if (now-requested_us>=120000000) state(AGNSS_ERROR,"GPS sem serial para assistência");
        return;
    }
    if (now<next_write_us) return;
    if (!transfer_started) {
        chronvs_agnss_blob_t info;
        if (!chronvs_agnss_validate(cache,cache_size,utc,&info)) { state(AGNSS_ERROR,"Órbitas expiraram"); return; }
        uint8_t aid[66]; size_t n=chronvs_agnss_aid(aid,utc,&reference);
        if (!n) { state(AGNSS_ERROR,"Tabela de tempo GPS vencida"); return; }
        int sent=write(aid,n);
        if (!sent) return;
        if (sent!=(int)n) { state(AGNSS_ERROR,"Falha ao enviar assistência"); return; }
        awaiting_fix=transfer_started=true; cursor=info.start; transfer_us=now;
        snapshot.gps_accepted=snapshot.gps_rejected=snapshot.gps_unconfirmed=snapshot.retries=0;
        snapshot.current_satellite=0; snapshot.current_attempt=attempt=1;
        memset(snapshot.gps,0,sizeof(snapshot.gps));
        snapshot.receiver_status=false; snapshot.receiver_ephemerides=0;
        pending_size=0; retry_pending=finishing=false; finish_error=NULL;
        wait_ack(0x0B,1);
        snapshot.sent=1; snapshot.total=1+info.satellites+(info.ionosphere?1:0);
        state(AGNSS_SENDING,"Enviando ao GPS"); next_write_us=now+20000;
        return;
    }
    while (!finishing && cursor<cache_size) {
        size_t n=10+cache[cursor+2]+((size_t)cache[cursor+3]<<8);
        if (!chronvs_agnss_frame_current(cache+cursor,utc)) { cursor+=n; retry_pending=false; attempt=0; continue; }
        int sent=write(cache+cursor,n);
        if (!sent) return;
        if (sent!=(int)n) { state(AGNSS_ERROR,"Falha ao enviar assistência"); return; }
        pending_offset=cursor; pending_size=n;
        if (retry_pending) { ++attempt; ++snapshot.retries; } else attempt=1;
        retry_pending=false;
        wait_ack(cache[cursor+4],cache[cursor+5]);
        unsigned sv=pending_satellite();
        snapshot.current_satellite=sv; snapshot.current_attempt=attempt;
        if (sv>=1 && sv<=32) { snapshot.gps[sv-1].attempts=attempt; snapshot.gps[sv-1].state=AGNSS_SV_WAITING; }
        ESP_LOGI("agnss","TX PRN=%u attempt=%u week=%u toe=%u source=%s",sv,attempt,
            sv ? cache[cursor+56]|(unsigned)cache[cursor+57]<<8 : 0,
            sv ? (cache[cursor+54]|(unsigned)cache[cursor+55]<<8)*16 : 0,cache_source?cache_source:"?");
        if (attempt==1) ++snapshot.sent;
        next_write_us=now+20000;
        return;
    }
    if (!finishing) finish(NULL);
    if (!query_sent) {
        uint8_t query[10]; chronvs_casic_frame(query,1,0,NULL,0);
        int sent=write(query,sizeof(query));
        if (!sent) return;
        if (sent!=(int)sizeof(query)) { state(AGNSS_ERROR,"Falha na consulta do GPS"); return; }
        query_sent=true; drain_until_us=now+3000000;
    }
    if (now>=drain_until_us) {
        ESP_LOGI("agnss","Batch source=%s accepted=%u rejected=%u unconfirmed=%u retries=%u status=%d eph=%u error=%s",cache_source?cache_source:"?",
            snapshot.gps_accepted,snapshot.gps_rejected,snapshot.gps_unconfirmed,snapshot.retries,
            snapshot.receiver_status,snapshot.receiver_ephemerides,finish_error?finish_error:"");
        if (finish_error) state(AGNSS_ERROR,finish_error);
        else {
            bool partial=snapshot.gps_rejected>0;
            state(snapshot.gps_accepted?AGNSS_SENT:AGNSS_ERROR,
                !snapshot.gps_accepted?(partial?"GPS rejeitou todas as órbitas":"Nenhuma órbita confirmada"):
                partial?"Envio parcial; aguardando GPS":"Enviada; aguardando satélites");
            if (partial && !igs_only && cache_source && strstr(cache_source,"Previsão")) fallback_pending=true;
        }
    }
}
