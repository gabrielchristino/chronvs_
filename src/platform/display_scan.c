#include "platform/display_scan.h"
#ifdef CHRONVS_DISPLAY_SCAN
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

/* The vendor init already enables TE (user page 0, command 35h, value 00h).
 * Observe GPIO18 as an input; never drive it or change panel registers. */
#define TE_PIN GPIO_NUM_18
#define TE_TIMEOUT_MS 30
static SemaphoreHandle_t edge;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t edges, periods;
static uint64_t period_sum;
static int64_t last_edge;
static bool initialized, off, first_flush, sync_enabled;
static unsigned consecutive_timeouts;
static void (*previous_flush)(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);
static void (*previous_start)(lv_disp_drv_t *);
static void (*previous_monitor)(lv_disp_drv_t *, uint32_t, uint32_t);
static int64_t frame_start, window_start;
static uint64_t frame_flush, frame_wait, total_flush, total_wait, total_render, total_px;
static uint64_t total_present;
static int64_t first_write;
static uint32_t frames, slices, timeouts, max_frame, max_present;

static void te_edge(void *unused) {
    (void)unused;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&lock);
    ++edges;
    if (last_edge) { period_sum += now - last_edge; ++periods; }
    last_edge = now;
    portEXIT_CRITICAL_ISR(&lock);
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(edge, &wake);
    if (wake) portYIELD_FROM_ISR();
}

static void start(lv_disp_drv_t *drv) {
    frame_start = esp_timer_get_time();
    frame_flush = frame_wait = 0;
    first_write = 0;
    first_flush = true;
    if (previous_start) previous_start(drv);
}

static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
    if (first_flush) {
        first_flush = false;
        if (sync_enabled && !off) {
            /* Binary semaphore: discard a stale pulse, then wait for a fresh
             * edge only ONCE per refresh, never once per partial strip. */
            xSemaphoreTake(edge, 0);
            const int64_t began = esp_timer_get_time();
            TickType_t ticks = pdMS_TO_TICKS(TE_TIMEOUT_MS);
            if (!ticks) ticks = 1;
            if (xSemaphoreTake(edge, ticks) == pdTRUE) consecutive_timeouts = 0;
            else {
                ++timeouts;
                if (++consecutive_timeouts >= 3) sync_enabled = false;
            }
            frame_wait += esp_timer_get_time() - began;
        }
        first_write = esp_timer_get_time();
    }
    const int64_t began = esp_timer_get_time();
    previous_flush(drv, area, colors);
    frame_flush += esp_timer_get_time() - began;
    ++slices;
}

static void monitor(lv_disp_drv_t *drv, uint32_t elapsed, uint32_t px) {
    const int64_t now = esp_timer_get_time();
    const uint32_t duration = (uint32_t)(now - frame_start);
    const uint32_t present = first_write ? (uint32_t)(now - first_write) : 0;
    total_render += duration >= frame_flush + frame_wait ? duration - frame_flush - frame_wait : 0;
    total_flush += frame_flush;
    total_wait += frame_wait;
    total_present += present;
    total_px += px;
    if (duration > max_frame) max_frame = duration;
    if (present > max_present) max_present = present;
    ++frames;
    if (previous_monitor) previous_monitor(drv, elapsed, px);
}

void chronvs_display_scan_init(void) {
    lv_disp_t *display = lv_disp_get_default();
    if (initialized || !display || !display->driver->flush_cb) return;
    edge = xSemaphoreCreateBinary();
    if (!edge) { ESP_LOGE("display_scan", "No TE semaphore; keeping original driver"); return; }
    gpio_config_t config = {.pin_bit_mask = 1ULL << TE_PIN, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE};
    esp_err_t result = gpio_config(&config);
    if (result == ESP_OK) {
        result = gpio_install_isr_service(0);
        if (result == ESP_ERR_INVALID_STATE) result = ESP_OK; /* Shared service. */
        if (result == ESP_OK) result = gpio_isr_handler_add(TE_PIN, te_edge, NULL);
    }
    if (result != ESP_OK) {
        gpio_intr_disable(TE_PIN);
        vSemaphoreDelete(edge); edge = NULL;
        ESP_LOGE("display_scan", "TE setup failed; keeping original driver"); return;
    }
    previous_flush = display->driver->flush_cb;
    previous_start = display->driver->render_start_cb;
    previous_monitor = display->driver->monitor_cb;
    display->driver->flush_cb = flush;
    display->driver->render_start_cb = start;
    display->driver->monitor_cb = monitor;
#ifdef CHRONVS_DISPLAY_TE_SYNC
    sync_enabled = true;
#endif
    initialized = true;
    window_start = esp_timer_get_time();
    ESP_LOGI("display_scan", "GPIO18 input; TE sync=%u, timeout=%u ms; original synchronous QSPI retained",
        sync_enabled, TE_TIMEOUT_MS);
}

void chronvs_display_scan_poll(bool display_off) {
    if (!initialized) return;
    if (display_off != off) {
        off = display_off;
        if (off) gpio_intr_disable(TE_PIN);
        else gpio_intr_enable(TE_PIN);
        portENTER_CRITICAL(&lock);
        edges = periods = 0; period_sum = 0; last_edge = 0;
        portEXIT_CRITICAL(&lock);
        xSemaphoreTake(edge, 0);
        frames = slices = timeouts = max_frame = max_present = 0;
        total_flush = total_wait = total_render = total_px = total_present = 0;
        window_start = esp_timer_get_time();
    }
    const int64_t now = esp_timer_get_time();
    if (off || now - window_start < 2000000 || !frames) return;
    portENTER_CRITICAL(&lock);
    const uint32_t n = edges, count = periods;
    const uint64_t sum = period_sum;
    edges = periods = 0; period_sum = 0;
    portEXIT_CRITICAL(&lock);
    /* render excludes synchronous flush and TE wait, but includes preemption.
     * present spans first write through last write, including inter-strip draws. */
    ESP_LOGI("display_scan", "frames=%" PRIu32 " slices=%" PRIu32 " px_avg=%" PRIu64
        " render_avg_us=%" PRIu64 " flush_avg_us=%" PRIu64 " wait_avg_us=%" PRIu64
        " present_avg_us=%" PRIu64 " frame_max_us=%" PRIu32 " present_max_us=%" PRIu32
        " te_edges=%" PRIu32 " te_period_us=%" PRIu64 " timeouts=%" PRIu32 " sync_on=%u",
        frames, slices, total_px/frames, total_render/frames, total_flush/frames, total_wait/frames,
        total_present/frames, max_frame, max_present, n, count ? sum/count : 0, timeouts, sync_enabled);
    frames = slices = timeouts = max_frame = max_present = 0;
    total_flush = total_wait = total_render = total_px = total_present = 0;
    window_start = now;
}
#endif
