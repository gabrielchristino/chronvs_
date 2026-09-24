#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#define pdPASS 1
#define pdTRUE 1
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xQueueSend(QueueHandle_t, const void *, unsigned);
void xQueueReset(QueueHandle_t);
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned, void *, int);
void vTaskDelete(void *);
typedef void *i2s_chan_handle_t;
typedef struct { unsigned dma_desc_num, dma_frame_num; } i2s_chan_config_t;
typedef struct {
    int clk_cfg;
    struct { int slot_mask; } slot_cfg;
    struct { int mclk, bclk, ws, dout, din; } gpio_cfg;
} i2s_std_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(...) ((i2s_chan_config_t){0})
#define I2S_STD_CLK_DEFAULT_CONFIG(...) 0
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(...) {0}
#define I2S_GPIO_UNUSED -1
#define I2S_STD_SLOT_RIGHT 2
#define GPIO_NUM_15 15
#define GPIO_NUM_2 2
#define GPIO_NUM_39 39
int i2s_new_channel(const i2s_chan_config_t *, void *, i2s_chan_handle_t *);
int i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
int i2s_channel_enable(i2s_chan_handle_t);
int i2s_channel_disable(i2s_chan_handle_t);
int i2s_del_channel(i2s_chan_handle_t);
int i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, unsigned);
typedef struct { int unused; } model_iface_data_t;
typedef struct { int unused; } srmodel_list_t;
typedef int esp_mn_state_t;
#define ESP_MN_STATE_DETECTED 1
#define ESP_MN_PREFIX "mn"
#define ESP_MN_ENGLISH "en"
typedef struct { int num; int command_id[1]; char string[256]; } esp_mn_results_t;
typedef struct {
    model_iface_data_t *(*create)(const char *, int);
    int (*get_samp_chunksize)(model_iface_data_t *);
    esp_mn_state_t (*detect)(model_iface_data_t *, int16_t *);
    esp_mn_results_t *(*get_results)(model_iface_data_t *);
    void (*destroy)(model_iface_data_t *);
} esp_mn_iface_t;
srmodel_list_t *esp_srmodel_init(const char *);
char *esp_srmodel_filter(srmodel_list_t *, const char *, const char *);
void esp_srmodel_deinit(srmodel_list_t *);
const esp_mn_iface_t *esp_mn_handle_from_name(const char *);
int esp_mn_commands_alloc(const esp_mn_iface_t *, model_iface_data_t *);
int esp_mn_commands_add(int, const char *);
void *esp_mn_commands_update(void);
int esp_mn_commands_free(void);
