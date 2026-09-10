#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_HTTP_EAGAIN 0x7007
typedef void *esp_http_client_handle_t;
#define HTTP_EVENT_ON_CONNECTED 1
typedef struct { int event_id; } esp_http_client_event_t;
typedef struct {
    const char *url;
    int (*crt_bundle_attach)(void *);
    int (*event_handler)(esp_http_client_event_t *);
    int timeout_ms;
    bool disable_auto_redirect;
    int buffer_size, buffer_size_tx;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
int esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_set_timeout_ms(esp_http_client_handle_t, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);
