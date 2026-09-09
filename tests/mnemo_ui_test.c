#define main model_test_main
#include "mnemo_test.c"
#undef main
#include "apps/mnemo_app.c"

static bool display_off;
static lv_indev_state_t contact;
static lv_point_t contact_point;
static unsigned flushes;
static unsigned activity_count, launcher_opens;
void chronvs_apps_add(const chronvs_app_t *app) { assert(!strcmp(app->id, "mnemo")); }
bool chronvs_app_open(const char *id) { assert(!strcmp(id, "apps")); ++launcher_opens; return true; }
bool chronvs_system_ui_display_is_off(void) { return display_off; }
void chronvs_system_ui_notify_activity(void) { ++activity_count; display_off = false; }
static void read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver; data->state=contact; data->point=contact_point;
}
static void touch(int x, int y, lv_indev_state_t state) {
    contact_point=(lv_point_t){x,y}; contact=state;
    lv_tick_inc(35); lv_timer_handler();
}
static unsigned char pixels[412*412*3];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    ++flushes;
    for (int y=area->y1; y<=area->y2; ++y) for (int x=area->x1; x<=area->x2; ++x) {
        lv_color32_t c = {.full=lv_color_to32(*colors++)};
        unsigned char *p=&pixels[3*((411-y)*412+x)];
        if ((x-206)*(x-206)+(y-206)*(y-206)>206*206) p[0]=p[1]=p[2]=0;
        else { p[0]=c.ch.blue; p[1]=c.ch.green; p[2]=c.ch.red; }
    }
    lv_disp_flush_ready(driver);
}
static void frame(const char *name) {
    lv_obj_update_layout(lv_scr_act()); lv_refr_now(NULL);
    char path[160]; snprintf(path,sizeof(path),".pio/host-tests/%s.bmp",name);
    FILE *file=fopen(path,"wb"); assert(file);
    unsigned char header[54]={'B','M'};
    uint32_t size=sizeof(pixels)+54, offset=54, dib=40, dim=412;
    uint16_t planes=1, bits=24;
    memcpy(header+2,&size,4); memcpy(header+10,&offset,4); memcpy(header+14,&dib,4);
    memcpy(header+18,&dim,4); memcpy(header+22,&dim,4); memcpy(header+26,&planes,2); memcpy(header+28,&bits,2);
    fwrite(header,1,54,file); fwrite(pixels,1,sizeof(pixels),file); fclose(file);
}
static void key(unsigned index) {
    lv_event_send(lv_obj_get_child(keyboard,index),LV_EVENT_SHORT_CLICKED,NULL);
}
static void swipe(int x, int y) {
    touch(x,y,LV_INDEV_STATE_PR);
    touch(x+45,y+2,LV_INDEV_STATE_PR);
    touch(x+95,y+4,LV_INDEV_STATE_PR);
    touch(x+95,y+4,LV_INDEV_STATE_REL);
}
int main(void) {
    lv_init();
    static lv_color_t buffer[412*412/20]; static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res=driver.ver_res=412; driver.draw_buf=&draw; driver.flush_cb=flush; lv_disp_drv_register(&driver);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER; input.read_cb=read_touch; lv_indev_drv_register(&input);
    create(lv_scr_act()); show(); frame("mnemo-01-empty");
    open_editor(0); frame("mnemo-02-editor");
    assert(lv_obj_get_child_cnt(keyboard)==20);
    for (unsigned i=0; i<20; ++i) {
        lv_area_t a; lv_obj_get_coords(lv_obj_get_child(keyboard,i),&a);
        assert(a.y1>=204 && lv_area_get_width(&a)==44 && lv_area_get_height(&a)==44);
        /* Even the rectangular bounds fit inside the physical circle. */
        for (int x=a.x1; x<=a.x2; x+=a.x2-a.x1)
            for (int y=a.y1; y<=a.y2; y+=a.y2-a.y1)
                assert((x-206)*(x-206)+(y-206)*(y-206)<206*206);
    }
    unsigned activity_before=activity_count;
    touch(76,226,LV_INDEV_STATE_PR); touch(76,226,LV_INDEV_STATE_REL);
    assert(activity_count>activity_before);
    assert(!strcmp(editor.text,"a"));
    for (unsigned i=0; i<2; ++i) key(0);
    assert(!strcmp(editor.text,"á")); frame("mnemo-03-accent-pending");
    lv_event_send(shift_key,LV_EVENT_SHORT_CLICKED,NULL);
    assert(editor.shift && lv_obj_has_state(shift_key,LV_STATE_CHECKED));
    key(1); key(1);
    assert(!strcmp(editor.text,"áD"));
    assert(lv_obj_has_state(shift_key,LV_STATE_CHECKED));
    lv_tick_inc(900); lv_timer_handler(); assert(!lv_obj_has_state(shift_key,LV_STATE_CHECKED));
    key(19); assert(symbols); frame("mnemo-10-symbols");
    key(4); key(4); assert(editor.text[strlen(editor.text)-1]==')');
    key(19); assert(!symbols);
    mnemo_editor_load(&editor,"Ação para amanhã:\ncomprar café, pão\ne maçãs.\nÁ À Ã Â Ç É Ê Í\nÓ Ô Õ Ú\nÚltima linha");
    changed(); frame("mnemo-04-multiline");
    mnemo_editor_move(&editor,0); update_editor(false);
    lv_obj_update_layout(textarea);
    lv_area_t text_coords; lv_obj_get_coords(lv_textarea_get_label(textarea),&text_coords);
    /* A click at the start of the first line resolves to character zero. */
    touch(text_coords.x1+1,text_coords.y1+4,LV_INDEV_STATE_PR);
    touch(text_coords.x1+1,text_coords.y1+4,LV_INDEV_STATE_REL); assert(editor.cursor==0);
    unsigned cursor=editor.cursor;
    touch(170,135,LV_INDEV_STATE_PR); touch(170,90,LV_INDEV_STATE_PR); touch(170,90,LV_INDEV_STATE_REL);
    assert(editor.cursor==cursor);
    touch(100,110,LV_INDEV_STATE_PR); touch(148,110,LV_INDEV_STATE_PR); touch(148,110,LV_INDEV_STATE_REL);
    assert(editor.cursor==cursor);
    touch(88,97,LV_INDEV_STATE_PR); touch(88,97,LV_INDEV_STATE_REL);
    assert(editor.cursor<=mnemo_text_length(editor.text));
    lv_obj_t *toggle=NULL;
    for (unsigned i=0;i<lv_obj_get_child_cnt(page);++i) {
        lv_obj_t *candidate=lv_obj_get_child(page,i);
        if (lv_obj_check_type(candidate,&lv_btn_class) && lv_obj_get_x(candidate)==90) toggle=candidate;
    }
    assert(toggle);
    mnemo_editor_move(&editor,1); update_editor(false);
    key(18);
    assert(editor.text[1]=='\n' && editor.cursor==2);
    key(16);
    assert(editor.text[1]!='\n' && editor.cursor==1);
    key(17); key(17); assert(editor.text[1]==' ' && editor.text[2]==' ');
    /* Holding delete keeps reporting activity, while repeating deletion. */
    unsigned length=mnemo_text_length(editor.text);
    activity_before=activity_count;
    touch(290,322,LV_INDEV_STATE_PR);
    for (unsigned i=0;i<40;++i) touch(290,322,LV_INDEV_STATE_PR);
    touch(290,322,LV_INDEV_STATE_REL);
    assert(activity_count>activity_before+30 && mnemo_text_length(editor.text)<length);
    lv_event_send(toggle,LV_EVENT_SHORT_CLICKED,NULL);
    assert(reading && lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN)); frame("mnemo-05-reading");
    lv_event_send(toggle,LV_EVENT_SHORT_CLICKED,NULL);
    fail_write=true; assert(!save_note()); swipe(100,110); assert(slot==0 && dirty);
    frame("mnemo-06-save-failure"); fail_write=false;
    hide(); assert(!dirty && refresh->paused); show();
    frame("mnemo-07-resumed");
    display_off=true; lv_tick_inc(2000); lv_timer_handler(); lv_refr_now(NULL);
    unsigned before=flushes;
    for (unsigned i=0; i<20; ++i) { lv_tick_inc(100); lv_timer_handler(); }
    assert(flushes==before); display_off=false; lv_tick_inc(100); lv_timer_handler();
    swipe(100,110); assert(slot==-1); frame("mnemo-08-list");
    /* Starting on a note row returns without opening it on release. */
    unsigned opens=launcher_opens; swipe(90,114);
    assert(slot==-1 && launcher_opens==opens+1);
    open_editor(0);
    lv_obj_t *trash=NULL;
    for (unsigned i=0;i<lv_obj_get_child_cnt(page);++i) {
        lv_obj_t *obj=lv_obj_get_child(page,i);
        if (lv_obj_check_type(obj,&lv_btn_class) && lv_obj_get_x(obj)==286) trash=obj;
    }
    assert(trash); lv_event_send(trash,LV_EVENT_SHORT_CLICKED,NULL);
    assert(dialog); frame("mnemo-09-delete");
    fail_write=true;
    lv_event_send(lv_obj_get_child(dialog,2),LV_EVENT_SHORT_CLICKED,NULL);
    assert(dialog && slot==0 && chronvs_mnemo_get(0)); fail_write=false;
    swipe(100,248);
    assert(!dialog && slot==0 && chronvs_mnemo_get(0));
    /* A keyboard swipe saves and returns without inserting its starting key. */
    length=mnemo_text_length(editor.text); swipe(94,226);
    assert(slot==-1 && mnemo_text_length(chronvs_mnemo_get(0)->text)==length);
    lv_mem_monitor_t initial, final; lv_mem_monitor(&initial);
    for(unsigned i=0;i<40;++i) { open_editor(0); back(); }
    lv_mem_monitor(&final); assert(final.free_size>=initial.free_size);
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&chronvs_mnemo_font,&glyph,0xE3,0) && !glyph.is_placeholder);
    printf("Mnemo UI passed. Heap used: %u bytes.\n",(unsigned)(final.total_size-final.free_size));
    return 0;
}
