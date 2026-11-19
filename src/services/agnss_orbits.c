#include "services/agnss_orbits.h"
#include <math.h>
#include <string.h>

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }
static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
void chronvs_agnss_selection_init(chronvs_agnss_selection_t *s, int64_t utc) {
    memset(s,0,sizeof(*s)); s->utc=utc;
}
bool chronvs_agnss_selection_add(chronvs_agnss_selection_t *s, const uint8_t f[82]) {
    if (!chronvs_casic_valid(f,82) || f[4]!=8 || f[5]!=7 || le16(f+2)!=72) return false;
    const uint8_t *p=f+6;
    uint32_t sum=0; for (unsigned i=4;i<72;i+=4) sum+=le32(p+i);
    unsigned sv=p[68];
    if (sum!=le32(p) || sv<1 || sv>32 || le16(p+50)>1023 ||
        le16(p+48)*16U>=604800 || le32(p+52)*16ULL>=604800 || p[66]>15) return false;
    if (!chronvs_agnss_frame_current(f,s->utc)) return true;
    unsigned week; double tow;
    if (!chronvs_agnss_time(s->utc,&week,&tow)) return false;
    int dw=(int)(week%1024)-(int)le16(p+50);
    if (dw>512) dw-=1024;
    if (dw< -512) dw+=1024;
    double age=fabs(dw*604800.0+tow-le16(p+48)*16.0);
    uint32_t bit=UINT32_C(1)<<(sv-1);
    if (!(s->seen&bit) || age<s->ages[sv-1]) {
        memcpy(s->frames[sv-1],f,82); s->ages[sv-1]=age; s->seen|=bit;
    }
    return true;
}
size_t chronvs_agnss_selection_finish(const chronvs_agnss_selection_t *s, uint8_t *out) {
    size_t n=0;
    for (unsigned i=0;i<32;++i) if (s->seen&(UINT32_C(1)<<i)) { memcpy(out+n,s->frames[i],82); n+=82; }
    chronvs_agnss_blob_t info;
    return chronvs_agnss_validate(out,n,s->utc,&info) ? n : 0;
}
bool chronvs_agnss_prediction(const uint8_t *bytes, size_t size, int64_t utc, uint8_t *out, size_t *length) {
    /* Bounded workspace on the 8 KiB network worker stack, never the UI. */
    if (!size || size>CHRONVS_AGNSS_PREDICTION_LIMIT || size%82) return false;
    chronvs_agnss_selection_t s; chronvs_agnss_selection_init(&s,utc);
    for (size_t i=0;i<size;i+=82) if (!chronvs_agnss_selection_add(&s,bytes+i)) return false;
    *length=chronvs_agnss_selection_finish(&s,out);
    return *length!=0;
}
