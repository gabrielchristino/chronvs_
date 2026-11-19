#include "services/agnss_data.h"
#include <math.h>
#include <string.h>

static unsigned u16(const uint8_t *p) { return p[0] | ((unsigned)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t value) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(value>>(i*8)); }
static void real64(uint8_t *p, double value) {
    uint64_t bits; memcpy(&bits, &value, 8);
    for (unsigned i=0;i<8;++i) p[i]=(uint8_t)(bits>>(i*8));
}
static void real32(uint8_t *p, float value) { uint32_t bits; memcpy(&bits, &value, 4); put32(p, bits); }
uint32_t chronvs_casic_checksum(const uint8_t *f, size_t size) {
    uint32_t sum = (uint32_t)f[5]<<24 | (uint32_t)f[4]<<16 | (uint32_t)size;
    for (size_t i=0; i<size; i+=4) sum += u32(f+6+i);
    return sum;
}
size_t chronvs_casic_frame(uint8_t *out, uint8_t cls, uint8_t id, const uint8_t *payload, size_t size) {
    if (size % 4 || size+10 > CHRONVS_CASIC_FRAME_LIMIT) return 0;
    out[0]=0xBA; out[1]=0xCE; out[2]=(uint8_t)size; out[3]=(uint8_t)(size>>8); out[4]=cls; out[5]=id;
    if (size) memcpy(out+6, payload, size);
    put32(out+6+size, chronvs_casic_checksum(out, size));
    return size+10;
}
bool chronvs_casic_valid(const uint8_t *f, size_t size) {
    if (size<10 || f[0]!=0xBA || f[1]!=0xCE) return false;
    size_t payload = u16(f+2);
    return payload%4==0 && payload+10==size && chronvs_casic_checksum(f,payload)==u32(f+6+payload);
}
bool chronvs_agnss_time(int64_t utc, unsigned *week, double *tow) {
    /* The current table includes the leap second effective 2017-01-01.
     * C72 excludes a leap at December 2026; review before July 2027. */
    if (utc < 1483228800LL || utc >= 1814400000LL) return false;
    int64_t gps = utc-315964800LL+18;
    *week = (unsigned)(gps/604800); *tow = (double)(gps%604800);
    return true;
}
bool chronvs_agnss_frame_current(const uint8_t *frame, int64_t utc) {
    unsigned week; double tow;
    if (!chronvs_agnss_time(utc, &week, &tow)) return false;
    if (frame[4]==8 && frame[5]==6) return frame[6+12]==3;
    if (frame[4]!=8 || frame[5]!=7 || u16(frame+2)!=72) return false;
    const uint8_t *p=frame+6;
    int delta_week=(int)(week%1024)-(int)u16(p+50);
    if (delta_week>512) delta_week-=1024;
    if (delta_week< -512) delta_week+=1024;
    double age=delta_week*604800.0+tow-u16(p+48)*16.0;
    return p[67]==0 && p[69]==3 && fabs(age)<=7200;
}
bool chronvs_agnss_validate(const uint8_t *bytes, size_t size, int64_t utc, chronvs_agnss_blob_t *out) {
    if (!out || size<20 || size>CHRONVS_AGNSS_BINARY_LIMIT) return false;
    size_t start=0;
    if (bytes[0]!=0xBA || bytes[1]!=0xCE) {
        static const char prefix[]="AGNSS data from CASIC.\n";
        if (size<sizeof(prefix)-1 || memcmp(bytes,prefix,sizeof(prefix)-1)) return false;
        while (start+1<size && start<128 && (bytes[start]!=0xBA || bytes[start+1]!=0xCE)) ++start;
        if (start>=128) return false;
    }
    size_t pos=start, frames=0;
    uint32_t seen=0;
    bool ion=false;
    unsigned fresh=0;
    while (pos+10<=size) {
        const uint8_t *f=bytes+pos;
        size_t payload=u16(f+2), n=payload+10;
        if (n>size-pos || !chronvs_casic_valid(f,n) || f[4]!=8) return false;
        if (!((f[5]==7 && payload==72) || (f[5]==6 && payload==16))) return false;
        uint32_t inner=0;
        for (size_t i=4;i<payload;i+=4) inner+=u32(f+6+i);
        if (inner!=u32(f+6)) return false;
        if (f[5]==7) {
            unsigned sv=f[6+68];
            if (sv<1 || sv>32 || (seen & (1UL<<(sv-1)))) return false;
            seen|=1UL<<(sv-1);
            if (chronvs_agnss_frame_current(f,utc)) ++fresh;
        } else { if (ion) return false; ion=true; }
        pos+=n; ++frames;
        if (frames>33) return false;
    }
    if (pos!=size || fresh<4) return false;
    bool valid_ion=false;
    for (size_t i=start;i<size;) {
        if (bytes[i+5]==6) valid_ion=chronvs_agnss_frame_current(bytes+i,utc);
        i+=10+u16(bytes+i+2);
    }
    *out=(chronvs_agnss_blob_t){.start=start,.frames=frames,.satellites=fresh,.ionosphere=valid_ion};
    return true;
}
size_t chronvs_agnss_aid(uint8_t out[66], int64_t utc, const chronvs_agnss_reference_t *ref) {
    unsigned week; double tow;
    if (!chronvs_agnss_time(utc,&week,&tow)) return 0;
    uint8_t payload[56]={0};
    payload[55]=2; /* Time valid. No invented position or clock-drift data. */
    if (ref && ref->valid) {
        if (!isfinite(ref->latitude) || !isfinite(ref->longitude) || !isfinite(ref->radius_m) ||
            fabs(ref->latitude)>90 || fabs(ref->longitude)>180 || ref->radius_m<100 || ref->radius_m>15000) return 0;
        real64(payload,ref->latitude); real64(payload+8,ref->longitude);
        /* CASIC V4.2: variance in m²; altitude explicitly unknown. */
        real32(payload+36,ref->radius_m*ref->radius_m);
        payload[55]|=0x61; /* Position valid, LLA, altitude invalid. */
    }
    real64(payload+24,tow);
    /* V4.2 scales time variance by c². Conservative uncertainty: 2 seconds. */
    real32(payload+40,3.5950207149472704e17f);
    payload[52]=(uint8_t)week; payload[53]=(uint8_t)(week>>8);
    return chronvs_casic_frame(out,0x0B,0x01,payload,sizeof(payload));
}
