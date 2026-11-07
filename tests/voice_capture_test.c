#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "voice_test_sdk.h"
#include "esp_heap_caps.h"
#include "services/voice_lab_service.h"

static void (*worker)(void *);
static bool fail_memory, fail_read, cancel_read, queued;
static unsigned reads, closes, yields;
static chronvs_voice_result_t result;
static int channel;
int64_t esp_timer_get_time(void) { return 1000000; }
void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(size == 64000 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return fail_memory ? NULL : malloc(size);
}
QueueHandle_t xQueueCreate(unsigned n, unsigned size) {
    assert(n == 8 && size == sizeof(result)); return &result;
}
int xQueueReceive(QueueHandle_t q, void *out, unsigned wait) {
    (void)q; (void)wait;
    if (!queued) return 0;
    memcpy(out, &result, sizeof(result)); queued = false; return pdTRUE;
}
int xQueueSend(QueueHandle_t q, const void *in, unsigned wait) {
    (void)q; (void)wait;
    memcpy(&result, in, sizeof(result)); queued = true; return pdTRUE;
}
void xQueueReset(QueueHandle_t q) { (void)q; queued = false; }
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, void *handle, int core) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    worker = fn; return pdPASS;
}
void vTaskDelete(void *task) { (void)task; assert(!chronvs_voice_lab_active()); }
void vTaskDelay(unsigned ticks) {
    assert(ticks == 1);
    assert(chronvs_voice_lab_active());
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_STOPPING);
    ++yields;
}
int i2s_new_channel(const i2s_chan_config_t *cfg, void *tx, i2s_chan_handle_t *rx) {
    (void)cfg; (void)tx; *rx = &channel; return ESP_OK;
}
int i2s_channel_init_std_mode(i2s_chan_handle_t ch, const i2s_std_config_t *cfg) {
    (void)ch; assert(cfg->slot_cfg.slot_mask == I2S_STD_SLOT_RIGHT); return ESP_OK;
}
int i2s_channel_enable(i2s_chan_handle_t ch) { (void)ch; return ESP_OK; }
int i2s_channel_disable(i2s_chan_handle_t ch) {
    (void)ch; assert(chronvs_voice_lab_active()); return ESP_OK;
}
int i2s_del_channel(i2s_chan_handle_t ch) {
    (void)ch; ++closes; assert(!chronvs_voice_lab_start()); return ESP_OK;
}
int i2s_channel_read(i2s_chan_handle_t ch, void *out, size_t size, size_t *read, unsigned ms) {
    (void)ch; assert(ms == 100);
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_LISTENING);
    if (fail_read) { *read = 0; return ESP_FAIL; }
    if (cancel_read) chronvs_voice_lab_stop();
    int32_t frame[256];
    for (unsigned i = 0; i < 256; ++i)
        frame[i] = i == 0 ? INT32_MAX : i == 1 ? INT32_MIN : (int32_t)i << 14;
    size_t offset = reads % 2 == 0 ? 0 : 3;
    assert(size == sizeof(frame) - offset);
    *read = reads++ % 2 == 0 ? 3 : size;
    memcpy(out, (const unsigned char *)frame + offset, *read);
    return ESP_ERR_TIMEOUT;
}
static void run(void) {
    worker(NULL); assert(!chronvs_voice_lab_active());
}
int main(void) {
    fail_memory = true;
    assert(chronvs_voice_lab_start()); run();
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_ERROR && closes == 0);
    fail_memory = false;
    assert(chronvs_voice_lab_start()); chronvs_voice_lab_stop(); run();
    assert(closes == 0 && reads == 0);
    fail_read = true;
    assert(chronvs_voice_lab_start()); run();
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_ERROR && closes == 1);
    fail_read = false; cancel_read = true;
    assert(chronvs_voice_lab_start()); run();
    assert(!queued && closes == 2);
    cancel_read = false; reads = 0;
    assert(chronvs_voice_lab_start()); run();
    assert(reads == 250 && closes == 3 && queued);
    assert(yields == 250);
    assert(strcmp(result.text, "Amostra enviada ao PC.") == 0);
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_IDLE);
    return 0;
}
