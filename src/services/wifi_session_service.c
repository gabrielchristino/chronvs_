#include "services/wifi_session_service.h"

#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#if __has_include("chronvs_secrets.h")
#include "chronvs_secrets.h"
#endif
#ifndef CHRONVS_WIFI_SSID
#define CHRONVS_WIFI_SSID ""
#endif
#ifndef CHRONVS_WIFI_PASSWORD
#define CHRONVS_WIFI_PASSWORD ""
#endif

#define CONNECTED BIT0
#define DISCONNECTED BIT1
#define STOPPED BIT2
#define CONNECT_TIMEOUT_US 20000000LL

static SemaphoreHandle_t session;
static EventGroupHandle_t events;
static bool initialized, started, unavailable;

bool chronvs_wifi_session_configured(void) { return CHRONVS_WIFI_SSID[0] != '\0'; }

bool chronvs_wifi_session_init(void) {
    if (!session) session = xSemaphoreCreateMutex();
    return session != NULL;
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    /* Only the owning worker starts/connects/stops Wi-Fi. No reconnect can race
     * with release, and STA_STOP drains older events before another session. */
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(events, CONNECTED);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED);
        xEventGroupSetBits(events, DISCONNECTED);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP)
        xEventGroupSetBits(events, STOPPED);
}

static esp_err_t initialize_network(void) {
    if (initialized) return ESP_OK;
    if (unavailable) return ESP_ERR_INVALID_STATE;
    /* Partial initialization is not repeated, avoiding duplicate netifs/handlers. */
    unavailable = true;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    events = xEventGroupCreate();
    if (!events || !esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    if ((err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                   network_event, NULL, NULL)) != ESP_OK) return err;
    if ((err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                   network_event, NULL, NULL)) != ESP_OK) return err;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, CHRONVS_WIFI_SSID,
           sizeof(CHRONVS_WIFI_SSID) - 1 < sizeof(config.sta.ssid)
               ? sizeof(CHRONVS_WIFI_SSID) - 1 : sizeof(config.sta.ssid));
    memcpy(config.sta.password, CHRONVS_WIFI_PASSWORD,
           sizeof(CHRONVS_WIFI_PASSWORD) - 1 < sizeof(config.sta.password)
               ? sizeof(CHRONVS_WIFI_PASSWORD) - 1 : sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &config)) != ESP_OK) return err;
    initialized = true;
    unavailable = false;
    return ESP_OK;
}

void chronvs_wifi_session_release(void) {
    if (started) {
        xEventGroupClearBits(events, STOPPED);
        esp_wifi_disconnect();
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK || !(xEventGroupWaitBits(events, STOPPED, pdTRUE,
                                  pdFALSE, pdMS_TO_TICKS(1000)) & STOPPED))
            unavailable = true;
        started = false;
    }
    xSemaphoreGive(session);
}

esp_err_t chronvs_wifi_session_acquire(void) {
    if (!session || !chronvs_wifi_session_configured()) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(session, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    esp_err_t err = unavailable ? ESP_ERR_INVALID_STATE : initialize_network();
    if (err != ESP_OK) goto failed;
    xEventGroupClearBits(events, CONNECTED | DISCONNECTED | STOPPED);
    err = esp_wifi_start();
    if (err != ESP_OK) goto failed;
    started = true;
    const int64_t deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
    err = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) { err = ESP_ERR_TIMEOUT; break; }
        xEventGroupClearBits(events, CONNECTED | DISCONNECTED);
        err = esp_wifi_connect();
        if (err != ESP_OK) break;
        EventBits_t bits = xEventGroupWaitBits(events, CONNECTED | DISCONNECTED,
            pdTRUE, pdFALSE, pdMS_TO_TICKS((remaining + 999) / 1000));
        if ((bits & CONNECTED) && !(bits & DISCONNECTED)) return ESP_OK;
        err = bits & DISCONNECTED ? ESP_FAIL : ESP_ERR_TIMEOUT;
        if (err == ESP_ERR_TIMEOUT) break;
    }
failed:
    chronvs_wifi_session_release();
    return err;
}
