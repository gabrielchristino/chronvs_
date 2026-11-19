#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "services/agnss_data.h"

static void put(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t get(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static size_t fixture(uint8_t *out,int64_t utc) {
    unsigned week; double tow; assert(chronvs_agnss_time(utc,&week,&tow));
    for(unsigned sv=1;sv<=4;++sv) {
        uint8_t p[72]={0}; unsigned toe=(unsigned)tow/16;
        p[48]=(uint8_t)toe;p[49]=(uint8_t)(toe>>8);p[50]=(uint8_t)(week%1024);p[51]=(uint8_t)((week%1024)>>8);
        p[68]=sv;p[69]=3;
        uint32_t sum=0;for(unsigned i=4;i<72;i+=4)sum+=get(p+i);put(p,sum);
        assert(chronvs_casic_frame(out+(sv-1)*82,8,7,p,72)==82);
    }
    uint8_t ion[16]={0}; ion[12]=3;put(ion,3);
    assert(chronvs_casic_frame(out+4*82,8,6,ion,16)==26);
    return 4*82+26;
}
int main(int argc,char **argv) {
    /* Independent zero-payload query vector: BA CE 00 00 01 00 00 00 01 00. */
    const uint8_t expected[]={0xBA,0xCE,0,0,1,0,0,0,1,0};
    uint8_t q[10]; assert(chronvs_casic_frame(q,1,0,NULL,0)==10);
    assert(!memcmp(q,expected,10) && chronvs_casic_valid(q,10));
    q[9]^=1; assert(!chronvs_casic_valid(q,10));
    unsigned week; double tow;
    assert(chronvs_agnss_time(1483228800,&week,&tow) && week==1930 && tow==18);
    assert(!chronvs_agnss_time(1483228799,&week,&tow));
    assert(!chronvs_agnss_time(1814400000,&week,&tow));
    uint8_t aid[66]; chronvs_agnss_reference_t ref={-23.668,-46.690,5000,true};
    assert(chronvs_agnss_aid(aid,1794054920,&ref)==66 && chronvs_casic_valid(aid,66));
    assert(aid[61]==0x63 && aid[4]==0x0B && aid[5]==1);
    double latitude; memcpy(&latitude,aid+6,8); assert(fabs(latitude-ref.latitude)<1e-9);
    float variance; memcpy(&variance,aid+42,4); assert(variance==25000000);
    ref.latitude=NAN; assert(!chronvs_agnss_aid(aid,1794054920,&ref));
    assert(chronvs_agnss_aid(aid,1794054920,NULL)==66 && aid[61]==2);
    uint8_t blob[1024]; size_t n=fixture(blob,1794054920);
    chronvs_agnss_blob_t info;
    assert(chronvs_agnss_validate(blob,n,1794054920,&info) && info.frames==5 && info.satellites==4);
    assert(!chronvs_agnss_validate(blob,n,1794054920+10800,&info));
    blob[n]=0; assert(!chronvs_agnss_validate(blob,n+1,1794054920,&info));
    blob[30]^=1; assert(!chronvs_agnss_validate(blob,n,1794054920,&info)); blob[30]^=1;
    memcpy(blob+82,blob,82); assert(!chronvs_agnss_validate(blob,n,1794054920,&info));
    n=fixture(blob,1794054920);
    /* Re-signed commands outside the whitelist cannot reach the receiver. */
    blob[4]=6;put(blob+78,chronvs_casic_checksum(blob,72));
    assert(!chronvs_agnss_validate(blob,n,1794054920,&info));
    puts("AGNSS data: independent framing, UTC/GPS rollover, uncertainty, bounded whitelist, checksums, stale and duplicates passed");
    if (argc==3) {
        FILE *file=fopen(argv[1],"rb");assert(file);
        uint8_t real[CHRONVS_AGNSS_BINARY_LIMIT+1];size_t length=fread(real,1,sizeof(real),file);fclose(file);
        assert(chronvs_agnss_validate(real,length,strtoll(argv[2],NULL,10),&info));
        printf("Downloaded CASIC sample validated: %u fresh GPS satellites, %u ionosphere\n",info.satellites,info.ionosphere);
    }
    return 0;
}
