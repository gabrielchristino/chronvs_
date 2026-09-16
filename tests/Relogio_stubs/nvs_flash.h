#pragma once
#include "esp_err.h"
#define ESP_ERR_NVS_NO_FREE_PAGES 1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2
int nvs_flash_init(void);
int nvs_flash_erase(void);
