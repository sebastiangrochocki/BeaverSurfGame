/* Beaver Surf rules, ported line for line from sim/src/lib.rs and sim/src/wave.rs.
 *
 * Arithmetic contract (shared with the Rust crate):
 * - Positions, heights and screen coordinates are int16_t in Q10.6: 64 is one pixel.
 * - Velocities and the surf speed are int16_t in Q8.8: 256 is one pixel per frame.
 *   A position advances by velocity >> 2.
 * - Any product that can exceed 16 bits is written with an explicit (int32_t) widening.
 * - Division truncates toward zero; >> on a signed value is an arithmetic shift.
 * - Only tick, scroll, score and best are 32-bit.
 *
 * Plain C99 so the same files build with clang on the host (gbc/test) and SDCC for the
 * Game Boy Color (gbc/Makefile). SDCC's int is 16 bits, so every type is spelled out.
 */
#ifndef BEAVER_RULES_H
#define BEAVER_RULES_H

#ifdef __SDCC
#define BANKED __banked
#else
#define BANKED
#endif

#include <stdint.h>

#define FP 64
#define VEL 256
#define SCREEN_W 240
#define SCREEN_H 160
#define PX(n) ((int16_t)((n) * FP))

#define LANE_MIN (20 * FP)
#define LANE_MAX (212 * FP)
#define BEAVER_X0 (64 * FP)

#define SCROLL_SPEED VEL
#define MIN_SPEED (3 * VEL / 4)
#define MAX_SPEED (3 * VEL)
#define SLOPE_ACCEL_MAX 8
#define PUMP_ACCEL 6

#define STEER_ACCEL (VEL / 6)
#define STEER_MAX (5 * VEL / 2)

#define GRAVITY 51
#define DIVE_GRAVITY 154
#define JUMP_VY (-(VEL * 4))
#define EARLY_JUMP_VY (-(VEL * 2))
#define RIGHT_TUG_VY (VEL / 2)
#define MAX_JUMP_VY (-(VEL * 7))

#define METER_LEN 96
#define METER_GREEN_START 32
#define METER_GREEN_END 64
#define METER_RATE 3

#define FLIP_FRAMES 26
#define FLIP_POINTS 20
#define FLIP_SPEED (VEL / 2)
#define AIR_FRAMES_PER_POINT 3
#define WIPEOUT_FRAMES 64
#define HEART_EVERY_RINGS 3
#define HEART_GAP (44 * FP)
#define BANNER_FRAMES 50
#define FLOW_COMBO 3

#define MAX_COMBO 4
#define COMBO_FRAMES 150
#define SPLASH_FRAMES 21

#define START_HEARTS 6
#define INVULN_FRAMES 45
#define RING_POINTS 50
#define GULL_AIR_SPEED FP
/* Gull lane band (see beaver_sim GULL_Y_MIN/MAX) and the glide step in eighths of a pixel. */
#define GULL_Y_MIN PX(48)
#define GULL_Y_MAX PX(80)
#define GULL_LANES 17
#define GULL_GLIDE (FP / 8)
#define GULL_STEEP 2
#define GULL_ZIG 4
#define GULL_ZIG_TICKS 14
#define GULL_EDGE_LANES 5
#define GULL_PAIR_TIER 2
#define GULL_PAIR_GAP PX(36)
#define RNG_SEED 0xACE1u
#define RING_LIFT (40 * FP)
#define GULL_SLOTS 4
#define RING_SLOTS 3

/* Wave. */
#define HORIZON (112 * FP)
#define FRONT_HORIZON (140 * FP)
#define WAVE_PERIOD_PX 512
#define TABLE_LEN 64
#define SLOPE_MAX (4 * FP)

/* Input bits, matching beaver_sim::Input::to_bits. */
#define INPUT_LEFT 1
#define INPUT_RIGHT 2
#define INPUT_JUMP 4
#define INPUT_DIVE 8
#define INPUT_START 16

enum { PHASE_TITLE = 0, PHASE_PLAY = 1, PHASE_OVER = 2 };
enum { BANNER_NONE = 0, BANNER_TRICK = 1, BANNER_WIPEOUT = 2, BANNER_HEART = 3 };

