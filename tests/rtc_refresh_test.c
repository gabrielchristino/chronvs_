#define main service_regressions
#include "Relogio_service_test.c"
#undef main
#include "../src/services/rtc_service.c"
static unsigned reads;
static bool read_fails;
static uint8_t registers[7]={0,0,0,1,6,1,0x26};
int i2c_master_write_read_device(int bus,uint8_t addr,const uint8_t *reg,size_t write_len,
                               uint8_t *out,size_t read_len,unsigned ticks) {
    (void)bus; assert(addr==0x51 && *reg==4 && write_len==1 && read_len==7 && ticks==10);
    ++reads; if (read_fails) return ESP_FAIL;
    memcpy(out,registers,7); return ESP_OK;
}
int main(void) {
    chronvs_Relogio_init();
    read_fails=true; chronvs_rtc_refresh(false); assert(reads==1);
    now_us=999999; chronvs_rtc_refresh(false); assert(reads==1);
    now_us=1000000; read_fails=false; chronvs_rtc_refresh(false); assert(reads==2);
    for(unsigned i=0;i<5900;++i) { now_us+=10000; chronvs_rtc_refresh(false); }
    assert(reads==2); /* Trusted clock: no one-second I2C polling. */
    chronvs_time_t current; assert(chronvs_Relogio_time(&current) && current.second==59);
    now_us+=1000000; registers[1]=1; chronvs_rtc_refresh(false); assert(reads==3);
    chronvs_rtc_refresh(true); now_us+=86400000000LL;
    chronvs_rtc_refresh(true); assert(reads==3);
    registers[3]=2; chronvs_rtc_refresh(false); assert(reads==4);
    assert(chronvs_Relogio_time(&current) && current.day==2);
    /* Stale/failed RTC after wake cannot move trusted time backwards. */
    chronvs_rtc_refresh(true); now_us+=1000000; registers[3]=1;
    chronvs_rtc_refresh(false); assert(chronvs_Relogio_time(&current) && current.day==2);
    chronvs_rtc_refresh(true); now_us+=1000000; read_fails=true;
    chronvs_rtc_refresh(false); assert(chronvs_Relogio_time(&current) && current.day==2);
    unsigned before=reads;
    now_us+=59000000; chronvs_rtc_refresh(false); assert(reads==before);
    /* NTP forward/backward correction remains authoritative. */
    set_time(26,9,25,12,0,0); chronvs_rtc_refresh(true); chronvs_rtc_refresh(false);
    assert(chronvs_Relogio_time(&current) && current.month==9);
    puts("RTC: boot retry, 60-second cadence, wake, no reads asleep and trusted fallback passed.");
    return 0;
}
