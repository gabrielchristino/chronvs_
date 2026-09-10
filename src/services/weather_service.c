#include "services/weather_service.h"
#include "services/weather_data.h"
#include "services/wifi_session_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define REQUEST_TIMEOUT_US 15000000LL
static const char *TAG = "weather";
static const char *URL = "https://api.open-meteo.com/v1/forecast?latitude=-23.5505&longitude=-46.6333"
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
    "&daily=temperature_2m_max,temperature_2m_min&timezone=America%2FSao_Paulo&forecast_days=1";

/* Fixed-width record, without bool or implicit snapshot padding. */
typedef struct {
    int64_t updated_epoch;
    float temperature, apparent, minimum, maximum;
    int16_t code;
    uint8_t humidity, version;
    uint32_t reserved;
} weather_record_t;

static SemaphoreHandle_t lock;
static chronvs_weather_snapshot_t cached;
static chronvs_weather_state_t state;
static const char *last_error = "";
static bool initialized, pending;

static void log_memory(const char *phase) {
    ESP_LOGI(TAG, "%s: internal=%u, largest DMA=%u, PSRAM=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static esp_err_t http_event(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_CONNECTED) log_memory("TLS connected");
    return ESP_OK;
}

static void load_cache(void) {
    nvs_handle_t storage;
    if (nvs_open("weather", NVS_READONLY, &storage) != ESP_OK) return;
    weather_record_t record = {0};
    size_t size = sizeof(record);
    esp_err_t err = nvs_get_blob(storage, "snapshot_v1", &record, &size);
    nvs_close(storage);
    if (err != ESP_OK || size != sizeof(record) || record.version != 1 || record.reserved) return;
    chronvs_weather_snapshot_t candidate = {
        .updated_epoch = record.updated_epoch, .temperature_c = record.temperature,
        .apparent_temperature_c = record.apparent, .minimum_c = record.minimum,
        .maximum_c = record.maximum, .weather_code = record.code,
        .humidity_percent = record.humidity, .valid = true,
    };
    if (chronvs_weather_snapshot_valid(&candidate)) cached = candidate;
}

bool chronvs_weather_init(void) {
    if (initialized) return true;
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    load_cache();
    initialized = true;
    return true;
}

bool chronvs_weather_get_snapshot(chronvs_weather_snapshot_t *snapshot) {
    if (!snapshot) return false;
    *snapshot = (chronvs_weather_snapshot_t){0};
    if (!initialized) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    *snapshot = cached;
    xSemaphoreGive(lock);
    return snapshot->valid;
}

chronvs_weather_state_t chronvs_weather_state(void) {
    if (!initialized) return CHRONVS_WEATHER_IDLE;
    xSemaphoreTake(lock, portMAX_DELAY);
    chronvs_weather_state_t value = state;
    xSemaphoreGive(lock);
    return value;
}

const char *chronvs_weather_error(void) {
    if (!initialized) return "Memória insuficiente";
    xSemaphoreTake(lock, portMAX_DELAY);
    const char *value = last_error;
    xSemaphoreGive(lock);
    return value;
}

bool chronvs_weather_take_result(chronvs_weather_snapshot_t *snapshot, char *error, size_t size) {
    if (!initialized) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool available = pending;
    if (available) {
        if (snapshot) *snapshot = cached;
        if (error && size) snprintf(error, size, "%s", last_error);
        pending = false;
    }
    xSemaphoreGive(lock);
    return available;
}

static bool persist(const chronvs_weather_snapshot_t *snapshot) {
    weather_record_t record = {
        .updated_epoch = snapshot->updated_epoch, .temperature = snapshot->temperature_c,
        .apparent = snapshot->apparent_temperature_c, .minimum = snapshot->minimum_c,
        .maximum = snapshot->maximum_c, .code = snapshot->weather_code,
        .humidity = snapshot->humidity_percent, .version = 1,
    };
    nvs_handle_t storage;
    if (nvs_open("weather", NVS_READWRITE, &storage) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(storage, "snapshot_v1", &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(storage);
    nvs_close(storage);
    return err == ESP_OK;
}

static int remaining_ms(int64_t deadline) {
    int64_t remaining = deadline - esp_timer_get_time();
    return remaining > 0 ? (int)((remaining + 999) / 1000) : 0;
}

static const char *fetch(chronvs_weather_snapshot_t *snapshot) {
    char *body = heap_caps_malloc(CHRONVS_WEATHER_RESPONSE_LIMIT + 1,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) return "Memória insuficiente";
    const char *error = "Sem Wi-Fi";
    const int64_t deadline = esp_timer_get_time() + REQUEST_TIMEOUT_US;
    /* All HTTP attempts share one 15 s budget; no periodic or background retry. */
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        int timeout = remaining_ms(deadline);
        if (!timeout) { error = "Tempo esgotado"; break; }
        error = "Sem Wi-Fi";
        esp_http_client_config_t config = {
            .url = URL, .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = http_event,
            .timeout_ms = timeout, .disable_auto_redirect = true,
            .buffer_size = 1024, .buffer_size_tx = 1024,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) { error = "Memória insuficiente"; break; }
        esp_err_t err = esp_http_client_open(client, 0);
        size_t length = 0;
        bool retry = true;
        if (err == ESP_OK && (timeout = remaining_ms(deadline)) > 0) {
            esp_http_client_set_timeout_ms(client, timeout);
            int64_t declared = esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);
            if (declared >= 0 && status != 200) {
                error = "Resposta inválida";
                retry = status >= 500 && status <= 599;
            } else if (declared > CHRONVS_WEATHER_RESPONSE_LIMIT) {
                error = "Resposta inválida";
                retry = false;
            } else if (declared >= 0) {
                while ((timeout = remaining_ms(deadline)) > 0) {
                    esp_http_client_set_timeout_ms(client, timeout > 1000 ? 1000 : timeout);
                    /* One extra byte detects an oversized chunked body without
                     * allowing the server to grow our allocation. */
                    int room = CHRONVS_WEATHER_RESPONSE_LIMIT + 1 - length;
                    int count = esp_http_client_read(client, body + length, room > 1024 ? 1024 : room);
                    if (count == -ESP_ERR_HTTP_EAGAIN) continue;
                    if (count < 0) break;
                    length += count;
                    if (length > CHRONVS_WEATHER_RESPONSE_LIMIT) {
                        error = "Resposta inválida";
                        retry = false;
                        break;
                    }
                    if (esp_http_client_is_complete_data_received(client)) {
                        body[length] = 0;
                        error = chronvs_weather_parse(body, length, snapshot) ? "" : "Resposta inválida";
                        retry = false;
                        break;
                    }
                    if (!count) {
                        error = "Resposta inválida";
                        retry = false;
                        break;
                    }
                }
            }
        }
        if (retry && (!remaining_ms(deadline) || err == ESP_ERR_TIMEOUT)) error = "Tempo esgotado";
        if (!*error && !remaining_ms(deadline)) error = "Tempo esgotado";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (!retry) break;
    }
    free(body);
    return error;
}

static void finish(const chronvs_weather_snapshot_t *snapshot, const char *error) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!*error) cached = *snapshot;
    last_error = error;
    state = *error ? CHRONVS_WEATHER_ERROR : CHRONVS_WEATHER_SUCCESS;
    pending = true;
    xSemaphoreGive(lock);
}

