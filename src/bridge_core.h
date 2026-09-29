#ifndef BRIDGE_CORE_H
#define BRIDGE_CORE_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed long s64;

enum report_profile { REPORT_UNKNOWN, REPORT_XBOX_7, REPORT_TTMAX_9 };

struct rumble_effect {
    u16 strong, weak, length, delay;
    s64 start, end;
    u8 active;
};

static u32 min_u32(u32 a, u32 b) { return a < b ? a : b; }

/* Round 16-bit FF strength to the report's range. Keep nonzero effects audible. */
static u8 strength_scaled(u16 magnitude, u32 maximum) {
    u32 rounded = ((u32)magnitude * maximum + 32767) / 65535;
    return (u8)(magnitude && !rounded ? 1 : rounded);
}

static void play_effect(struct rumble_effect *e, s64 now, int repeats) {
    if (repeats <= 0 || e->length == 0) { e->active = 0; return; }
    u32 total = min_u32((u32)e->length * min_u32((u32)repeats, 65535), 3000);
    e->start = now + e->delay;
    e->end = e->start + total;
    e->active = 1;
}

static void mix_effects(struct rumble_effect *effects, int count, s64 now, u16 *strong, u16 *weak) {
    *strong = *weak = 0;
    for (int i = 0; i < count; i++) {
        struct rumble_effect *e = effects + i;
        if (!e->active) continue;
        if (now >= e->end) { e->active = 0; continue; }
        if (now < e->start) continue;
        if (e->strong > *strong) *strong = e->strong;
        if (e->weak > *weak) *weak = e->weak;
    }
}

/* Count Output bits for report ID 3, including local/global HID items. */
static enum report_profile report_profile_from_descriptor(const u8 *d, u32 n) {
    u32 size = 0, count = 0, id = 0, bits = 0;
    u32 stack[16][3]; int depth = 0;
    for (u32 i = 0; i < n;) {
        u8 tag = d[i++];
        if (tag == 0xfe) {
            if (i + 2 > n) return REPORT_UNKNOWN;
            u32 length = d[i++]; i++; if (i + length > n) return REPORT_UNKNOWN;
            i += length; continue;
        }
        u32 length = tag & 3; if (length == 3) length = 4;
        if (i + length > n) return REPORT_UNKNOWN;
        u32 value = 0;
        for (u32 j = 0; j < length; j++) value |= (u32)d[i + j] << (j * 8);
        i += length;
        u8 kind = tag & 0xfc;
        if (kind == 0x74) size = value;
        else if (kind == 0x94) count = value;
        else if (kind == 0x84) id = value;
        else if (kind == 0xa4) {
            if (depth == 16) return REPORT_UNKNOWN;
            stack[depth][0] = size; stack[depth][1] = count; stack[depth++][2] = id;
        } else if (kind == 0xb4) {
            if (!depth) return REPORT_UNKNOWN;
            depth--; size = stack[depth][0]; count = stack[depth][1]; id = stack[depth][2];
        } else if (kind == 0x90 && id == 3) {
            if (size > 1024 || count > 1024 || bits + size * count > 8192) return REPORT_UNKNOWN;
            bits += size * count;
        }
    }
    if (bits == 48) return REPORT_XBOX_7;
    if (bits == 64) return REPORT_TTMAX_9;
    return REPORT_UNKNOWN;
}

static int make_report(enum report_profile profile, u16 strong, u16 weak, u8 *out) {
    if (profile == REPORT_XBOX_7) {
        out[0] = 3; out[1] = 3; out[2] = strength_scaled(strong, 100); out[3] = strength_scaled(weak, 100);
        out[4] = 255; out[5] = 0; out[6] = 255;
        return 7;
    }
    if (profile == REPORT_TTMAX_9) {
        out[0] = 3; out[1] = 3; out[2] = 0; out[3] = 0;
        out[4] = strength_scaled(strong, 255); out[5] = strength_scaled(weak, 255);
        out[6] = 0; out[7] = 0; out[8] = 1;
        return 9;
    }
    return 0;
}

#endif
