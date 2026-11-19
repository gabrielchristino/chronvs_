#include "services/agnss_gzip.h"
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "miniz.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint32_t le32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
bool chronvs_agnss_gzip_rinex(const uint8_t *b, size_t size, int64_t utc, uint8_t *out, size_t *length) {
    if (size<18 || size>CHRONVS_AGNSS_GZIP_LIMIT || b[0]!=31 || b[1]!=139 || b[2]!=8 || (b[3]&0xE0)) return false;
    size_t pos=10, end=size-8;
    if (b[3]&4) {
        if (pos+2>end) return false;
        size_t extra=b[pos]|(size_t)b[pos+1]<<8; pos+=2;
        if (extra>end-pos) return false;
        pos+=extra;
    }
    for (unsigned flag=8;flag<=16;flag<<=1) if (b[3]&flag) {
        while (pos<end && b[pos]) ++pos;
        if (pos==end) return false;
        ++pos;
    }
    if (b[3]&2) {
        if (pos+2>end || (esp_rom_crc32_le(0,b,pos)&65535)!=(unsigned)(b[pos]|b[pos+1]<<8)) return false;
        pos+=2;
    }
    if (pos>=end || le32(b+size-4)>16*1024*1024U) return false;
    uint8_t *ring=heap_caps_malloc(TINFL_LZ_DICT_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    tinfl_decompressor *d=heap_caps_malloc(sizeof(*d),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    chronvs_agnss_rinex_t *r=heap_caps_malloc(sizeof(*r),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool ok=false;
    if (ring && d && r) {
        tinfl_init(d); chronvs_agnss_rinex_init(r,utc);
        size_t offset=0,total=0; uint32_t crc=0;
        for (;;) {
            size_t input=end-pos, output=TINFL_LZ_DICT_SIZE-offset;
            tinfl_status status=tinfl_decompress(d,b+pos,&input,ring,ring+offset,&output,0);
            pos+=input; total+=output;
            if (total>16*1024*1024U || !chronvs_agnss_rinex_feed(r,ring+offset,output)) break;
            crc=esp_rom_crc32_le(crc,ring+offset,output);
            offset=(offset+output)&(TINFL_LZ_DICT_SIZE-1);
            if (status==TINFL_STATUS_DONE) {
                if (pos==end && crc==le32(b+size-8) && total==le32(b+size-4)) {
                    *length=chronvs_agnss_rinex_finish(r,out); ok=*length!=0;
                }
                break;
            }
            if (status<0 || (!input && !output) || (status==TINFL_STATUS_NEEDS_MORE_INPUT && pos==end)) break;
            /* Yield between 32 KiB chunks; UI stays on its own task. */
            vTaskDelay(1);
        }
    }
    free(r); free(d); free(ring); return ok;
}
