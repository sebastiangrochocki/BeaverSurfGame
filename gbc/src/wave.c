/* Integer main wave sampled into a 64-entry table, a quarter of it per frame.
 * Main-wave samples mirror sim/src/wave.rs; the GBC lower-lane shaping differs.
 *
 * The Game Boy has no multiplier or divider, so this file reshapes the arithmetic without
 * changing a single result: envelope values and meter positions come from generated ROM tables,
 * angles are byte additions, amplitude uses 8-bit factors by quarter squares,
 * and 0..7 interpolation weights use shift-and-add. The main-wave ROM cache is
 * checked against every sample of this reference calculation.
 *
 * On the Game Boy this file lives in ROM bank 2. The callers are banked, so the lookup tables
 * stay visible for the whole call and the fixed bank is free for the rest of the game. */
#ifdef __SDCC
#pragma bank 2
#endif
#include "rules.h"
#include "wave_tables.h"
#ifdef __SDCC
void wave_lookup(uint16_t tick,uint8_t tier) __nonbanked;
#endif

static const int8_t SIN8[256] = {
    0,    3,    6,    9,    12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,
    46,   49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,
    85,   88,   90,   92,   94,   96,   98,   100,  102,  104,  106,  107,  109,  111,  112,
    113,  115,  116,  117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,
    126,  127,  127,  127,  127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,
    122,  122,  121,  120,  118,  117,  116,  115,  113,  112,  111,  109,  107,  106,  104,
    102,  100,  98,   96,   94,   92,   90,   88,   85,   83,   81,   78,   76,   73,   71,
    68,   65,   63,   60,   57,   54,   51,   49,   46,   43,   40,   37,   34,   31,   28,
    25,   22,   19,   16,   12,   9,    6,    3,    0,    -3,   -6,   -9,   -12,  -16,  -19,
    -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,  -49,  -51,  -54,  -57,  -60,  -63,
    -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,  -90,  -92,  -94,  -96,  -98,
    -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116, -117, -118, -120, -121,
    -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127, -127, -127, -127,
    -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118, -117, -116,
    -115, -113, -112, -111, -109, -107, -106, -104, -102, -100, -98,  -96,  -94,  -92,  -90,
    -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
    -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,  -9,   -6,
    -3};

/* floor(n * n / 4) for 0 <= n < 256: a * b == QSQ[|a + b|] - QSQ[|a - b|] exactly. */
static const uint16_t QSQ[256] = {
    0,     0,     1,     2,     4,     6,     9,     12,    16,    20,    25,    30,    36,
    42,    49,    56,    64,    72,    81,    90,    100,   110,   121,   132,   144,   156,
    169,   182,   196,   210,   225,   240,   256,   272,   289,   306,   324,   342,   361,
    380,   400,   420,   441,   462,   484,   506,   529,   552,   576,   600,   625,   650,
    676,   702,   729,   756,   784,   812,   841,   870,   900,   930,   961,   992,   1024,
    1056,  1089,  1122,  1156,  1190,  1225,  1260,  1296,  1332,  1369,  1406,  1444,  1482,
    1521,  1560,  1600,  1640,  1681,  1722,  1764,  1806,  1849,  1892,  1936,  1980,  2025,
    2070,  2116,  2162,  2209,  2256,  2304,  2352,  2401,  2450,  2500,  2550,  2601,  2652,
    2704,  2756,  2809,  2862,  2916,  2970,  3025,  3080,  3136,  3192,  3249,  3306,  3364,
    3422,  3481,  3540,  3600,  3660,  3721,  3782,  3844,  3906,  3969,  4032,  4096,  4160,
    4225,  4290,  4356,  4422,  4489,  4556,  4624,  4692,  4761,  4830,  4900,  4970,  5041,
    5112,  5184,  5256,  5329,  5402,  5476,  5550,  5625,  5700,  5776,  5852,  5929,  6006,
    6084,  6162,  6241,  6320,  6400,  6480,  6561,  6642,  6724,  6806,  6889,  6972,  7056,
    7140,  7225,  7310,  7396,  7482,  7569,  7656,  7744,  7832,  7921,  8010,  8100,  8190,
    8281,  8372,  8464,  8556,  8649,  8742,  8836,  8930,  9025,  9120,  9216,  9312,  9409,
    9506,  9604,  9702,  9801,  9900,  10000, 10100, 10201, 10302, 10404, 10506, 10609, 10712,
    10816, 10920, 11025, 11130, 11236, 11342, 11449, 11556, 11664, 11772, 11881, 11990, 12100,
    12210, 12321, 12432, 12544, 12656, 12769, 12882, 12996, 13110, 13225, 13340, 13456, 13572,
    13689, 13806, 13924, 14042, 14161, 14280, 14400, 14520, 14641, 14762, 14884, 15006, 15129,
    15252, 15376, 15500, 15625, 15750, 15876, 16002, 16129, 16256};

