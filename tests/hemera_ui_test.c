#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "apps/hemera_app.c"
#define main aion_service_tests
#include "aion_service_test.c"
#undef main

static bool display_off;
static unsigned reads, activity, opens, flushes;
static chronvs_time_t rtc = {.year=26,.month=9,.day=10,.valid=true};
static lv_point_t point;
static lv_indev_state_t contact;
static lv_obj_t *root;
static unsigned char pixels[412*412*3];

void chronvs_apps_add(const chronvs_app_t *app) { assert(!strcmp(app->id,"hemera")); }
bool chronvs_app_open(const char *id) { assert(!strcmp(id,"apps")); ++opens; hide(); return true; }
bool chronvs_system_ui_display_is_off(void) { return display_off; }
void chronvs_system_ui_notify_activity(void) { ++activity; }
chronvs_time_t chronvs_rtc_read(void) { assert(!display_off); ++reads; return rtc; }

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    ++flushes;
    for (int y=area->y1;y<=area->y2;++y) for (int x=area->x1;x<=area->x2;++x) {
        lv_color32_t c={.full=lv_color_to32(*colors++)};
        unsigned char *p=&pixels[3*((411-y)*412+x)];
        if ((x-206)*(x-206)+(y-206)*(y-206)>206*206) p[0]=p[1]=p[2]=0;
        else { p[0]=c.ch.blue; p[1]=c.ch.green; p[2]=c.ch.red; }
    }
    lv_disp_flush_ready(driver);
}

static void frame(const char *name) {
    lv_obj_update_layout(root); lv_refr_now(NULL);
    char path[160]; snprintf(path,sizeof(path),".pio/host-tests/hemera-%s.bmp",name);
    FILE *file=fopen(path,"wb"); assert(file);
    unsigned char header[54]={'B','M'};
    uint32_t size=sizeof(pixels)+54,offset=54,dib=40,dim=412;
    uint16_t planes=1,bits=24;
    memcpy(header+2,&size,4); memcpy(header+10,&offset,4); memcpy(header+14,&dib,4);
    memcpy(header+18,&dim,4); memcpy(header+22,&dim,4); memcpy(header+26,&planes,2); memcpy(header+28,&bits,2);
    fwrite(header,1,54,file); fwrite(pixels,1,sizeof(pixels),file); fclose(file);
}

static void elapse(unsigned ms) { lv_tick_inc(ms); lv_timer_handler(); }
static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver; data->state=contact; data->point=point;
}
static void touch(int x,int y,lv_indev_state_t state) {
    point=(lv_point_t){x,y}; contact=state; elapse(35);
}
static void tap(int x,int y) { touch(x,y,LV_INDEV_STATE_PR); touch(x,y,LV_INDEV_STATE_REL); }
static void swipe(int x,int y) {
    touch(x,y,LV_INDEV_STATE_PR); touch(x+45,y,LV_INDEV_STATE_PR);
    touch(x+95,y,LV_INDEV_STATE_PR); touch(x+95,y,LV_INDEV_STATE_REL);
}
static void vertical_swipe(int x,int y,int dy) {
    touch(x,y,LV_INDEV_STATE_PR); touch(x,y+dy/2,LV_INDEV_STATE_PR);
    touch(x,y+dy,LV_INDEV_STATE_PR); touch(x,y+dy,LV_INDEV_STATE_REL);
}
static void date_tap(int day) {
    int slot=chronvs_calendar_weekday(year,month,1)+day-1;
    tap(GRID_X+slot%7*CELL_W+CELL_W/2,GRID_Y+slot/7*CELL_H+CELL_H/2);
}

