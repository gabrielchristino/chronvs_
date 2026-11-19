#include "platform/display_bench.h"
#ifdef CHRONVS_DISPLAY_BENCH
#include "core/app_manager.h"
#include "esp_log.h"
#include "ui/app_input.h"
#include "ui/system_ui.h"
#include "lvgl.h"

/* Same full-screen dirty area in both scenes. All stripes read this one
 * position, changed only by the UI timer, never by drawing/ISR callbacks. */
static lv_obj_t *surface;
static lv_timer_t *timer;
static int x = 24;
static uint32_t scene_since;
#ifdef CHRONVS_DISPLAY_BENCH_AUTORUN
static uint32_t active_ms, previous_tick;
#endif
static bool watch_background;
static chronvs_ui_app_input_t input;
static void leave(void);

static void draw(lv_event_t *event) {
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0xF2B84B);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_opa = LV_OPA_TRANSP;
    for (int y = 100; y <= 300; y += 100) {
        const lv_area_t area = {x, y, x + 63, y + 11};
        lv_draw_rect(ctx, &dsc, &area);
    }
    lv_draw_label_dsc_t text;
    lv_draw_label_dsc_init(&text);
    text.color = lv_color_hex(0xF2F2E9);
    text.align = LV_TEXT_ALIGN_CENTER;
    const lv_area_t area = {56, 150, 355, 195};
    lv_draw_label(ctx, &text, &area, watch_background ? "FUNDO: MOSTRADOR" : "FUNDO: LISO", NULL);
}
static void switch_scene(void) {
    watch_background = !watch_background;
    scene_since = lv_tick_get();
    lv_obj_set_style_bg_opa(surface, watch_background ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_invalidate(lv_scr_act());
    ESP_LOGI("display_bench", "scene=%s; ignore the mixed log window after switching",
        watch_background ? "watch" : "flat");
}
static void animate(lv_timer_t *unused) {
    (void)unused;
    if (!surface) return;
#ifdef CHRONVS_DISPLAY_BENCH_AUTORUN
    const uint32_t now = lv_tick_get();
    const uint32_t elapsed = now - previous_tick;
    previous_tick = now;
    if (!chronvs_system_ui_display_is_off()) active_ms += elapsed;
    if (active_ms >= 40000) { leave(); return; }
#endif
    if (chronvs_system_ui_display_is_off()) { scene_since = lv_tick_get(); return; }
    /* Same wall-clock speed even if drawing or TE waiting slows the cadence. */
    const uint32_t phase = lv_tick_get() % 3000;
    x = 24 + (int)(phase <= 1500 ? phase : 3000 - phase) / 5;
    if (lv_tick_elaps(scene_since) >= 8000) switch_scene();
    lv_obj_invalidate(lv_scr_act());
}
static void leave(void) {
    lv_timer_del(timer); timer = NULL;
    lv_obj_del(surface); surface = NULL;
    chronvs_app_open("apps");
    /* Automatic completion can happen during a held contact. Consume it after
     * deleting the old target so release cannot open a launcher item. */
    for (lv_indev_t *device = lv_indev_get_next(NULL); device; device = lv_indev_get_next(device))
        if (lv_indev_get_type(device) == LV_INDEV_TYPE_POINTER &&
            device->proc.state == LV_INDEV_STATE_PR) lv_indev_wait_release(device);
    ESP_LOGI("display_bench", "Stopped; normal menus available with scan instrumentation");
}
void chronvs_display_bench_init(void) {
    if (surface) return;
    scene_since = lv_tick_get();
    surface = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, 412, 412);
    lv_obj_clear_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(surface, lv_color_hex(0x26302B), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(surface, draw, LV_EVENT_DRAW_MAIN, NULL);
#ifdef CHRONVS_DISPLAY_BENCH_AUTORUN
    active_ms = 0; previous_tick = lv_tick_get();
    /* A bounded automated measurement, not an app navigation screen. Ignore
     * touch navigation until it ends; the global power menu remains available. */
    input.back = NULL;
#else
    input.back = leave;
#endif
    chronvs_ui_app_input_bind(surface, &input);
    timer = lv_timer_create(animate, 20, NULL);
#ifdef CHRONVS_DISPLAY_BENCH_AUTORUN
    ESP_LOGI("display_bench", "scene=flat; automatic 8 s scenes; exits after 40 s of active screen; touch does not navigate");
#else
    ESP_LOGI("display_bench", "scene=flat; automatic 8 s scenes; right swipe exits; timers are not activity");
#endif
}
#endif
