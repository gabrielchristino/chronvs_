#define main original_agnss_tests
#include "agnss_data_test.c"
#undef main
#include "services/agnss_orbits.h"

static void rinex(chronvs_agnss_rinex_t *r, int64_t utc) {
    chronvs_agnss_rinex_init(r,utc);
    char line[160];
    snprintf(line,sizeof(line),"%9s           N%39sRINEX VERSION / TYPE\n","3.05","");
    assert(strlen(line)==81 && chronvs_agnss_rinex_feed(r,(uint8_t *)line,strlen(line)));
    snprintf(line,sizeof(line),"%60sEND OF HEADER\n","");
    assert(chronvs_agnss_rinex_feed(r,(uint8_t *)line,strlen(line)));
}
int main(int argc, char **argv) {
    chronvs_agnss_blob_t info;
    int64_t utc=1794054920; uint8_t original[1024], out[CHRONVS_AGNSS_BINARY_LIMIT];
    fixture(original,utc);
    chronvs_agnss_selection_t s; chronvs_agnss_selection_init(&s,utc);
    for (unsigned i=0;i<4;++i) {
        assert(chronvs_agnss_selection_add(&s,original+i*82));
        assert(chronvs_agnss_selection_add(&s,original+i*82)); /* repeated periods collapse */
    }
    assert(chronvs_agnss_selection_finish(&s,out)==328 && !memcmp(out,original,328));
    size_t n;
    assert(chronvs_agnss_prediction(original,328,utc,out,&n) && n==328);
    assert(!chronvs_agnss_prediction(original,327,utc,out,&n));
    assert(!chronvs_agnss_prediction(original,328,utc+10800,out,&n));
    original[45]^=1; assert(!chronvs_agnss_prediction(original,328,utc,out,&n));
    chronvs_agnss_rinex_t r; rinex(&r,utc);
    assert(!chronvs_agnss_rinex_finish(&r,out));
    rinex(&r,utc); uint8_t bad=0;
    assert(!chronvs_agnss_rinex_feed(&r,&bad,1));
    assert(!chronvs_agnss_rinex_finish(&r,out));
    /* Valid synthetic GPS LNAV, one byte per feed: exercise field boundaries,
     * quantization, GPS calendar (not UTC), PRNs and record completeness. */
    const int64_t sample_utc=1794509154;
    rinex(&r,sample_utc);
    unsigned week; double tow; assert(chronvs_agnss_time(sample_utc,&week,&tow));
    for (unsigned sv=1;sv<=4;++sv) {
        char text[120];
        double values[31]={0};values[3]=values[26]=1;values[10]=5153.5;
        values[11]=410400;values[21]=week;values[23]=2.4;values[28]=4;
        snprintf(text,sizeof(text),"G%02u 2026 11 12 18 00 00%19.12E%19.12E%19.12E\n",sv,0.0,0.0,0.0);
        for (size_t i=0;i<strlen(text);++i) assert(chronvs_agnss_rinex_feed(&r,(uint8_t *)text+i,1));
        for (unsigned row=0;row<7;++row) {
            unsigned i=3+row*4;
            snprintf(text,sizeof(text),"    %19.12E%19.12E%19.12E%19.12E\n",values[i],values[i+1],values[i+2],values[i+3]);
            for (size_t j=0;j<strlen(text);++j) assert(chronvs_agnss_rinex_feed(&r,(uint8_t *)text+j,1));
        }
    }
    assert(chronvs_agnss_rinex_finish(&r,out)==328);
    assert(get(out+10)==UINT32_C(2701918208)); /* sqrt(A) in 2^-19 metres^0.5 */
    assert(out[74]==1 && out[75]==3 && out[54]==(410400/16)%256);
    assert(chronvs_agnss_validate(out,328,sample_utc,&info));
    if (argc==5) {
        /* Real RINEX and predicted samples are optional, read-only inputs. */
        utc=strtoll(argv[3],NULL,10);
        FILE *f=fopen(argv[1],"rb"); assert(f); chronvs_agnss_rinex_init(&r,utc);
        uint8_t block[137]; size_t got;
        while ((got=fread(block,1,sizeof(block),f))) assert(chronvs_agnss_rinex_feed(&r,block,got));
        assert(!ferror(f)); fclose(f);
        n=chronvs_agnss_rinex_finish(&r,out); assert(n>=328);
        f=fopen(argv[4],"wb"); assert(f); assert(fwrite(out,1,n,f)==n); fclose(f);
        printf("Real RINEX -> CASIC: %u GPS records\n",(unsigned)(n/82));
        f=fopen(argv[2],"rb"); assert(f);
        uint8_t *prediction=malloc(CHRONVS_AGNSS_PREDICTION_LIMIT+1); assert(prediction);
        got=fread(prediction,1,CHRONVS_AGNSS_PREDICTION_LIMIT+1,f); fclose(f);
        assert(chronvs_agnss_prediction(prediction,got,utc,out,&n));
        assert(n>=328 && n<=32*82); free(prediction);
        printf("Real prediction -> CASIC: %u GPS records\n",(unsigned)(n/82));
    }
    puts("Orbit selection: checksum, truncation, expiry, unique satellites and RINEX input validation passed");
}