typedef struct {
    int16_t x;
    int16_t y;
} Actor;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t airborne;
    uint8_t flip;
    uint8_t flips;
    uint8_t perfect;
    uint16_t air_time;
    uint8_t rings;
} Beaver;

typedef struct {
    uint8_t present;
    int16_t x;
    int16_t y;
    uint8_t age;
    uint8_t big;
} Splash;

typedef struct {
    uint32_t tick;
    int32_t scroll;
    int16_t speed;
    Beaver beaver;
    uint8_t gull_present[GULL_SLOTS];
    Actor gulls[GULL_SLOTS];
    int16_t gull_vy[GULL_SLOTS];
    uint8_t gull_turn[GULL_SLOTS];
    uint8_t ring_present[RING_SLOTS];
    Actor rings[RING_SLOTS];
    uint8_t heart_present;
    Actor heart;
    uint16_t ring_count;
    Splash splash;
    uint8_t combo;
    uint16_t combo_timer;
    int8_t meter;
    uint8_t stun;
    uint8_t banner;
    uint16_t banner_points;
    uint8_t banner_mult;
    uint8_t banner_air;
    uint8_t banner_timer;
    uint8_t hearts;
    uint32_t score;
    uint32_t best;
    uint8_t phase;
    uint8_t invuln;
    uint16_t gull_timer;
    uint16_t ring_timer;
    uint16_t rng;
    /* Main wave height every 8 px over the 512 px period; a quarter is refreshed per tick. */
    int16_t heights[TABLE_LEN];
    uint8_t start_held;
    uint8_t jump_held;
    /* Difficulty tier (tick / 900, at most 8) kept incrementally to avoid a 32-bit divide. */
    uint8_t tier;
    uint16_t tier_ticks;
} World;

typedef struct {
    uint8_t jumped;
    uint8_t landed;
    uint8_t clean_landing;
    uint8_t trick;
    uint8_t wipeout;
    uint8_t hit;
    uint8_t ring;
    uint8_t heart;
    uint8_t died;
} Events;

/* Size of the state record written by world_encode; matches beaver_sim::trace::STATE_LEN. */
#define STATE_LEN 116

/* The one world. A global so SDCC addresses every field directly; pointer-relative access
 * costs the SM83 several times more. */
extern World world;

/* rules.c */
void world_new(void) BANKED;
void world_step(uint8_t input, Events *events) BANKED;
int32_t world_beaver_x(void) BANKED;
uint8_t world_wave_tier(void) BANKED;
int16_t world_max_speed(void) BANKED;
void world_encode(uint8_t *out) BANKED;

/* wave.c: all sampling reads world.heights. */
/* Heights 8 px left of, under, and 8 px right of a position, plus its pixel phase. */
typedef struct {
    int16_t px;
    int16_t left;
    int16_t here;
    int16_t right;
} Samples;

uint8_t wave_tier(uint32_t tick) BANKED;
void sea_build(uint32_t tick, uint8_t tier) BANKED;
void sea_refresh(uint32_t tick, uint8_t tier) BANKED;
int16_t sea_at_px(int16_t world_px) BANKED;
int16_t sea_at(int32_t world_x) BANKED;
void sea_sample(int32_t world_x, Samples *s) BANKED;
uint8_t sea_jump_allowed_s(const Samples *s) BANKED;
int16_t sea_slope_s(const Samples *s) BANKED;
uint8_t sea_in_top_fifth_s(const Samples *s) BANKED;
int8_t sea_jump_meter_s(const Samples *s) BANKED;
int16_t sea_slope(int32_t world_x) BANKED;
uint8_t sea_is_crest(int32_t world_x) BANKED;
uint8_t sea_is_trough(int32_t world_x) BANKED;
int16_t front_height(uint16_t phase_px, uint32_t tick, uint8_t tier) BANKED;
int16_t steer_scale(int16_t slope) BANKED;

/* save.c */
#define SAVE_LEN 8
void save_encode(uint32_t best, uint8_t *out);
uint8_t save_decode(const uint8_t *in, uint32_t *best);

#endif
