#ifdef CHRONVS_GPS_ASSIST_DIAGNOSTIC
#include "platform/gps_diagnostics.h"
#include "core/app_manager.h"
#include "services/gps_service.h"
#include "services/agnss_service.h"
#include "services/time_sync_service.h"
#include "esp_log.h"
#include "esp_timer.h"

#if !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) || defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#error GPS assistance diagnostics require a USB-only console, leaving GPIO43 free
#endif
static int64_t next_report;
void chronvs_gps_diagnostics_start(void) {
    ESP_LOGI("gps_probe","USB-only probe; no receiver reset/config save; opening GPS once");
    chronvs_app_open("gps");
}
void chronvs_gps_diagnostics_poll(bool display_off) {
    int64_t now=esp_timer_get_time();
    if(now<next_report)return;
    next_report=now+5000000;
    chronvs_gps_snapshot_t gps;chronvs_gps_snapshot(&gps);
    chronvs_agnss_snapshot_t aid;chronvs_agnss_snapshot(&aid);
    int64_t utc=0;bool trusted=chronvs_time_sync_utc(&utc);
    ESP_LOGI("gps_probe","off=%d running=%d baud=%u bytes=%lu NMEA=%lu errors=%lu fix=%d used=%d HDOP=%.1f UTC=%d clock=%d",
        display_off,gps.running,gps.baud,(unsigned long)gps.bytes,(unsigned long)gps.data.sentences,
        (unsigned long)gps.data.errors,gps.data.fix,gps.data.satellites_valid?(int)gps.data.satellites:-1,
        gps.data.hdop_valid?gps.data.hdop:-1.0,gps.data.utc_valid,trusted);
    ESP_LOGI("gps_probe","assist=%u sent=%u/%u ACK=%u NACK=%u status=%d eph=%u internal=%lu DMA=%lu phase=%s",
        (unsigned)aid.state,aid.sent,aid.total,aid.acknowledgements,aid.rejections,aid.receiver_status,
        aid.receiver_ephemerides,(unsigned long)aid.minimum_internal,(unsigned long)aid.minimum_dma,aid.message);
    if(aid.source)ESP_LOGI("gps_probe","source=%s",aid.source);
    ESP_LOGI("gps_probe","accepted=%u rejected=%u unconfirmed=%u retries=%u PRN=%u attempt=%u unique=%u",aid.gps_accepted,aid.gps_rejected,
        aid.gps_unconfirmed,aid.retries,aid.current_satellite,aid.current_attempt,aid.gps_confirmed_total);
    for(unsigned i=0;i<CHRONVS_GPS_GSV_SOURCES;++i) {
        const chronvs_gps_sky_t *sky=&gps.data.sky[i];
        if(sky->valid)ESP_LOGI("gps_probe","GSV %s visible=%u CNO=%d",sky->talker,sky->visible,sky->cno_valid?(int)sky->cno:-1);
    }
}
#endif
