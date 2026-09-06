/* Headless LVGL 8 render and interaction checks, using the real app sources. */
#define main service_test_main
#include "aion_service_test.c"
#undef main
#include "apps/aion_pages.c"
#include "apps/aion_app.c"
#include "ui/aion_alert.c"

static bool display_off, ringing;
static lv_indev_state_t contact;
static lv_point_t contact_point;
static void read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver; data->state=contact; data->point=contact_point;
}
static void touch(int x, int y, lv_indev_state_t state) {
    contact_point=(lv_point_t){x,y}; contact=state;
    lv_tick_inc(35); lv_timer_handler();
}
void chronvs_apps_add(const chronvs_app_t *app) { (void)app; }
bool chronvs_app_open(const char *id) { (void)id; return true; }
bool chronvs_system_ui_display_is_off(void) { return display_off; }
void chronvs_system_ui_notify_activity(void) { display_off = false; }
void chronvs_sound_set_ringing(bool value) { ringing = value; }

static unsigned char pixels[412 * 412 * 3];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            lv_color32_t c = {.full = lv_color_to32(*colors++)};
            unsigned char *p = &pixels[3 * ((411-y) * 412 + x)];
            if ((x-206)*(x-206) + (y-206)*(y-206) > 206*206) p[0]=p[1]=p[2]=0;
            else { p[0]=c.ch.blue; p[1]=c.ch.green; p[2]=c.ch.red; }
        }
    }
    lv_disp_flush_ready(driver);
}
static void frame(const char *name) {
    chronvs_aion_pages_refresh();
    lv_obj_update_layout(lv_scr_act());
    lv_tick_inc(300);
    lv_timer_handler();
    lv_refr_now(NULL);
    char path[160];
    snprintf(path, sizeof(path), ".pio/host-tests/%s.bmp", name);
    FILE *file = fopen(path, "wb"); assert(file);
    unsigned char header[54] = {'B','M'};
    uint32_t size = sizeof(pixels) + sizeof(header), offset=54, dib=40, dim=412;
    uint16_t planes=1, bits=24;
    memcpy(header+2,&size,4); memcpy(header+10,&offset,4); memcpy(header+14,&dib,4);
    memcpy(header+18,&dim,4); memcpy(header+22,&dim,4);
    memcpy(header+26,&planes,2); memcpy(header+28,&bits,2);
    fwrite(header,1,sizeof(header),file); fwrite(pixels,1,sizeof(pixels),file); fclose(file);
}
static lv_obj_t *button_with_text(lv_obj_t *parent, const char *text) {
    for (unsigned i=0; i<lv_obj_get_child_cnt(parent); ++i) {
        lv_obj_t *child = lv_obj_get_child(parent,i);
        if (lv_obj_check_type(child,&lv_btn_class)) {
            lv_obj_t *label=lv_obj_get_child(child,0);
            if (label && !strcmp(lv_label_get_text(label),text)) return child;
        }
        lv_obj_t *found=button_with_text(child,text);
        if(found) return found;
    }
    return NULL;
}
static void click(const char *text) {
    lv_obj_t *button=button_with_text(aion_root,text); assert(button);
    lv_event_send(button,LV_EVENT_CLICKED,NULL);
    chronvs_aion_pages_refresh();
}
int main(void) {
    lv_init(); chronvs_aion_init();
    static lv_color_t buffer[412*412/20];
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver); driver.hor_res=driver.ver_res=412;
    driver.draw_buf=&draw; driver.flush_cb=flush; lv_disp_drv_register(&driver);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input); input.type=LV_INDEV_TYPE_POINTER; input.read_cb=read_touch;
    lv_indev_drv_register(&input);
    create_aion(lv_scr_act()); show_aion(); frame("01-stopwatch");
    /* Dragging a stopwatch button navigates without starting the stopwatch. */
    touch(141,291,LV_INDEV_STATE_PR); touch(141,190,LV_INDEV_STATE_PR);
    touch(141,190,LV_INDEV_STATE_REL);
    assert(current_page==1 && !running);
    touch(206,150,LV_INDEV_STATE_PR); touch(206,250,LV_INDEV_STATE_PR);
    touch(206,250,LV_INDEV_STATE_REL); assert(current_page==0);
    /* Up advances twice; down from the middle of the empty list returns. */
    for (unsigned next=1; next<=2; ++next) {
        touch(206,280,LV_INDEV_STATE_PR); touch(206,170,LV_INDEV_STATE_PR);
        touch(206,170,LV_INDEV_STATE_REL); assert(current_page==next);
    }
    touch(206,150,LV_INDEV_STATE_PR); touch(206,250,LV_INDEV_STATE_PR);
    touch(206,250,LV_INDEV_STATE_REL); assert(current_page==1);
    select_page(1); frame("02-timer-presets");
    click("120"); assert(chronvs_timer_remaining()==7200);
    click("Cancelar");
    click("5"); assert(chronvs_timer_remaining()==300); frame("03-timer-running");
    display_off=true; advance(300); chronvs_aion_alert_poll();
    assert(!display_off && !ringing); frame("04-timer-alert");
    lv_tick_inc(1000); chronvs_aion_alert_poll(); assert(!ringing);
    lv_tick_inc(1000); chronvs_aion_alert_poll(); assert(ringing);
    lv_obj_t *extra=button_with_text(overlay,"+120"); assert(extra);
    lv_event_send(extra,LV_EVENT_CLICKED,NULL); chronvs_aion_alert_poll();
    assert(!ringing && chronvs_timer_remaining()==7200);
    select_page(2); frame("05-alarm-empty");
    click("Novo alarme"); assert(view==HOUR); frame("06-alarm-hour");
    lv_arc_set_value(arc,7); click("Proximo"); assert(view==MINUTE);
    lv_arc_set_value(arc,30); frame("07-alarm-minute");
    click("Proximo"); assert(view==DAYS);
    assert(lv_obj_has_state(create_button,LV_STATE_DISABLED));
    frame("08a-alarm-days-disabled");
    assert(lv_obj_get_width(create_button)==CHRONVS_UI_ACTION_WIDTH);
    assert(lv_obj_get_height(create_button)==CHRONVS_UI_ACTION_HEIGHT);
    click("seg"); click("qua"); click("sex");
    assert(days==((1<<1)|(1<<3)|(1<<5))); frame("08-alarm-days");
    click("Criar"); assert(view==LIST); frame("09-alarm-list");
    /* A downward swipe starting on an alarm row returns, without opening it. */
    touch(206,116,LV_INDEV_STATE_PR); touch(206,220,LV_INDEV_STATE_PR);
    touch(206,220,LV_INDEV_STATE_REL); assert(current_page==1);
    select_page(2); chronvs_aion_pages_refresh();
    click("07:30"); assert(view==DETAIL); frame("10-alarm-detail");
    click("Excluir"); assert(!chronvs_alarm_get(0));
    /* A scrolled list consumes the downward gesture to scroll, not navigate. */
    for (unsigned i=0; i<6; ++i) assert(chronvs_alarm_create(i,45,127));
    select_page(2); chronvs_aion_pages_refresh(); lv_obj_update_layout(surface);
    lv_obj_scroll_to_y(alarm_list,100,LV_ANIM_OFF);
    touch(206,140,LV_INDEV_STATE_PR); touch(206,250,LV_INDEV_STATE_PR);
    touch(206,250,LV_INDEV_STATE_REL); assert(current_page==2 && view==LIST);
    lv_obj_scroll_to_y(alarm_list,0,LV_ANIM_OFF);
    touch(206,140,LV_INDEV_STATE_PR); touch(206,250,LV_INDEV_STATE_PR);
    touch(206,250,LV_INDEV_STATE_REL); assert(current_page==1);
    clear_alarms();
    chronvs_timer_cancel(); set_time(26,9,7,7,29,59);
    assert(chronvs_alarm_create(7,30,127)); advance(1);
    chronvs_aion_alert_poll(); assert(!ringing); frame("11-alarm-alert");
    /* Dismissing during the silent interval must cancel the delayed sound. */
    lv_obj_t *stop=button_with_text(overlay,"Parar"); assert(stop);
    lv_event_send(stop,LV_EVENT_CLICKED,NULL); chronvs_aion_alert_poll();
    lv_tick_inc(2500); chronvs_aion_alert_poll(); assert(!ringing && !overlay);
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    printf("Aion UI passed. LVGL heap used: %u bytes. Snapshots in .pio/host-tests.\n",(unsigned)(memory.total_size-memory.free_size));
    return 0;
}
