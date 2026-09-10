#include "services/sound_service.h"

#include <stdatomic.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_bool requested;
static atomic_bool preview_requested;
static atomic_uchar volume = 1;
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
    unsigned preview_frames = 0;
    for (;;) {
        const uint8_t level = atomic_load(&volume);
        const bool ringing = atomic_load(&requested);
        if (atomic_exchange(&preview_requested, false)) preview_frames = 16;
        /* Alerts take priority. Four silent frames drain the preview's DMA tail. */
        if (ringing || level == 0) preview_frames = 0;
        if ((!ringing && preview_frames == 0) || level == 0) {
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
        bool beep = ringing ? frame < 80 && frame % 20 < 12 : preview_frames > 4;
        /* Increasing PCM gain, with signed 32-bit intermediates and no clipping. */
        static const int32_t peak[] = {0, 3276, 6553, 11468, 19660, 32760};
        for (unsigned i = 0; i < 160; ++i)
            samples[2*i] = samples[2*i+1] = beep ?
                (int32_t)wave[i % 16] * peak[level] / 3276 : 0;
        size_t written;
        if (i2s_channel_write(channel, samples, sizeof(samples), &written, 100) != ESP_OK) {
            ESP_LOGW("sound", "I2S write failed");
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (ringing) frame = (frame + 1) % 160;
        else { frame = 0; --preview_frames; }
    }
}

static void initialize_sound(void) {
    ESP_LOGI("sound", "Initializing I2S for first sound");
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
    atomic_store(&requested, ringing);
    if (ringing && atomic_load(&volume) > 0 && !initialization_attempted) {
        initialization_attempted = true;
        initialize_sound();
    }
}

uint8_t chronvs_sound_volume(void) { return atomic_load(&volume); }

void chronvs_sound_preview(void) {
    if (atomic_load(&volume) == 0 || atomic_load(&requested)) return;
    atomic_store(&preview_requested, true);
    if (!initialization_attempted) {
        initialization_attempted = true;
        initialize_sound();
    }
}

void chronvs_sound_set_volume(uint8_t level) {
    if (level > CHRONVS_SOUND_MAX_VOLUME) return;
    atomic_store(&volume, level);
    if (level == 0) atomic_store(&preview_requested, false);
    /* Unmuting an already active alert starts its audio lazily. */
    if (level && atomic_load(&requested)) chronvs_sound_set_ringing(true);
}
