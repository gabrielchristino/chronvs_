#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apps/weather_app.c"

static bool display_off, mock_pending;
static chronvs_weather_state_t mock_state;
static chronvs_weather_snapshot_t mock_snapshot;
static const char *mock_error="";
static unsigned queries, rtc_reads, activity, opens, flushes;
static lv_indev_state_t contact;
static lv_point_t point;
static chronvs_time_t rtc={.year=26,.month=9,.day=10,.hour=15,.valid=true};
static lv_obj_t *root;

void chronvs_apps_add(const chronvs_app_t *app) { assert(!strcmp(app->id,"weather")); }
bool chronvs_app_open(const char *id) { assert(!strcmp(id,"apps")); ++opens; hide(); return true; }
bool chronvs_system_ui_display_is_off(void) { return display_off; }
void chronvs_system_ui_notify_activity(void) { ++activity; }
chronvs_time_t chronvs_rtc_read(void) { assert(!display_off); ++rtc_reads; return rtc; }
bool chronvs_weather_init(void) { return true; }
bool chronvs_weather_get_snapshot(chronvs_weather_snapshot_t *out) { *out=mock_snapshot; return out->valid; }
chronvs_weather_state_t chronvs_weather_state(void) { return mock_state; }
const char *chronvs_weather_error(void) { return mock_error; }
bool chronvs_weather_request_update(void) {
    if (mock_state==CHRONVS_WEATHER_FETCHING) return false;
    ++queries; mock_state=CHRONVS_WEATHER_FETCHING; mock_pending=false; mock_error=""; return true;
}
bool chronvs_weather_take_result(chronvs_weather_snapshot_t *out,char *error,size_t size) {
    if (!mock_pending) return false;
    *out=mock_snapshot; if (error && size) snprintf(error,size,"%s",mock_error);
    mock_pending=false; return true;
}
static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver; data->state=contact; data->point=point;
}
static void touch(int x,int y,lv_indev_state_t state) {
    point=(lv_point_t){x,y}; contact=state; lv_tick_inc(35); lv_timer_handler();
}
static void swipe(int x,int y) {
    touch(x,y,LV_INDEV_STATE_PR); touch(x+45,y,LV_INDEV_STATE_PR);
    touch(x+95,y,LV_INDEV_STATE_PR); touch(x+95,y,LV_INDEV_STATE_REL);
}
static unsigned char pixels[412*412*3];
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *colors) {
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
    char path[160]; snprintf(path,sizeof(path),".pio/host-tests/weather-%s.bmp",name);
    FILE *file=fopen(path,"wb"); assert(file);
    unsigned char header[54]={'B','M'};
    uint32_t size=sizeof(pixels)+54,offset=54,dib=40,dim=412;
    uint16_t planes=1,bits=24;
    memcpy(header+2,&size,4); memcpy(header+10,&offset,4); memcpy(header+14,&dib,4);
    memcpy(header+18,&dim,4); memcpy(header+22,&dim,4); memcpy(header+26,&planes,2); memcpy(header+28,&bits,2);
    fwrite(header,1,54,file); fwrite(pixels,1,sizeof(pixels),file); fclose(file);
}
static void complete(bool success) {
    mock_pending=true; mock_state=success?CHRONVS_WEATHER_SUCCESS:CHRONVS_WEATHER_ERROR;
    mock_error=success?"":"Sem Wi-Fi"; lv_tick_inc(250); lv_timer_handler();
}
int main(void) {
    lv_init();
    static lv_color_t buffer[412*412/20]; static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res=driver.ver_res=412; driver.draw_buf=&draw; driver.flush_cb=flush; lv_disp_drv_register(&driver);
    static lv_indev_drv_t indev; lv_indev_drv_init(&indev);
    indev.type=LV_INDEV_TYPE_POINTER; indev.read_cb=touch_read; lv_indev_drv_register(&indev);
    root=create(lv_scr_act()); show(); frame("01-loading");
    assert(queries==1 && !strcmp(lv_label_get_text(status),"Atualizando..."));
    complete(false); frame("02-no-data");
    assert(!strcmp(lv_label_get_text(condition),"Sem dados") && !strcmp(lv_label_get_text(status),"Sem Wi-Fi"));
    FILE *file=fopen("tests/fixtures/weather.json","rb"); assert(file);
    char json[4096]; size_t length=fread(json,1,sizeof(json),file); fclose(file);
    assert(chronvs_weather_parse(json,length,&mock_snapshot));
    show(); frame("03-cached-loading");
    assert(!strcmp(lv_label_get_text(age),"Atualizado há 3 h"));
    complete(true); frame("04-success");
    assert(!strcmp(lv_label_get_text(temperature),"23°C"));
    /* All label rectangles fit the round display and text stays within them. */
    for (unsigned i=0;i<lv_obj_get_child_cnt(root);++i) {
        lv_obj_t *child=lv_obj_get_child(root,i); lv_area_t a; lv_obj_get_coords(child,&a);
        for (int x=a.x1;x<=a.x2;x+=a.x2-a.x1)
            for (int y=a.y1;y<=a.y2;y+=a.y2-a.y1)
                assert((x-206)*(x-206)+(y-206)*(y-206)<=206*206);
        if (lv_obj_check_type(child,&lv_label_class)) {
            lv_point_t size;
            lv_txt_get_size(&size,lv_label_get_text(child),lv_obj_get_style_text_font(child,0),0,0,1000,LV_TEXT_FLAG_NONE);
            assert(size.x<=lv_obj_get_width(child));
        }
    }
    unsigned before=queries; lv_tick_inc(180000); lv_timer_handler(); assert(queries==before);
    show(); complete(false); frame("05-cached-error");
    assert(!strcmp(lv_label_get_text(status),"Falha ao atualizar") && !strcmp(lv_label_get_text(temperature),"23°C"));
    show(); unsigned reads=rtc_reads; display_off=true; lv_refr_now(NULL); unsigned drawn=flushes;
    mock_snapshot.temperature_c=30; complete(true);
    for (unsigned i=0;i<10;++i) { lv_tick_inc(10000); lv_timer_handler(); }
    assert(mock_pending && flushes==drawn && rtc_reads==reads);
    display_off=false; lv_tick_inc(250); lv_timer_handler();
    assert(!mock_pending && !strcmp(lv_label_get_text(temperature),"30°C"));
    show(); before=queries; hide(); assert(refresh->paused); show(); assert(queries==before);
    hide(); mock_snapshot.temperature_c=31; complete(true);
    assert(mock_pending && !strcmp(lv_label_get_text(temperature),"30°C"));
    show(); assert(!strcmp(lv_label_get_text(temperature),"31°C") && queries==before+1);
    unsigned touches=activity;
    touch(160,240,LV_INDEV_STATE_PR);
    for (unsigned i=0;i<40;++i) touch(160,240,LV_INDEV_STATE_PR);
    touch(160,240,LV_INDEV_STATE_REL); assert(activity>touches+30);
    swipe(120,180); assert(opens==1 && refresh->paused && input.consumed);
    show(); swipe(170,125); assert(opens==2); /* Icon is a separate touch target. */
    puts("Weather UI: circular layout, cache/age, no refresh while hidden/off, touch and back passed");
    return 0;
}
