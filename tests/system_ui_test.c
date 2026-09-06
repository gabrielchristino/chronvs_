/* Render the real watch, launcher and global controls with hardware stubs. */
#define main service_test_main
#include "aion_service_test.c"
#undef main
#include "lvgl.h"
#include "apps/app_catalog.h"
#include "ui/system_ui.h"
#include "ui/control_style.h"

uint8_t LCD_Backlight;
void Set_Backlight(uint8_t brightness) { LCD_Backlight=brightness; }
int nvs_flash_init(void) { return 0; }
int nvs_flash_erase(void) { return 0; }
int nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h;(void)k;(void)v;return -1; }
int nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h;(void)k;(void)v;return 0; }

static unsigned char pixels[412*412*3];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
    for(int y=area->y1;y<=area->y2;++y) for(int x=area->x1;x<=area->x2;++x) {
        lv_color32_t c={.full=lv_color_to32(*colors++)};
        unsigned char *p=&pixels[3*((411-y)*412+x)];
        if((x-206)*(x-206)+(y-206)*(y-206)>206*206) p[0]=p[1]=p[2]=0;
        else {p[0]=c.ch.blue;p[1]=c.ch.green;p[2]=c.ch.red;}
    }
    lv_disp_flush_ready(driver);
}
static void capture(const char *name) {
    lv_obj_update_layout(lv_scr_act()); lv_tick_inc(100);lv_timer_handler();lv_refr_now(NULL);
    char path[160];snprintf(path,sizeof(path),".pio/host-tests/%s.bmp",name);
    FILE *f=fopen(path,"wb");assert(f);
    unsigned char h[54]={'B','M'};
    uint32_t size=sizeof(pixels)+54,offset=54,dib=40,dim=412;uint16_t planes=1,bits=24;
    memcpy(h+2,&size,4);memcpy(h+10,&offset,4);memcpy(h+14,&dib,4);
    memcpy(h+18,&dim,4);memcpy(h+22,&dim,4);memcpy(h+26,&planes,2);memcpy(h+28,&bits,2);
    fwrite(h,1,54,f);fwrite(pixels,1,sizeof(pixels),f);fclose(f);
}
int main(void) {
    lv_init();
    static lv_color_t buffer[412*412/20];static lv_disp_draw_buf_t draw;static lv_disp_drv_t driver;
    lv_disp_draw_buf_init(&draw,buffer,NULL,412*412/20);lv_disp_drv_init(&driver);
    driver.hor_res=driver.ver_res=412;driver.draw_buf=&draw;driver.flush_cb=flush;
    lv_disp_drv_register(&driver);
    chronvs_app_manager_init(lv_scr_act());assert(chronvs_apps_register_all());
    assert(chronvs_app_open("watch"));capture("12-watch");
    lv_obj_t *panel=lv_obj_get_child(lv_scr_act(),1);assert(panel);
    lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(panel,0);
    chronvs_system_ui_set_battery(72,3.9f);capture("13-quick-settings");
    unsigned circles=0;
    for(unsigned i=0;i<lv_obj_get_child_cnt(panel);++i) {
        lv_obj_t *child=lv_obj_get_child(panel,i);
        if(lv_obj_check_type(child,&lv_btn_class)) {
            assert(lv_obj_get_width(child)==CHRONVS_UI_CIRCLE_SIZE);
            assert(lv_obj_get_height(child)==CHRONVS_UI_CIRCLE_SIZE);++circles;
        }
    }
    assert(circles==7);
    lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
    assert(chronvs_app_open("apps"));capture("14-app-list");
    puts("System UI: real watch, quick settings and launcher rendered; seven circle sizes checked.");
    return 0;
}