uint8_t wave_tier(uint32_t tick) BANKED {
    uint32_t t = tick / (60UL * 15UL);
    return t > 8 ? 8 : (uint8_t)t;
}

uint8_t front_num = 12;
static int8_t front_meter_target;
static uint8_t front_meter_tick,front_meter_valid;

/* Higher crests every stage through the inherited sea amplitude. Shallow
 * troughs keep the lower rider above the footer; crests retain their full lift. */
static uint8_t front_scale(uint8_t tier) {
    if (tier > 8) tier = 8;
    return (uint8_t)(12 + (tier >> 1));
}

/* a * b for a >= 0: one absolute value and the sign of b decide everything. */
static int16_t mul8u(uint8_t a, int8_t b) {
    uint8_t ab = b < 0 ? (uint8_t)-b : (uint8_t)b;
    uint8_t sum = (uint8_t)(a + ab);
    uint8_t dif = a > ab ? (uint8_t)(a - ab) : (uint8_t)(ab - a);
    int16_t prod = (int16_t)(QSQ[sum] - QSQ[dif]);
    return b < 0 ? (int16_t)-prod : prod;
}

/* Per-refresh state lives in statics: SDCC reaches them in one instruction instead of
 * juggling the stack, and the entry loop walks pointers instead of recomputing addresses. */
static uint16_t rf_shift;
static uint8_t rf_t1, rf_t2;
static const int8_t *rf_amp1, *rf_amp2;
static uint8_t rq_index, rq_a0, rq_a1, rq_a2, rq_delta, rq_left;
static uint16_t rq_shifted;
static int16_t rq_sum;
static int16_t *rq_dst;

#ifdef __SDCC
/* The entry loop of refresh_quarter in assembly: the same table lookups, quarter-square
 * products and shifts as the C loop below it, which the host trace test still runs. The
 * phase (rq_shifted) rides on the stack across an entry; rq_mul is the product of a
 * non-negative amplitude and a signed sine, both under 128 in magnitude. */
