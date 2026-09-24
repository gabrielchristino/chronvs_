#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice_test_sdk.h"
#include "services/voice_lab_service.h"

static void (*worker)(void *);
static bool task_failure, model_failure, cancel_loading, read_failure;
static unsigned reads, detections, command_frees, destroys, closes;
static model_iface_data_t model;
static srmodel_list_t models;
static chronvs_voice_result_t queued;
static bool has_result;
static void check_owned(void) {
    assert(chronvs_voice_lab_active());
    assert(!chronvs_voice_lab_start());
}
QueueHandle_t xQueueCreate(unsigned n, unsigned size) {
    assert(n == 8 && size == sizeof(queued)); return &queued;
}
int xQueueReceive(QueueHandle_t q, void *out, unsigned wait) {
    (void)q; (void)wait;
    if (!has_result) return 0;
    memcpy(out, &queued, sizeof(queued)); has_result = false; return pdTRUE;
}
int xQueueSend(QueueHandle_t q, const void *in, unsigned wait) {
    (void)q; (void)wait;
    memcpy(&queued, in, sizeof(queued)); has_result = true; return pdTRUE;
}
void xQueueReset(QueueHandle_t q) { (void)q; has_result = false; }
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, void *handle, int core) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
    if (task_failure) return 0;
    worker = fn; return pdPASS;
}
void vTaskDelete(void *task) { (void)task; assert(!chronvs_voice_lab_active()); }
int64_t esp_timer_get_time(void) { return 1000000; }
srmodel_list_t *esp_srmodel_init(const char *name) {
    (void)name; return &models;
}
char *esp_srmodel_filter(srmodel_list_t *list, const char *prefix, const char *lang) {
    (void)list; (void)prefix; (void)lang;
    return model_failure ? NULL : "model";
}
void esp_srmodel_deinit(srmodel_list_t *list) { (void)list; check_owned(); }
static model_iface_data_t *create(const char *name, int duration) {
    (void)name; (void)duration;
    if (cancel_loading) chronvs_voice_lab_stop();
    return &model;
}
static int chunk_size(model_iface_data_t *m) { (void)m; return 4; }
static esp_mn_state_t detect(model_iface_data_t *m, int16_t *samples) {
    (void)m;
    for (int i = 0; i < 4; ++i) assert(samples[i] == i + 1);
    ++detections;
    chronvs_voice_lab_stop();
    return ESP_MN_STATE_DETECTED;
}
static esp_mn_results_t *get_results(model_iface_data_t *m) {
    (void)m;
    static esp_mn_results_t result = {.num = 1, .command_id = {1}, .string = " hello "};
    return &result;
}
static void destroy(model_iface_data_t *m) { (void)m; check_owned(); ++destroys; }
const esp_mn_iface_t *esp_mn_handle_from_name(const char *name) {
    (void)name;
    static const esp_mn_iface_t iface = {create, chunk_size, detect, get_results, destroy};
    return &iface;
}
int esp_mn_commands_alloc(const esp_mn_iface_t *iface, model_iface_data_t *m) {
    (void)iface; (void)m; return ESP_OK;
}
int esp_mn_commands_add(int id, const char *text) { (void)id; (void)text; return ESP_OK; }
void *esp_mn_commands_update(void) { return NULL; }
int esp_mn_commands_free(void) { check_owned(); ++command_frees; return ESP_OK; }
int i2s_new_channel(const i2s_chan_config_t *cfg, void *tx, i2s_chan_handle_t *rx) {
    (void)cfg; (void)tx; *rx = &model; return ESP_OK;
}
int i2s_channel_init_std_mode(i2s_chan_handle_t ch, const i2s_std_config_t *cfg) {
    (void)ch; assert(cfg->slot_cfg.slot_mask == I2S_STD_SLOT_RIGHT); return ESP_OK;
}
int i2s_channel_enable(i2s_chan_handle_t ch) { (void)ch; return ESP_OK; }
int i2s_channel_disable(i2s_chan_handle_t ch) { (void)ch; check_owned(); return ESP_OK; }
int i2s_del_channel(i2s_chan_handle_t ch) { (void)ch; check_owned(); ++closes; return ESP_OK; }
int i2s_channel_read(i2s_chan_handle_t ch, void *out, size_t size, size_t *read, unsigned ms) {
    (void)ch;
    assert(ms == 100);
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_LISTENING);
    if (read_failure) { *read = 0; return ESP_FAIL; }
    /* Split a frame at a non-sample boundary, including data on timeout. */
    static const int32_t frame[] = {1 << 14, 2 << 14, 3 << 14, 4 << 14};
    const size_t offset = reads == 0 ? 0 : 3;
    assert(size == sizeof(frame) - offset);
    *read = reads++ == 0 ? 3 : size;
    memcpy(out, (const uint8_t *)frame + offset, *read);
    return ESP_ERR_TIMEOUT;
}
static void run(void) {
    assert(worker); worker(NULL); worker = NULL;
    assert(!chronvs_voice_lab_active());
}
int main(void) {
    task_failure = true;
    assert(!chronvs_voice_lab_start());
    assert(!chronvs_voice_lab_active());
    task_failure = false;
    model_failure = true;
    assert(chronvs_voice_lab_start()); run();
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_ERROR);
    chronvs_voice_lab_stop();
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_IDLE);
    model_failure = false;
    assert(chronvs_voice_lab_start());
    assert(!chronvs_voice_lab_start());
    chronvs_voice_lab_stop(); run();
    assert(destroys == 0 && closes == 0);
    cancel_loading = true;
    assert(chronvs_voice_lab_start()); run();
    assert(destroys == 1 && command_frees == 0 && closes == 0);
    cancel_loading = false;
    assert(chronvs_voice_lab_start()); run();
    assert(reads == 2 && detections == 1 && command_frees == 1 && closes == 1);
    chronvs_voice_result_t result;
    assert(chronvs_voice_lab_take_result(&result));
    assert(strcmp(result.text, "hello") == 0);
    read_failure = true;
    assert(chronvs_voice_lab_start()); run();
    assert(chronvs_voice_lab_state() == CHRONVS_VOICE_ERROR);
    assert(command_frees == 2 && closes == 2);
    /* Restart after error is safe once cleanup has fully finished. */
    assert(chronvs_voice_lab_start()); chronvs_voice_lab_stop(); run();
    puts("Vox: ownership, cancellation, cleanup, restart and partial I2S reads passed.");
    return 0;
}
