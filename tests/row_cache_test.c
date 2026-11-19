#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/row_cache.h"
int main(void) {
    uint32_t arena[128];
    chronvs_row_cache_t cache;
    uint16_t pixels[4][10], row[8];
    for (unsigned y=0;y<4;++y) for (unsigned x=0;x<10;++x)
        pixels[y][x] = x<3 ? 0x1234 : x<8 ? (uint16_t)(0x1000+y) : 0xFFFF;
    assert(chronvs_row_cache_init(&cache,arena,sizeof(arena),8,4));
    assert(!chronvs_row_cache_decode(&cache,0,row));
    assert(!chronvs_row_cache_record(&cache,2,2,pixels[2],10));
    assert(chronvs_row_cache_record(&cache,0,2,pixels[0],10) && !cache.valid);
    assert(chronvs_row_cache_record(&cache,2,2,pixels[2],10) && cache.valid);
    assert(cache.count==8);
    for(unsigned y=0;y<4;++y) {
        assert(chronvs_row_cache_decode(&cache,(uint16_t)y,row));
        assert(!memcmp(row,pixels[y],sizeof(row)));
    }
    chronvs_row_cache_clear(&cache);
    assert(chronvs_row_cache_record(&cache,0,1,pixels[0],10));
    assert(!chronvs_row_cache_record(&cache,2,1,pixels[2],10) && !cache.valid);
    assert(chronvs_row_cache_record(&cache,0,4,pixels[0],10) && cache.valid);
    /* Too little storage for this image: stop retries until invalidated. */
    assert(chronvs_row_cache_init(&cache,arena,16,8,4));
    assert(!chronvs_row_cache_record(&cache,0,4,pixels[0],10) && cache.blocked);
    assert(!chronvs_row_cache_record(&cache,0,4,pixels[0],10));
    assert(!chronvs_row_cache_init(&cache,(char *)arena+1,sizeof(arena)-1,8,4));
    assert(!chronvs_row_cache_init(&cache,arena,4,8,4));
    assert(!chronvs_row_cache_init(&cache,NULL,sizeof(arena),8,4));
    assert(!chronvs_row_cache_record(&cache,0,4,pixels[0],10));
    puts("Row cache: partial strips, stride, exact RGB565, invalidation, incomplete/out-of-order data and bounded overflow passed.");
}