static void rq_run(void) __naked {
    __asm
        ld a, #(TABLE_LEN / 4)
        ld (#_rq_left), a
        ld hl, #_rq_shifted
        ld a, (hl+)
        ld d, (hl)
        ld e, a
rq_loop:
        push de
        ld hl, #_ENV_AMP
        add hl, de
        ld e, (hl)
        ld d, #0x00
        ld hl, #_rf_amp1
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        add hl, de
        ld b, (hl)
        ld hl, #_rf_amp2
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        add hl, de
        ld c, (hl)
        push bc
        ld a, (#_rq_a0)
        ld e, a
        ld hl, #_SIN8
        add hl, de
        ld c, (hl)
        ld a, b
        call rq_mul
        sra h
        rr l
        sra h
        rr l
        pop bc
        push hl
        ld a, (#_rq_a1)
        ld e, a
        ld d, #0x00
        ld hl, #_SIN8
        add hl, de
        ld a, c
        ld c, (hl)
        call rq_mul
        sra h
        rr l
        sra h
        rr l
        pop de
        add hl, de
        push hl
        ld a, (#_rq_a2)
        ld e, a
        ld d, #0x00
        ld hl, #_SIN8
        add hl, de
        ld a, (hl)
        ld l, a
        rlca
        sbc a, a
        ld h, a
        add hl, hl
        ld e, l
        ld d, h
        add hl, hl
        add hl, de
        sra h
        rr l
        sra h
        rr l
        pop de
        add hl, de
        pop de
        push hl
        ld hl, #_ENV_LIFT
        add hl, de
        add hl, de
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        pop bc
        add hl, bc
        ld bc, #HORIZON
        add hl, bc
        ld a, e
        add a, #32
        ld e, a
        ld a, d
        adc a, #0x00
        and a, #0x01
        ld d, a
        push de
        ld e, l
        ld d, h
        ld hl, #_rq_dst
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld (hl), e
        inc hl
        ld (hl), d
        ld de, #0x0007
        add hl, de
        ld a, l
        ld (#_rq_dst), a
        ld a, h
        ld (#_rq_dst + 1), a
        ld hl, #_rq_a0
        ld a, (hl)
        add a, #32
        ld (hl), a
        ld hl, #_rq_a1
        ld a, (hl)
        add a, #64
        ld (hl), a
        ld hl, #_rq_a2
        ld a, (hl)
        add a, #16
        ld (hl), a
        pop de
        ld hl, #_rq_left
        dec (hl)
        jp NZ, rq_loop
        ret
rq_mul:
        ld b, a
        ld a, c
        bit 7, a
        jr Z, rqm_pos
        cpl
        inc a
rqm_pos:
        ld d, a
        ld a, b
        sub a, d
        jr NC, rqm_dif
        cpl
        inc a
rqm_dif:
        ld e, a
        ld a, b
        add a, d
        bit 7, c
        jr Z, rqm_order
        ld b, e
        ld e, a
        jr rqm_look
rqm_order:
        ld b, a
rqm_look:
        ld l, e
        ld h, #0x00
        add hl, hl
        ld de, #_QSQ
        add hl, de
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld d, h
        ld e, l
        ld l, b
        ld h, #0x00
        add hl, hl
        ld bc, #_QSQ
        add hl, bc
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld a, l
        sub a, e
        ld l, a
        ld a, h
        sbc a, d
        ld h, a
        ret
    __endasm;
}
#endif

static void refresh_quarter(uint8_t quarter) {
    uint8_t i;
    rq_index = quarter;
    rq_dst = world.heights + quarter;
    /* Entry index i has phase 8i: angle bytes advance by 32, 64 and 16 per step of four. */
    rq_a0 = (uint8_t)(quarter * 8 + rf_t1);
    rq_a1 = (uint8_t)(quarter * 16 + rf_t2);
    rq_a2 = (uint8_t)(quarter * 4 + rf_t1);
    rq_shifted = (uint16_t)(quarter * 8 + rf_shift) & 511;
#ifdef __SDCC
    rq_run();
#else
    for (rq_left = TABLE_LEN / 4; rq_left; rq_left--) {
        rq_delta = (uint8_t)ENV_AMP[rq_shifted];
        rq_sum = (int16_t)(HORIZON + ENV_LIFT[rq_shifted]);
        rq_sum = (int16_t)(rq_sum + (int16_t)(mul8u((uint8_t)rf_amp1[rq_delta], SIN8[rq_a0]) >> 2));
        rq_sum = (int16_t)(rq_sum + (int16_t)(mul8u((uint8_t)rf_amp2[rq_delta], SIN8[rq_a1]) >> 2));
        rq_sum = (int16_t)(rq_sum + (int16_t)(((int16_t)(6 * SIN8[rq_a2])) >> 2));
        *rq_dst = rq_sum;
        rq_dst += 4;
        rq_a0 = (uint8_t)(rq_a0 + 32);
        rq_a1 = (uint8_t)(rq_a1 + 64);
        rq_a2 = (uint8_t)(rq_a2 + 16);
        rq_shifted = (uint16_t)(rq_shifted + 32) & 511;
    }
#endif
    if(world.wave_scale<16) {
        for(i=quarter;i<TABLE_LEN;i+=4)
            world.heights[i]=(int16_t)(HORIZON+((world.heights[i]-HORIZON)>>4)*world.wave_scale);
    }
}

static void prepare(uint32_t tick, uint8_t tier) {
    rf_shift = (uint16_t)(tick >> 1) & 511;
    rf_t1 = (uint8_t)tick;
    rf_t2 = (uint8_t)(rf_t1 + rf_t1);
    if (tier > 8) tier = 8;
    rf_amp1 = AMP1_TAB + (uint16_t)tier * 256;
    rf_amp2 = AMP2_TAB + (uint16_t)tier * 256;
    front_num = front_scale(tier);
}

void sea_build(uint32_t tick, uint8_t tier) BANKED {
    uint8_t quarter;
    front_meter_valid=0;
    prepare(tick, tier);
    for (quarter = 0; quarter < 4; quarter++) refresh_quarter(quarter);
}

void sea_refresh(uint32_t tick, uint8_t tier) BANKED {
#ifdef __SDCC
    uint8_t n=16,scale=world.wave_scale;
    int16_t *dest=world.heights+((uint8_t)tick&3);
    front_num=front_scale(tier);
    if(scale)wave_lookup((uint16_t)tick,tier);
    if(scale<16)do {
        *dest=scale?(int16_t)(HORIZON+((*dest-HORIZON)>>4)*scale):HORIZON;
        dest+=4;
    } while(--n);
#else
    prepare(tick, tier);
    refresh_quarter((uint8_t)tick & 3);
#endif
}

int16_t steer_scale(int16_t slope) BANKED {
    int16_t s = slope < 0 ? (int16_t)-slope : slope;
    if (s > SLOPE_MAX) s = SLOPE_MAX;
    return (int16_t)(256 - (s >> 1));
}

#ifndef __SDCC
/* d * frac for frac in 0..7 without a multiply. */
static int16_t mul_frac(int16_t d, uint8_t frac) {
    int16_t r = 0;
    int16_t d2 = (int16_t)(d + d);
    if (frac & 1) r = d;
    if (frac & 2) r = (int16_t)(r + d2);
    if (frac & 4) r = (int16_t)(r + d2 + d2);
    return r;
}
#endif

/* Height between entry `index` and the next with weight frac/8. */
#ifdef __SDCC
static uint8_t le_index, le_frac, wk_seek, wk_left;
static int8_t wk_dir;
static int16_t le_result, wk_prev;
/* Front-lane walk: fa_at is the sea-table pixel, fa_dir is ±4 (one 8px front step). */
static uint16_t fa_at;
static int8_t fa_dir;
static uint8_t fa_seek, fa_left;
static int16_t fa_prev;

/* le_core: HL = lerp of world.heights at (le_index, le_frac), exactly as the C below: the
 * difference times frac by shift-and-add, an arithmetic shift by 3, plus h0. Clobbers A B C D E.
 * wk_run: the walk loop, stepping le_index by wk_dir while the table keeps rising (wk_seek)
 * or falling, at most wk_left times; the last accepted height stays in wk_prev. */
static void le_run(void) __naked {
    __asm
        call le_core
        ld a, l
        ld (#_le_result), a
        ld a, h
        ld (#_le_result + 1), a
        ret
le_core:
        ld a, (#_le_index)
        and a, #0x3f
        add a, a
        ld l, a
        ld h, #0x00
        ld de, #(_world + 117)
        add hl, de
        ld a, (hl+)
        ld c, a
        ld b, (hl)
        ld a, (#_le_index)
        inc a
        and a, #0x3f
        add a, a
        ld l, a
        ld h, #0x00
        add hl, de
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld a, l
        sub a, c
        ld l, a
        ld a, h
        sbc a, b
        ld h, a
        push bc
        ld d, h
        ld e, l
        ld hl, #0x0000
        ld a, (#_le_frac)
        ld b, a
        bit 0, b
        jr Z, le_f1
        add hl, de
le_f1:
        sla e
        rl d
        bit 1, b
        jr Z, le_f2
        add hl, de
le_f2:
        sla e
        rl d
        bit 2, b
        jr Z, le_f3
        add hl, de
le_f3:
        sra h
        rr l
        sra h
        rr l
        sra h
        rr l
        pop de
        add hl, de
        ret
    __endasm;
}

static void wk_run(void) __naked {
    __asm
wk_loop:
        ld a, (#_le_index)
        ld hl, #_wk_dir
        add a, (hl)
        ld (#_le_index), a
        call le_core
        ld a, (#_wk_prev)
        ld e, a
        ld a, (#_wk_prev + 1)
        ld d, a
        ld a, (#_wk_seek)
        or a
        jr Z, wk_desc
        ld a, l
        sub a, e
        ld a, h
        sbc a, d
        bit 7, a
        ret Z
        jr wk_cont
wk_desc:
        ld a, e
        sub a, l
        ld a, d
        sbc a, h
        bit 7, a
        ret Z
wk_cont:
        ld a, l
        ld (#_wk_prev), a
        ld a, h
        ld (#_wk_prev + 1), a
        ld hl, #_wk_left
        dec (hl)
        jr NZ, wk_loop
        ret
    __endasm;
}

/* 112*64 and 140*64. The walker bakes these in; the typedef fails the build if they move. */
typedef char fa_horizon_ok[(HORIZON == 7168 && FRONT_HORIZON == 8960) ? 1 : -1];

/* Lower crests keep full height, troughs use a quarter of the swing. Reduce
 * precision by two fractional bits before multiplying to avoid signed overflow.
 * front_num is 12..16: multiply by 12, add at most four copies. */
static void fa_raw_height(void) __naked {
    __asm
        ld hl, #_fa_at
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld a, h
        and a, #0x01
        ld h, a
        ld a, l
        and a, #0x07
        ld (#_le_frac), a
        srl h
        rr l
        srl h
        rr l
        srl h
        rr l
        ld a, l
        ld (#_le_index), a
        call le_core
        ret
    __endasm;
}

static void fa_scale_height(void) __naked {
    __asm
        ld de, #0x1c00
        ld a, l
        sub a, e
        ld l, a
        ld a, h
        sbc a, d
        ld h, a
        bit 7, h
        jr NZ, fa_crest
        sra h
        rr l
        sra h
        rr l
fa_crest:
        sra h
        rr l
        sra h
        rr l
        ld d, h
        ld e, l
        add hl, hl
        add hl, de
        add hl, hl
        add hl, hl
        ld a, (#_front_num)
        sub a, #12
        jr Z, fa_scaled
fa_more:
        add hl, de
        dec a
        jr NZ, fa_more
fa_scaled:
        sra h
        rr l
        sra h
        rr l
        ; Limit the very deepest troughs to y150 (feet remain above the HUD).
        bit 7, h
        jr NZ, fa_add
        ld a, h
        cp a, #2
        jr C, fa_add
        jr NZ, fa_limit
        ld a, l
        cp a, #129
        jr C, fa_add
fa_limit:
        ld hl, #640
fa_add:
        ld de, #0x2300
        add hl, de
        ret
    __endasm;
}

static void fa_height(void) __naked {
    __asm
        call _fa_raw_height
        jp _fa_scale_height
    __endasm;
}

/* Crest searches compare raw table samples: shaping is monotone, so only the
 * final crest/trough needs scaling instead of every entry in both walks. */
static void fa_raw_sample(void) __naked {
    __asm
        call _fa_raw_height
        ld a, l
        ld (#_fa_prev), a
        ld a, h
        ld (#_fa_prev + 1), a
        ret
    __endasm;
}

static void fa_scaled_sample(void) __naked {
    __asm
        ld a, (#_fa_prev)
        ld l, a
        ld a, (#_fa_prev + 1)
        ld h, a
        call _fa_scale_height
        ld a, l
        ld (#_fa_prev), a
        ld a, h
        ld (#_fa_prev + 1), a
        ret
    __endasm;
}

/* fa_prev = bottom-lane height at sea pixel fa_at, through fa_height. */
static void fa_sample(void) __naked {
    __asm
        call _fa_height
        ld a, l
        ld (#_fa_prev), a
        ld a, h
        ld (#_fa_prev + 1), a
        ret
    __endasm;
}

/* The walk from fa_at, whose height the caller already has in fa_prev. */
static void fa_walk(void) __naked {
    __asm
        ld a, #20
        ld (#_fa_left), a
fa_loop:
        ld hl, #_fa_at
        ld a, (hl)
        ld b, a
        inc hl
        ld c, (hl)
        ld a, (#_fa_dir)
        ld e, a
        rlca
        sbc a, a
        ld d, a
        ld l, b
        ld h, c
        add hl, de
        ld a, l
        ld (#_fa_at), a
        ld a, h
        ld (#_fa_at + 1), a
        call _fa_raw_height
        ld a, (#_fa_prev)
        ld e, a
        ld a, (#_fa_prev + 1)
        ld d, a
        ld a, (#_fa_seek)
        or a
        jr Z, fa_desc
        ld a, l
        sub a, e
        ld a, h
        sbc a, d
        bit 7, a
        ret Z
        jr fa_keep
fa_desc:
        ld a, e
        sub a, l
        ld a, d
        sbc a, h
        bit 7, a
        ret Z
fa_keep:
        ld a, l
        ld (#_fa_prev), a
        ld a, h
        ld (#_fa_prev + 1), a
        ld hl, #_fa_left
        dec (hl)
        jr NZ, fa_loop
        ret
    __endasm;
}

static int16_t lerp_entry(uint8_t index, uint8_t frac) {
    le_index = index;
    le_frac = frac;
    le_run();
    return le_result;
}
#else
static int16_t lerp_entry(uint8_t index, uint8_t frac) {
    int16_t h0 = world.heights[index & (TABLE_LEN - 1)];
    int16_t h1 = world.heights[(index + 1) & (TABLE_LEN - 1)];
    return (int16_t)(h0 + (int16_t)(mul_frac((int16_t)(h1 - h0), frac) >> 3));
}
#endif

int16_t sea_at_px(int16_t world_px) BANKED {
    uint16_t phase = (uint16_t)(world_px & 511);
    return lerp_entry((uint8_t)(phase >> 3), (uint8_t)(phase & 7));
}

int16_t sea_at(int32_t world_x) BANKED { return sea_at_px((int16_t)((world_x >> 6) & 511)); }

void sea_sample(int32_t world_x, Samples *s) BANKED {
    int16_t px = (int16_t)((world_x >> 6) & 511);
    uint8_t index = (uint8_t)(((uint16_t)px) >> 3);
    uint8_t frac = (uint8_t)(px & 7);
    s->px = px;
    s->left = lerp_entry((uint8_t)(index - 1), frac);
    s->here = lerp_entry(index, frac);
    s->right = lerp_entry((uint8_t)(index + 1), frac);
}

static uint8_t crest_of(const Samples *s) { return s->here < s->left && s->here < s->right; }
static uint8_t trough_of(const Samples *s) { return s->here > s->left && s->here > s->right; }
static int16_t slope_of(const Samples *s) { return (int16_t)(s->right - s->left); }

int16_t sea_slope(int32_t world_x) BANKED {
    Samples s;
    sea_sample(world_x, &s);
    return slope_of(&s);
}

uint8_t sea_is_crest(int32_t world_x) BANKED {
    Samples s;
    sea_sample(world_x, &s);
    return crest_of(&s);
}

uint8_t sea_is_trough(int32_t world_x) BANKED {
    Samples s;
    sea_sample(world_x, &s);
    return trough_of(&s);
}

static uint8_t jump_allowed_of(const Samples *s) {
    if (trough_of(s)) return 0;
    return crest_of(s) || slope_of(s) < 0;
}

uint8_t sea_jump_allowed_s(const Samples *s) BANKED { return jump_allowed_of(s); }
int16_t sea_slope_s(const Samples *s) BANKED { return slope_of(s); }

/* Follows the table in 8 px steps while it keeps rising (seek_crest) or falling. Every step
 * shares the interpolation weight of the start, so only the table index moves. */
#ifdef __SDCC
static int16_t walk(const Samples *s, int8_t dir, uint8_t seek_crest) {
    wk_prev = s->here;
    le_index = (uint8_t)(((uint16_t)s->px) >> 3);
    le_frac = (uint8_t)(s->px & 7);
    wk_dir = dir;
    wk_seek = seek_crest;
    wk_left = 20;
    wk_run();
    return wk_prev;
}
#else
static int16_t walk(const Samples *s, int8_t dir, uint8_t seek_crest) {
    int16_t prev = s->here;
    uint8_t index = (uint8_t)(((uint16_t)s->px) >> 3);
    uint8_t frac = (uint8_t)(s->px & 7);
    uint8_t step;
    for (step = 0; step < 20; step++) {
        int16_t next;
        index = (uint8_t)(index + dir);
        next = lerp_entry(index, frac);
        if (seek_crest ? !(next < prev) : !(next > prev)) break;
        prev = next;
    }
    return prev;
}
#endif

/* part / whole in Q8, clamped to 0..=256, in 16-bit unsigned math. */
static int16_t fraction(int16_t part, int16_t whole) {
    uint16_t p, w, q;
    if (whole <= 0 || part <= 0) return whole <= 0 ? 256 : 0;
    p = (uint16_t)(part >> 5);
    w = (uint16_t)(whole >> 5);
    if (w == 0) return 256;
    q = (uint16_t)((uint16_t)(p << 8) / w);
    return q > 256 ? 256 : (int16_t)q;
}

static int16_t climb_progress_of(const Samples *s) {
    int16_t crest = walk(s, 1, 1);
    int16_t trough = walk(s, -1, 0);
    return fraction((int16_t)(trough - s->here), (int16_t)(trough - crest));
}

static int16_t descent_progress_of(const Samples *s) {
    int16_t crest = walk(s, -1, 1);
    int16_t trough = walk(s, 1, 0);
    return fraction((int16_t)(s->here - crest), (int16_t)(trough - crest));
}

uint8_t sea_in_top_fifth_s(const Samples *s) BANKED {
    if (crest_of(s)) return 1;
    return slope_of(s) < 0 && climb_progress_of(s) >= 204;
}

int8_t sea_jump_meter_s(const Samples *s) BANKED {
    if (jump_allowed_of(s)) {
        /* in_top_fifth and the bar position share one climb_progress walk. */
        uint8_t top_fifth = crest_of(s);
        int16_t climb = 0;
        if (top_fifth) {
            climb = climb_progress_of(s);
        } else if (slope_of(s) < 0) {
            climb = climb_progress_of(s);
            top_fifth = climb >= 204;
        }
        if (top_fifth) {
            if (climb < 204) climb = 204;
            return (int8_t)METER_TOP[climb - 204];
        }
        return (int8_t)METER_CLIMB[climb];
    } else if (trough_of(s) || slope_of(s) < 0) {
        return 0;
    } else {
        return (int8_t)METER_DESCENT[descent_progress_of(s)];
    }
}

/* Bottom lane: the height table, twice as long, with stronger crests and shallow troughs. The scale is a
 * shift-add in fa_height so the jump-meter walk stays in this bank; a banked sea_at_px plus a
 * 32-bit multiply per sample pushed one grounded frame past the two-vblank budget. */
static int16_t front_at_px(int32_t world_px) {
#ifdef __SDCC
    /* fa_height keeps nine bits of the pixel, so the low half of world_px is all it needs. */
    fa_at = (uint16_t)(((int16_t)world_px >> 1) + FRONT_PHASE);
    fa_sample();
    return fa_prev;
#else
    int16_t sample = (int16_t)((world_px >> 1) + FRONT_PHASE);
    uint16_t phase = (uint16_t)(sample & 511);
    int16_t delta = (int16_t)(lerp_entry((uint8_t)(phase >> 3), (uint8_t)(phase & 7)) - HORIZON);
    if(delta>0)delta>>=2;
    delta=(int16_t)(((delta>>2)*front_num)>>2);
    if(delta>PX(10))delta=PX(10);
    return (int16_t)(FRONT_HORIZON+delta);
#endif
}

int16_t front_at(int32_t world_x) BANKED { return front_at_px(world_x >> 6); }

static void front_sample(int32_t world_x, Samples *s) {
    int32_t px = world_x >> 6;
    s->px = (int16_t)px;
#ifdef __SDCC
    /* Neighbours 8 world px apart are 4 table px apart; nine bits of the pixel suffice. */
    fa_at = (uint16_t)(((int16_t)px >> 1) + FRONT_PHASE - 4);
    fa_sample();
    s->left = fa_prev;
    fa_at += 4;
    fa_sample();
    s->here = fa_prev;
    fa_at += 4;
    fa_sample();
    s->right = fa_prev;
#else
    s->left = front_at_px(px - 8);
    s->here = front_at_px(px);
    s->right = front_at_px(px + 8);
#endif
}

int16_t front_slope(int32_t world_x) BANKED {
    Samples s;
    front_sample(world_x, &s);
    return (int16_t)(s.right - s.left);
}

/* `here` is the lane height at world_x, which the caller has sampled already. */
static int16_t front_walk(int32_t world_x, int16_t here, int8_t dir, uint8_t seek_crest) {
    (void)here;
#ifdef __SDCC
    int16_t px = (int16_t)(world_x >> 6);
    fa_at = (uint16_t)((px >> 1) + FRONT_PHASE);
    fa_raw_sample();
    fa_dir = (int8_t)(dir * 4);
    fa_seek = seek_crest;
    fa_walk();
    fa_scaled_sample();
    return fa_prev;
#else
    int16_t at = (int16_t)((world_x >> 7) + FRONT_PHASE);
    int16_t prev = sea_at_px(at);
    int16_t step_px = (int16_t)(dir * 4);
    uint8_t step;
    for (step = 0; step < 20; step++) {
        int16_t next;
        at += step_px;
        next = sea_at_px(at);
        if (seek_crest ? next >= prev : next <= prev) break;
        prev = next;
    }
    prev-=HORIZON;
    if(prev>0)prev>>=2;
    prev=(int16_t)(((prev>>2)*front_num)>>2);
    if(prev>PX(10))prev=PX(10);
    return (int16_t)(FRONT_HORIZON+prev);
#endif
}

static int16_t front_climb(int32_t world_x, int16_t here) {
    int16_t crest = front_walk(world_x, here, 1, 1);
    int16_t trough = front_walk(world_x, here, -1, 0);
    return fraction((int16_t)(trough - here), (int16_t)(trough - crest));
}

static int16_t front_descent(int32_t world_x, int16_t here) {
    int16_t crest = front_walk(world_x, here, -1, 1);
    int16_t trough = front_walk(world_x, here, 1, 0);
    return fraction((int16_t)(here - crest), (int16_t)(trough - crest));
}

/* Same bar as the top lane, from one sample. The tables replace the divisions. */
static int8_t front_meter_of(int32_t world_x, const Samples *s) {
    int16_t slope = slope_of(s);
    if (trough_of(s)) return 0;
    if (crest_of(s) || slope < 0) {
        int16_t top = front_climb(world_x, s->here);
        if (crest_of(s) || top >= 204) {
            if (top < 204) top = 204;
            return (int8_t)METER_TOP[top - 204];
        }
        return (int8_t)METER_CLIMB[top];
    }
    return (int8_t)METER_DESCENT[front_descent(world_x, s->here)];
}

uint8_t front_jump_allowed(int32_t world_x) BANKED {
    Samples s;
    front_sample(world_x, &s);
    if (trough_of(&s)) return 0;
    return crest_of(&s) || slope_of(&s) < 0;
}

uint8_t front_in_top_fifth(int32_t world_x) BANKED {
    Samples s;
    front_sample(world_x, &s);
    if (crest_of(&s)) return 1;
    return slope_of(&s) < 0 && front_climb(world_x, s.here) >= 204;
}

int8_t front_jump_meter(int32_t world_x) BANKED {
    Samples s;
    front_sample(world_x, &s);
    return front_meter_of(world_x, &s);
}

/* Grounded bottom lane: height, slope, jump permission and the meter from one sample. */
void front_ride(int32_t world_x, FrontRide *out) BANKED {
    Samples s;
    front_sample(world_x, &s);
    out->surface = s.here;
    out->slope = slope_of(&s);
    out->allowed = (uint8_t)(!trough_of(&s) && (crest_of(&s) || slope_of(&s) < 0));
    /* The visible meter already glides toward its target. Refresh that costly
     * crest/trough search at 15Hz; contact, slope, permission and actual launch
     * timing remain exact at 60Hz. The NPC never pays for a display meter. */
    if(!front_meter_valid || (uint8_t)((uint8_t)world.tick-front_meter_tick)>=4) {
        front_meter_target=front_meter_of(world_x,&s);
        front_meter_tick=(uint8_t)world.tick;front_meter_valid=1;
    }
    out->meter=front_meter_target;
}
/* Identical physical samples/permission without the player's display-only meter walk. */
void front_motion(int32_t world_x,FrontRide *out) BANKED {
    Samples s;
    front_sample(world_x,&s);
    out->surface=s.here;out->slope=slope_of(&s);
    out->allowed=(uint8_t)(!trough_of(&s) && (crest_of(&s) || slope_of(&s)<0));
    out->meter=0;
}

int16_t lane_surface(int32_t world_x, uint8_t lane) BANKED {
    return lane == LANE_BOTTOM ? front_at(world_x) : sea_at(world_x);
}

int16_t lane_slope(int32_t world_x, uint8_t lane) BANKED {
    return lane == LANE_BOTTOM ? front_slope(world_x) : sea_slope(world_x);
}
