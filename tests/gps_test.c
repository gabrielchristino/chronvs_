#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "services/gps_data.h"

static bool sentence(chronvs_gps_data_t *d, const char *body, uint64_t now) {
    unsigned sum = 0;
    for (const char *p = body; *p; ++p) sum ^= (unsigned char)*p;
    char line[180]; snprintf(line, sizeof(line), "$%s*%02X", body, sum);
    return chronvs_gps_parse(d, line, now);
}

int main(void) {
    chronvs_gps_data_t d = {0};
    assert(sentence(&d, "GNGGA,123519,2333.0000,S,04638.0000,W,1,08,0.9,760.5,M,0.0,M,,", 100));
    assert(d.fix && d.satellites == 8 && d.altitude_valid && d.hdop_valid);
    assert(fabs(d.latitude + 23.55) < 0.000001 && fabs(d.longitude + 46.633333333) < 0.000001);
    assert(sentence(&d, "GPRMC,123520,A,2333.0000,S,04638.0000,W,10.0,0.0,071126,,,A", 200));
    assert(d.fix && d.speed_valid && fabs(d.speed_kmh - 18.52) < 0.00001);
    assert(d.utc_valid && d.utc == 1794054920LL && d.utc_ms == 200);
    chronvs_gps_data_t saved = d;
    assert(!chronvs_gps_parse(&d, "$GPRMC,broken*00", 250));
    assert(d.position_ms == saved.position_ms && d.sentences == saved.sentences && d.errors == 1);
    assert(sentence(&d, "GNRMC,123521,V,,,,,,,071126,,,N", 300));
    assert(!d.fix && !d.speed_valid);
    assert(!d.utc_valid);
    chronvs_gps_freshen(&d, 300);
    assert(!d.altitude_valid);
    assert(sentence(&d, "GPGGA,123522,2333.0000,S,04638.0000,W,0,00,99.9,,M,,,", 400));
    assert(!d.fix && d.satellites == 0);
    assert(sentence(&d, "GPGGA,123523,2360.0,S,04638.0,W,1,08,0.9,760,M,,,", 500));
    assert(!d.fix); /* 60 minutes is invalid. */
    assert(sentence(&d, "GPRMC,123524,A,9000.0,N,18000.0,E,0,0,071126,,,A", 600));
    assert(d.fix && d.latitude == 90 && d.longitude == 180);
    assert(sentence(&d, "GPRMC,123525,A,9000.1,N,18000.0,E,0,0,071126,,,A", 700));
    assert(!d.fix);
    assert(sentence(&d, "GPRMC,123525,A,1e99,N,18000.0,E,0,0,071126,,,A", 700));
    assert(!d.fix);
    assert(sentence(&d, "GPRMC,123525,A,2333,S,04638,W,,0,071126,,,A", 800));
    assert(d.fix && !d.speed_valid);
    chronvs_gps_freshen(&d, 5800);
    assert(!d.fix && !d.satellites_valid && !d.hdop_valid);
    assert(sentence(&d, "GNGSV,1,1,00", 5900));
    assert(!d.fix && d.sentence_ms == 5900); /* Serial alone never creates a position. */
    assert(sentence(&d, "GNRMC", 6000) == false);
    assert(sentence(&d, "GPRMC,000000.250,A,2333,S,04638,W,0,0,290224,,,A", 6100));
    assert(d.utc_valid && d.utc == 1709164800LL);
    assert(sentence(&d, "GPRMC,000000,A,2333,S,04638,W,0,0,290223,,,A", 6200));
    assert(!d.utc_valid);
    assert(sentence(&d, "GPRMC,240000,A,2333,S,04638,W,0,0,010126,,,A", 6300));
    assert(!d.utc_valid);
    puts("GPS parser: checksum, talkers, no-fix, ranges and stale data passed");
    chronvs_gps_data_t view = {.fix=true,.position_ms=7000};
    assert(sentence(&view, "GPGSV,2,1,05,01,45,100,23,02,30,050,,03,20,250,19,04,10,180,20,1", 7000));
    assert(view.sky[0].valid && view.sky[0].visible == 5 && view.sky[0].cno == 23);
    assert(view.fix && view.position_ms == 7000); /* GSV must never create/remove a fix. */
    assert(sentence(&view, "BDGSV,1,1,02,11,60,250,35,12,30,100,,1", 7100));
    assert(view.sky[1].visible == 2 && view.sky[1].cno == 35);
    assert(sentence(&view, "GPGSV,2,2,05,05,30,150,42,1", 7200));
    assert(view.sky[0].cno == 42 && view.sky[0].next == 3);
    assert(sentence(&view, "GPGSV,2,2,05,05,30,150,99,1", 7300));
    assert(view.sky[0].cno == 42); /* Duplicate/out-of-order fragment ignored. */
    assert(sentence(&view, "GAGSV,2,2,05,05,30,150,99,1", 7400));
    assert(!view.sky[2].valid); /* Missing first fragment cannot fabricate a cycle. */
    assert(sentence(&view, "GPGSV,1,1,01,05,30,150,,1", 7500));
    assert(view.sky[0].visible == 1 && !view.sky[0].cno_valid);
    assert(sentence(&view, "GNGSV,1,1,00", 7600));
    assert(view.sky[2].valid && view.sky[2].visible == 0 && !view.sky[2].cno_valid);
    assert(sentence(&view, "GIGSV,1,1,01,01,30,150,100", 7700));
    assert(!view.sky[3].valid);
    chronvs_gps_freshen(&view, 12100);
    assert(!view.sky[1].valid && view.sky[0].valid);
    chronvs_gps_freshen(&view, 12600);
    assert(!view.sky[0].valid && !view.sky[2].valid);
    puts("GPS sky: per-talker GSV, fragments, optional signal ID, missing C/N0, zero and expiry passed");
    return 0;
}
