#include "services/sound_service.h"

#include <stdatomic.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_bool requested;
static i2s_chan_handle_t channel;
static bool initialization_attempted;

static void sound_task(void *arg) {
    (void)arg;
    /* 1 kHz sine, 16 kHz stereo, four 120 ms beeps every 1.6 seconds. */
    static const int16_t wave[16] = {0,1254,2317,3027,3276,3027,2317,1254,
                                    0,-1254,-2317,-3027,-3276,-3027,-2317,-1254};
    int16_t samples[320];
    bool enabled = false;
    unsigned frame = 0;
    for (;;) {
        if (!atomic_load(&requested)) {
            if (enabled) { i2s_channel_disable(channel); enabled = false; }
            frame = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!enabled) {
            if (i2s_channel_enable(channel) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            enabled = true;
        }
        bool beep = frame < 80 && frame % 20 < 12;
        for (unsigned i = 0; i < 160; ++i)
            samples[2*i] = samples[2*i+1] = beep ? wave[i % 16] : 0;
        size_t written;
        if (i2s_channel_write(channel, samples, sizeof(samples), &written, 100) != ESP_OK) {
            ESP_LOGW("sound", "I2S write failed");
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        frame = (frame + 1) % 160;
    }
}

static void initialize_sound(void) {
    ESP_LOGI("sound", "Initializing I2S for first audible alert");
    i2s_chan_config_t config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    config.dma_desc_num = 4;
    config.dma_frame_num = 160;
    config.auto_clear = true;
    esp_err_t err = i2s_new_channel(&config, &channel, NULL);
    if (err != ESP_OK) goto failed;
    i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_48, .ws = GPIO_NUM_38,
                     .dout = GPIO_NUM_47, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(channel, &standard);
    if (err != ESP_OK) goto failed;
    if (xTaskCreate(sound_task, "timer_beep", 3072, NULL, 2, NULL) == pdPASS) return;
    err = ESP_ERR_NO_MEM;
failed:
    ESP_LOGE("sound", "Speaker unavailable: %s", esp_err_to_name(err));
    if (channel) { i2s_del_channel(channel); channel = NULL; }
}
void chronvs_sound_set_ringing(bool ringing) {
    if (ringing && !initialization_attempted) {
        initialization_attempted = true;
        initialize_sound();
    }
    atomic_store(&requested, ringing && channel != NULL);
}