static lv_obj_t *find_caption(lv_obj_t *tree, const char *caption) {
    if (lv_obj_has_flag(tree, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(tree,&lv_label_class) && !strcmp(lv_label_get_text(tree),caption)) return tree;
    for (unsigned i=0;i<lv_obj_get_child_cnt(tree);++i) {
        lv_obj_t *found=find_caption(lv_obj_get_child(tree,i),caption); if (found) return found;
    }
    return NULL;
}
static lv_obj_t *find_class(lv_obj_t *tree, const lv_obj_class_t *class) {
    if (lv_obj_has_flag(tree, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(tree,class)) return tree;
    for (unsigned i=0;i<lv_obj_get_child_cnt(tree);++i) {
        lv_obj_t *found=find_class(lv_obj_get_child(tree,i),class); if (found) return found;
    }
    return NULL;
}
static void click_caption(const char *caption) {
    lv_obj_update_layout(root);
    lv_obj_t *obj=find_caption(root,caption); assert(obj);
    lv_area_t a; lv_obj_get_coords(obj,&a); tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);
}
static void reminder_ui_tests(void) {
    date_tap(11); click_caption("Lembretes"); frame("08-reminder-list");
    assert(chronvs_hemera_reminders_active()); click_caption("Criar");
    lv_obj_t *textarea=find_class(root,&lv_textarea_class); assert(textarea);
    tap(154,322); /* Shift. */
    tap(128,226); tap(76,226); tap(232,274); tap(76,226); /* Casa. */
    assert(!strcmp(lv_textarea_get_text(textarea),"Casa"));
    frame("09-reminder-title");
    swipe(100,140); assert(find_caption(root,"Descartar rascunho?"));
    swipe(100,150); textarea=find_class(root,&lv_textarea_class);
    assert(textarea && !strcmp(lv_textarea_get_text(textarea),"Casa"));
    /* Leaving the app and switching off preserve a draft without rendering. */
    hide(); show(); assert(find_class(root,&lv_textarea_class));
    elapse(1000); unsigned before_reads=reads, before_flushes=flushes;
    display_off=true; elapse(60000); assert(reads==before_reads && flushes==before_flushes);
    display_off=false; elapse(35);
    vertical_swipe(206,278,-100);
    lv_obj_t *picker=find_class(root,&lv_arc_class); assert(picker);
    lv_arc_set_value(picker,8); elapse(35); frame("10-reminder-hour");
    vertical_swipe(206,230,-100); picker=find_class(root,&lv_arc_class); assert(picker);
    assert(find_caption(root,"Escolha o minuto")); lv_arc_set_value(picker,0); elapse(35);
    frame("11-reminder-minute"); vertical_swipe(206,230,-100);
    assert(find_caption(root,"Salvar")); frame("12-reminder-review");
    fail_save=true; click_caption("Salvar"); assert(!chronvs_reminder_get(0));
    assert(find_caption(root,"Falha ao salvar"));
    fail_save=false; click_caption("Salvar");
    const chronvs_reminder_t *r=chronvs_reminder_get(0);
    assert(r && r->day==11 && r->hour==8 && !strcmp(r->title,"Casa"));
    frame("13-reminder-saved"); click_caption("08:00 Casa");
    assert(find_caption(root,"Agendado")); frame("14-reminder-detail");
    click_caption("Excluir"); frame("15-reminder-delete");
    swipe(100,150); assert(find_caption(root,"Agendado")); /* Back cancels deletion. */
    click_caption("Excluir"); fail_save=true; click_caption("Excluir");
    assert(chronvs_reminder_get(0) && find_caption(root,"Falha ao excluir"));
    fail_save=false; click_caption("Excluir"); assert(!chronvs_reminder_get(0));
    swipe(100,180); assert(!chronvs_hemera_reminders_active() && detail);
    back(); lv_refr_now(NULL); lv_mem_monitor_t a,b; lv_mem_monitor(&a);
    for (unsigned i=0;i<20;++i) {
        date_tap(11); click_caption("Lembretes"); click_caption("Criar");
        swipe(100,140); click_caption("Descartar"); swipe(100,180); back();
    }
    lv_refr_now(NULL); lv_mem_monitor(&b); assert(a.free_size==b.free_size);
    printf("Hemera reminder flow: touch, gestures, draft, persistence failures and stable memory (%u bytes free).\n",(unsigned)b.free_size);
}

static void calendar_tests(void) {
    assert(chronvs_calendar_days(2000,2)==29);
    assert(chronvs_calendar_days(2024,2)==29);
    assert(chronvs_calendar_days(2026,2)==28);
    assert(!chronvs_calendar_valid(2026,2,29));
    assert(!chronvs_calendar_valid(2026,4,31));
    assert(!chronvs_calendar_valid(2026,0,1));
    assert(!chronvs_calendar_valid(2100,1,1));
    assert(!chronvs_calendar_valid(2026,1,0));
    int ordinal=0;
    for (int y=2000;y<=2099;++y) for (int m=1;m<=12;++m)
        for (int d=1;d<=chronvs_calendar_days(y,m);++d) {
            struct tm t={.tm_year=y-1900,.tm_mon=m-1,.tm_mday=d,.tm_hour=12,.tm_isdst=-1};
            assert(mktime(&t)!=(time_t)-1);
            assert(chronvs_calendar_weekday(y,m,d)==t.tm_wday);
            assert(chronvs_calendar_ordinal(y,m,d)==ordinal++);
        }
    assert(ordinal==36525);
    int y=2000,m=1; assert(!chronvs_calendar_step(&y,&m,-1));
    y=2099;m=12; assert(!chronvs_calendar_step(&y,&m,1));
    y=2026;m=12; assert(chronvs_calendar_step(&y,&m,1) && y==2027 && m==1);
    assert(chronvs_calendar_step(&y,&m,-1) && y==2026 && m==12);
}

int main(void) {
    calendar_tests();
    chronvs_aion_init();
    lv_init();
    static lv_color_t buffer[412*412/20]; static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res=driver.ver_res=412; driver.draw_buf=&draw; driver.flush_cb=flush; lv_disp_drv_register(&driver);
    static lv_indev_drv_t indev; lv_indev_drv_init(&indev);
    indev.type=LV_INDEV_TYPE_POINTER; indev.read_cb=touch_read; lv_indev_drv_register(&indev);
    root=create(lv_scr_act());
    rtc.valid=false; show(); frame("01-no-rtc");
    assert(!year && lv_obj_has_state(today_button,LV_STATE_DISABLED));
    vertical_swipe(206,270,-100); assert(!year && !detail);
    rtc.valid=true; elapse(60000); frame("02-month");
    assert(year==2026 && month==9 && !detail);
    for (unsigned i=0;i<lv_obj_get_child_cnt(month_page);++i) {
        lv_obj_t *child=lv_obj_get_child(month_page,i);
        if (!lv_obj_check_type(child,&lv_label_class)) continue;
        lv_area_t a; lv_obj_get_coords(child,&a);
        for (int x=a.x1;x<=a.x2;x+=a.x2-a.x1)
            for (int y=a.y1;y<=a.y2;y+=a.y2-a.y1)
                assert((x-206)*(x-206)+(y-206)*(y-206)<=206*206);
        lv_point_t size;
        lv_txt_get_size(&size,lv_label_get_text(child),lv_obj_get_style_text_font(child,0),0,0,1000,LV_TEXT_FLAG_NONE);
        assert(size.x<=lv_obj_get_width(child));
    }
    /* Empty cells do nothing; actual touches use the drawn grid's geometry. */
    tap(80,168); assert(!detail);
    date_tap(10); frame("03-today"); assert(detail && !strcmp(lv_label_get_text(distance_label),"Hoje"));
    swipe(120,230); assert(!detail && !opens && input.consumed);
    date_tap(11); assert(!strcmp(lv_label_get_text(distance_label),"Amanhã")); back();
    date_tap(9); assert(!strcmp(lv_label_get_text(distance_label),"Ontem")); back();
    date_tap(1); assert(!strcmp(lv_label_get_text(distance_label),"Há 9 dias")); back();
    date_tap(30); assert(!strcmp(lv_label_get_text(distance_label),"Daqui a 20 dias")); frame("04-future"); back();
    vertical_swipe(206,270,-100); assert(month==10 && !detail && input.consumed);
    vertical_swipe(206,170,100); assert(month==9 && !detail);
    /* Header, background and Hoje all accept swipes without click-through. */
    vertical_swipe(206,80,100); assert(month==8);
    vertical_swipe(45,230,-100); assert(month==9);
    vertical_swipe(206,369,-100); assert(month==10);
    vertical_swipe(206,369,-100); assert(month==11); /* Must not return to Hoje. */
    vertical_swipe(206,369,-30); assert(month==11); /* Short drag cancels the action. */
    tap(206,369); assert(month==9);
    /* A long held swipe advances only once, including further motion. */
    touch(206,300,LV_INDEV_STATE_PR); touch(206,210,LV_INDEV_STATE_PR);
    for (int i=0;i<20;++i) touch(206,110,LV_INDEV_STATE_PR);
    touch(206,110,LV_INDEV_STATE_REL); assert(month==10 && !detail);
    vertical_swipe(206,170,80); assert(month==10); /* Strictly more than 80. */
    touch(150,280,LV_INDEV_STATE_PR); touch(220,190,LV_INDEV_STATE_PR);
    touch(220,190,LV_INDEV_STATE_REL); assert(month==10 && !detail && !opens);
    date_tap(10); vertical_swipe(206,260,-100);
    assert(detail && month==10 && selected_day==10); back();
    year=2026;month=12;render(); vertical_swipe(206,270,-100);
    assert(year==2027 && month==1);
    vertical_swipe(206,170,100); assert(year==2026 && month==12);
    year=2026;month=8;render(); frame("05-six-weeks");
    date_tap(31); assert(detail && selected_day==31); back();
    /* Leap day and correct weekday without trusting rtc.weekday. */
    year=2024;month=2;render(); date_tap(29);
    assert(!strcmp(lv_label_get_text(detail_weekday),"Quinta-feira")); back();
    tap(206,369); assert(year==2026 && month==9);
    year=2000;month=1;render();
    vertical_swipe(206,170,100); assert(year==2000 && month==1 && input.consumed && !detail);
    year=2099;month=12;render();
    vertical_swipe(206,270,-100); assert(year==2099 && month==12 && input.consumed && !detail);
    tap(206,369);
    unsigned before=reads; elapse(1000); assert(reads==before);
    frame("06-rest"); unsigned drawn=flushes;
    for (int i=0;i<10;++i) elapse(1000);
    assert(flushes==drawn);
    display_off=true; elapse(250); before=reads;
    rtc.day=11;
    for (int i=0;i<10;++i) elapse(60000);
    assert(reads==before && flushes==drawn);
    display_off=false; elapse(250); assert(reads==before+1 && today.day==11);
    date_tap(11); rtc.day=12; elapse(60000);
    assert(!strcmp(lv_label_get_text(distance_label),"Ontem"));
    rtc.month=2;rtc.day=31; elapse(60000);
    assert(!today.valid && !strcmp(lv_label_get_text(distance_label),"Data atual indisponível"));
    back(); frame("07-invalid-rtc");
    rtc.month=9;rtc.day=10;show();
    unsigned touches=activity; touch(80,168,LV_INDEV_STATE_PR);
    for (int i=0;i<40;++i) touch(80,168,LV_INDEV_STATE_PR);
    touch(80,168,LV_INDEV_STATE_REL); assert(activity>touches+30);
    swipe(86,88); assert(opens==1 && month==9 && refresh->paused);
    before=reads; elapse(60000); assert(reads==before);
    show(); swipe(120,200); assert(opens==2 && !detail);
    show(); swipe(180,369); assert(opens==3); /* Hoje also supports back. */
    /* Compare identical label contents and completed frames at both ends. */
    show(); date_tap(10); back(); lv_refr_now(NULL);
    lv_mem_monitor_t a,b; lv_mem_monitor(&a);
    for (int i=0;i<100;++i) { date_tap(10); back(); hide(); show(); }
    lv_refr_now(NULL); lv_mem_monitor(&b);
    printf("Hemera LVGL free before/after 100 visits: %u / %u bytes.\n",
        (unsigned)a.free_size, (unsigned)b.free_size);
    assert(a.free_size==b.free_size);
    reminder_ui_tests();
    puts("Hemera: all 36525 dates, month bounds, leap days, touch/back, RTC recovery, off/hidden and stable LVGL memory passed.");
    return 0;
}
