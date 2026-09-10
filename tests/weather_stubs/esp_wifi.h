#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define WIFI_EVENT_STA_DISCONNECTED 1
#define WIFI_EVENT_STA_STOP 2
#define IP_EVENT_STA_GOT_IP 3
#define WIFI_STORAGE_RAM 0
#define WIFI_AUTH_OPEN 0
#define WIFI_MODE_STA 0
#define WIFI_IF_STA 0
typedef struct { int dummy; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {
    struct {
        uint8_t ssid[32],password[64];
        struct {int authmode;} threshold;
        struct {bool capable,required;} pmf_cfg;
    } sta;
} wifi_config_t;
esp_err_t esp_wifi_init(wifi_init_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
