#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_NO_MEM 0x101
typedef void *i2s_chan_handle_t;
typedef struct { int dma_desc_num, dma_frame_num; bool auto_clear; } i2s_chan_config_t;
typedef struct {
    int clk_cfg, slot_cfg;
    struct { int mclk, bclk, ws, dout, din; } gpio_cfg;
} i2s_std_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(...) ((i2s_chan_config_t){0})
#define I2S_STD_CLK_DEFAULT_CONFIG(...) 0
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(...) 0
#define I2S_GPIO_UNUSED -1
#define GPIO_NUM_48 48
#define GPIO_NUM_38 38
#define GPIO_NUM_47 47
int i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, void *);
int i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
int i2s_channel_enable(i2s_chan_handle_t);
int i2s_channel_disable(i2s_chan_handle_t);
int i2s_del_channel(i2s_chan_handle_t);
int i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *, int);
