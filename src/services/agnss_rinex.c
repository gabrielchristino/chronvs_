#include "services/agnss_orbits.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(uint8_t *p, uint32_t v, unsigned n) { for (unsigned i=0;i<n;++i) p[i]=(uint8_t)(v>>(8*i)); }
static bool quant(uint8_t *p, double value, int power, unsigned n, bool sign) {
    double q=round(ldexp(value,-power));
    double low=sign ? -ldexp(1,n*8-1) : 0;
    double high=sign ? ldexp(1,n*8-1)-1 : ldexp(1,n*8)-1;
    if (!isfinite(q) || q<low || q>high) return false;
    put(p,(uint32_t)(int64_t)q,n); return true;
}
static bool integer(double v, unsigned max) { return isfinite(v) && v>=0 && v<=max && floor(v)==v; }
static bool encode(chronvs_agnss_rinex_t *r) {
    const double *v=r->values;
    if (!integer(v[21],65535) || !integer(v[24],255) || !integer(v[26],1023) ||
        !integer(v[3],255) || ((unsigned)v[26]&255)!=(unsigned)v[3] ||
        v[10]<3000 || v[10]>8000 || v[8]<0 || v[8]>=1 ||
        v[11]<0 || v[11]>=604800 || v[23]<0 || (v[28]>0 && v[28]<4)) return false;
    uint8_t p[72]={0}, f[82];
    /* GPS LNAV semicircles use the IS-GPS-200 value of pi. RINEX is radians.
     * Fields/quantization follow CASIC MSG-GPSEPH, independently checked against
     * real broadcast frames; no downloaded configuration messages are emitted. */
    const double pi=3.1415926535898;
    struct field { unsigned index, offset, bytes; int power; bool sign, angle; };
    static const struct field fields[]={
        {10,4,4,-19,false,false},{8,8,4,-33,false,false},
        {17,12,4,-31,true,true},{6,16,4,-31,true,true},{15,20,4,-31,true,true},
        {13,24,4,-31,true,true},{18,28,4,-43,true,true},{5,32,2,-43,true,true},
        {19,34,2,-43,true,true},{7,36,2,-29,true,false},{9,38,2,-29,true,false},
        {16,40,2,-5,true,false},{4,42,2,-5,true,false},{12,44,2,-29,true,false},
        {14,46,2,-29,true,false},{11,48,2,4,false,false},
        {0,56,4,-31,true,false},{1,60,2,-43,true,false},{2,62,1,-55,true,false},
        {25,63,1,-31,true,false}};
    for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        const struct field *a=fields+i;
        if (!quant(p+a->offset,v[a->index]/(a->angle?pi:1),a->power,a->bytes,a->sign)) return false;
    }
    put(p+50,(unsigned)v[21]%1024,2);
    if (!quant(p+52,r->toc,4,4,false)) return false;
    put(p+64,(unsigned)v[26],2);
    static const double ura[]={2.4,3.4,4.85,6.85,9.65,13.65,24,48,96,192,384,768,1536,3072,6144};
    while (p[66]<15 && v[23]>ura[p[66]]) ++p[66];
    p[67]=(unsigned)v[24]; p[68]=r->sv; p[69]=p[67]?1:3;
    uint32_t sum=0;
    for (unsigned i=4;i<72;i+=4) sum+=(uint32_t)p[i]|(uint32_t)p[i+1]<<8|(uint32_t)p[i+2]<<16|(uint32_t)p[i+3]<<24;
    put(p,sum,4); chronvs_casic_frame(f,8,7,p,72);
    return chronvs_agnss_selection_add(&r->selected,f);
}
static bool number(const char *line, size_t offset, double *value) {
    size_t n=strlen(line); char field[20]={0};
    if (offset>=n) { *value=0; return true; }
    size_t count=n-offset; if (count>19) count=19;
    memcpy(field,line+offset,count);
    for (size_t i=0;i<count;++i) if (field[i]=='D' || field[i]=='d') field[i]='E';
    char *end; *value=strtod(field,&end);
    if (end==field) { for (size_t i=0;i<count;++i) if (!isspace((unsigned char)field[i])) return false; }
    else while (*end) if (!isspace((unsigned char)*end++)) return false;
    return isfinite(*value);
}
static bool line(chronvs_agnss_rinex_t *r) {
    char *s=r->line; size_t n=r->length; s[n]=0;
    if (!r->header) {
        if (n>=80 && !strncmp(s+60,"RINEX VERSION / TYPE",20))
            r->version=s[5]=='3' && s[20]=='N';
        if (n>=73 && !strncmp(s+60,"END OF HEADER",13)) { r->header=true; return r->version; }
        return true;
    }
    if (!r->row) {
        if (s[0]!='G') return true;
        if (n<80 || !isdigit((unsigned char)s[1]) || !isdigit((unsigned char)s[2]) || s[3]!=' ') return false;
        r->sv=(s[1]-'0')*10+s[2]-'0'; if (r->sv<1 || r->sv>32) return false;
        int y,m,d,h,min,sec;
        if (sscanf(s+4,"%d %d %d %d %d %d",&y,&m,&d,&h,&min,&sec)!=6 ||
            y<2017 || y>2027 || m<1 || m>12 || d<1 || d>31 || h<0 || h>23 || min<0 || min>59 || sec<0 || sec>59) return false;
        /* Civil date -> GPS calendar seconds, no UTC leap offset: RINEX GPS
         * epochs already use GPS time. Avoid libc TZ and timegm dependencies. */
        static const unsigned month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
        bool leap=y%4==0 && (y%100!=0 || y%400==0);
        if (d>(int)month_days[m-1]+(m==2 && leap)) return false;
        int64_t days=0;
        for (int year=1980;year<y;++year) days+=365+(year%4==0 && (year%100!=0 || year%400==0));
        for (int month=1;month<m;++month) days+=month_days[month-1]+(month==2 && leap);
        days+=d-1-5;
        r->toc=(double)((days*86400+h*3600+min*60+sec)%604800);
        memset(r->values,0,sizeof(r->values));
        for (unsigned i=0;i<3;++i) if (!number(s,23+19*i,r->values+i)) return false;
        r->row=1; return true;
    }
    if (n<4 || strncmp(s,"    ",4)) return false;
    unsigned base=3+(r->row-1)*4;
    /* Only the last row may omit unused trailing fields. */
    if (r->row<7 && n<80) return false;
    for (unsigned i=0;i<4;++i) if (!number(s,4+19*i,r->values+base+i)) return false;
    if (++r->row==8) { r->row=0; return encode(r); }
    return true;
}
void chronvs_agnss_rinex_init(chronvs_agnss_rinex_t *r, int64_t utc) {
    memset(r,0,sizeof(*r)); chronvs_agnss_selection_init(&r->selected,utc);
}
bool chronvs_agnss_rinex_feed(chronvs_agnss_rinex_t *r, const uint8_t *bytes, size_t size) {
    if (r->failed) return false;
    for (size_t i=0;i<size;++i) {
        if (bytes[i]=='\r') continue;
        if (bytes[i]=='\n') {
            if (!line(r)) { r->failed=true; return false; } r->length=0;
        } else {
            if (bytes[i]<32 || bytes[i]>126 || r->length>=sizeof(r->line)-1) { r->failed=true; return false; }
            r->line[r->length++]=(char)bytes[i];
        }
    }
    return true;
}
size_t chronvs_agnss_rinex_finish(chronvs_agnss_rinex_t *r, uint8_t *out) {
    if (r->failed || (r->length && !line(r)) || !r->header || r->row) return 0;
    return chronvs_agnss_selection_finish(&r->selected,out);
}
