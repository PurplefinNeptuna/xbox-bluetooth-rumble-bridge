#include <assert.h>
#include "../src/bridge_core.h"

int main(void) {
    assert(strength_scaled(0, 255) == 0);
    assert(strength_scaled(1, 255) == 1);
    assert(strength_scaled(32768, 255) == 128);
    assert(strength_scaled(65535, 255) == 255);
    assert(strength_scaled(65535, 100) == 100);

    const u8 xbox_desc[] = {0x85,3,0x75,8,0x95,6,0x91,2};
    const u8 tt_desc[] = {0x85,3,0x75,8,0x95,8,0x91,2};
    const u8 unknown_desc[] = {0x85,3,0x75,8,0x95,5,0x91,2};
    assert(report_profile_from_descriptor(xbox_desc,sizeof(xbox_desc)) == REPORT_XBOX_7);
    assert(report_profile_from_descriptor(tt_desc,sizeof(tt_desc)) == REPORT_TTMAX_9);
    assert(report_profile_from_descriptor(unknown_desc,sizeof(unknown_desc)) == REPORT_UNKNOWN);
    assert(report_profile_from_descriptor(tt_desc,sizeof(tt_desc)-1) == REPORT_UNKNOWN);

    u8 report[9];
    assert(make_report(REPORT_XBOX_7,65535,32768,report) == 7);
    assert(report[0] == 3 && report[1] == 3 && report[2] == 100 && report[3] == 50);
    assert(make_report(REPORT_TTMAX_9,65535,32768,report) == 9);
    assert(report[4] == 255 && report[5] == 128 && report[8] == 1);
    assert(make_report(REPORT_UNKNOWN,1,1,report) == 0);

    struct rumble_effect e[2] = {{.strong=65535,.weak=0,.length=4000,.delay=100},
                                  {.strong=0,.weak=32768,.length=1000}};
    u16 a,b;
    play_effect(&e[0],1000,1);
    assert(e[0].start == 1100 && e[0].end == 4100);
    play_effect(&e[1],1000,1);
    mix_effects(e,2,1050,&a,&b); assert(a == 0 && b == 32768);
    mix_effects(e,2,1100,&a,&b); assert(a == 65535 && b == 32768);
    e[1].active = 0;
    mix_effects(e,2,1200,&a,&b); assert(a == 65535 && b == 0);
    play_effect(&e[0],2000,2);
    assert(e[0].end == 5100);
    mix_effects(e,2,5099,&a,&b); assert(a == 65535 && b == 0);
    mix_effects(e,2,5100,&a,&b); assert(a == 0 && b == 0);
    e[0].length = 0;
    play_effect(&e[0],6000,1); assert(!e[0].active);
    return 0;
}