static void weather_task(void *argument) {
    (void)argument;
    chronvs_weather_snapshot_t candidate = {0};
    log_memory("Waiting for Wi-Fi session");
    esp_err_t err = chronvs_wifi_session_acquire();
    const char *error = err == ESP_ERR_TIMEOUT ? "Tempo esgotado" : "Sem Wi-Fi";
    if (err == ESP_OK) {
        log_memory("Wi-Fi connected; starting HTTPS");
        error = fetch(&candidate);
        log_memory("HTTPS finished");
        chronvs_wifi_session_release();
        log_memory("Wi-Fi stopped");
        if (!*error && !persist(&candidate)) error = "Falha ao salvar";
    }
    if (*error) ESP_LOGW(TAG, "Update failed: %s", error);
    finish(&candidate, error);
    vTaskDelete(NULL);
}

bool chronvs_weather_request_update(void) {
    if (!initialized) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (state == CHRONVS_WEATHER_FETCHING) { xSemaphoreGive(lock); return false; }
    state = CHRONVS_WEATHER_FETCHING;
    pending = false;
    last_error = "";
    xSemaphoreGive(lock);
    if (xTaskCreate(weather_task, "weather", 8192, NULL, 3, NULL) == pdPASS) return true;
    finish(NULL, "Memória insuficiente");
    return false;
}
