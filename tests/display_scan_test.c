#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform/display_scan.c"

static int64_t test_now = 100;
static bool pulse, pending, irq_enabled, fail_setup;
static unsigned waits, writes, starts, monitors, ready, logs;
static lv_disp_drv_t driver;
static lv_disp_t display;
int64_t esp_timer_get_time(void) { return test_now; }
lv_disp_t *lv_disp_get_default(void) { return &display; }
void scan_test_log(const char *format, ...) { (void)format; ++logs; }
int gpio_config(const gpio_config_t *config) {
    assert(config->pin_bit_mask == (1ULL << 18) && config->mode == GPIO_MODE_INPUT);
    return fail_setup ? 2 : ESP_OK;
}
int gpio_install_isr_service(int flags) { assert(!flags); return ESP_ERR_INVALID_STATE; }
int gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg) {
    assert(pin == 18 && handler == te_edge && !arg); irq_enabled = true; return ESP_OK;
}
int gpio_intr_disable(int pin) { assert(pin == 18); irq_enabled = false; return ESP_OK; }
int gpio_intr_enable(int pin) { assert(pin == 18); irq_enabled = true; return ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return &pending; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { assert(sem == &pending); }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *wake) {
    assert(sem == &pending); pending = true; *wake = pdFALSE; return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    assert(sem == &pending);
    if (pending) { pending = false; return pdTRUE; }
    if (!ticks) return pdFALSE;
    assert(ticks == 3); ++waits;
    test_now += pulse ? 5000 : 30000;
    if (pulse) { assert(irq_enabled); te_edge(NULL); pending = false; return pdTRUE; }
    return pdFALSE;
}
static void raw_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
    assert(drv == &driver && area && colors);
    ++writes; test_now += 100; ++ready;
}
static void raw_start(lv_disp_drv_t *drv) { assert(drv == &driver); ++starts; }
static void raw_monitor(lv_disp_drv_t *drv, uint32_t elapsed, uint32_t px) {
    assert(drv == &driver && elapsed && px == 412U*412U); ++monitors;
}
static void frame(void) {
    const lv_area_t area = {0, 0, 411, 19}; lv_color_t color = {0};
    driver.render_start_cb(&driver);
    /* 21 partial strips, with a simulated 200 us draw before every flush. */
    for (int i = 0; i < 21; ++i) {
        test_now += 200;
        driver.flush_cb(&driver, &area, &color);
    }
    driver.monitor_cb(&driver, 10, 412U*412U);
}
int main(void) {
    display.driver = &driver;
    driver.flush_cb = raw_flush; driver.render_start_cb = raw_start; driver.monitor_cb = raw_monitor;
    fail_setup = true;
    chronvs_display_scan_init();
    assert(!initialized && driver.flush_cb == raw_flush && !edge);
    fail_setup = false;
    chronvs_display_scan_init(); chronvs_display_scan_init();
    assert(initialized && irq_enabled);
    pulse = true;
    te_edge(NULL); /* A stale pulse must not satisfy the fresh-edge wait. */
    frame();
    assert(writes == 21 && ready == writes && starts == 1 && monitors == 1);
    assert(total_flush == 2100 && total_render == 4200 && total_present == 6100);
#ifdef CHRONVS_DISPLAY_TE_SYNC
    assert(waits == 1 && total_wait == 5000);
    pulse = false;
    frame(); frame(); frame();
    assert(timeouts == 3 && !sync_enabled && waits == 4);
    frame(); assert(waits == 4); /* Missing TE never traps the UI in repeated waits. */
#else
    assert(waits == 0 && total_wait == 0);
#endif
    test_now += 2000000;
    chronvs_display_scan_poll(false);
    assert(!frames && !slices && !total_flush && logs);
    chronvs_display_scan_poll(true); assert(!irq_enabled && off && !last_edge);
    chronvs_display_scan_poll(false); assert(irq_enabled && !off && !pending);
    puts("Display scan: preserved synchronous flush, one fresh-edge wait per frame, timeout fallback, accounting and sleep passed.");
}
