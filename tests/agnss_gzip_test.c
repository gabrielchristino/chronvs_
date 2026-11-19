#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/agnss_gzip.h"
#include "esp_heap_caps.h"
void *heap_caps_malloc(size_t n,unsigned caps) { assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return malloc(n); }
void vTaskDelay(unsigned ticks) { assert(ticks==1); }
uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *bytes,size_t n) {
    crc=~crc;
    for(size_t i=0;i<n;++i) { crc^=bytes[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^((crc&1)?UINT32_C(0xEDB88320):0); }
    return ~crc;
}
int main(int argc,char **argv) {
    assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);
    uint8_t *b=malloc(CHRONVS_AGNSS_GZIP_LIMIT+1);assert(b);
    size_t n=fread(b,1,CHRONVS_AGNSS_GZIP_LIMIT+1,f);assert(!ferror(f));fclose(f);
    int64_t utc=strtoll(argv[2],NULL,10);uint8_t out[CHRONVS_AGNSS_BINARY_LIMIT],expected[CHRONVS_AGNSS_BINARY_LIMIT];size_t length;
    f=fopen(argv[3],"rb");assert(f);size_t want=fread(expected,1,sizeof(expected),f);fclose(f);
    assert(chronvs_agnss_gzip_rinex(b,n,utc,out,&length));
    assert(length==want && !memcmp(expected,out,want));
    b[n-8]^=1;assert(!chronvs_agnss_gzip_rinex(b,n,utc,out,&length));b[n-8]^=1;
    b[n-4]^=1;assert(!chronvs_agnss_gzip_rinex(b,n,utc,out,&length));b[n-4]^=1;
    assert(!chronvs_agnss_gzip_rinex(b,n-1,utc,out,&length));
    assert(!chronvs_agnss_gzip_rinex(b,10,utc,out,&length));
    b[3]|=0x80;assert(!chronvs_agnss_gzip_rinex(b,n,utc,out,&length));b[3]&=0x7F;
    b[30]^=0x40;assert(!chronvs_agnss_gzip_rinex(b,n,utc,out,&length));
    free(b);puts("Gzip reference: 32 KiB ring, real RINEX conversion, CRC/ISIZE, truncated/corrupt payload and reserved flags passed");
}
