/* Surfing rules. Every function mirrors the method of the same name in sim/src/lib.rs.
 * The world is a global (see rules.h); `W` keeps the code readable.
 *
 * On the Game Boy this file lives in ROM bank 1 so the fixed bank stays below 16 KB. */
#ifdef __SDCC
#pragma bank 1
#endif
#include "rules.h"
#include "actors.h"
#include "shop.h"
#include "surf.h"
#include "race.h"

#include <string.h>

World world;
uint32_t course_length;
static uint8_t rival_stepping;
#define W world

static int16_t clamp16(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int16_t abs16(int16_t v) { return v < 0 ? (int16_t)-v : v; }
static uint8_t sat_add8(uint8_t a, uint8_t b) { return (uint8_t)(a + b) < a ? 255 : (uint8_t)(a + b); }
static uint16_t sat_add16(uint16_t a, uint16_t b) {
    return (uint16_t)(a + b) < a ? 65535 : (uint16_t)(a + b);
}
static uint32_t sat_add32(uint32_t a, uint32_t b) {
    return (uint32_t)(a + b) < a ? 0xFFFFFFFFUL : (uint32_t)(a + b);
}

/* v / 4 truncating toward zero, like C and Rust division, without the division routine. */
static int16_t div4(int16_t v) {
    int16_t q = (int16_t)(v >> 2);
    if (v < 0 && (v & 3)) q++;
    return q;
}

/* v / d truncating toward zero for small |v / d|, by repeated subtraction. */
static int16_t div_small(int16_t v, int16_t d) {
    int16_t q = 0;
    if (v >= 0) {
        while (v >= d) {
            v = (int16_t)(v - d);
            q++;
        }
    } else {
        while (v <= (int16_t)-d) {
            v = (int16_t)(v + d);
            q--;
        }
    }
    return q;
}

/* (face / 10).clamp(-8, 8) */
static int16_t slope_accel(int16_t face) {
    if (face >= 80) return SLOPE_ACCEL_MAX;
    if (face <= -80) return -SLOPE_ACCEL_MAX;
    return div_small(face, 10);
}

static uint16_t gull_interval(uint8_t tier) {
    uint16_t v = (uint16_t)(300 - (uint16_t)tier * 16);
    return v < 160 ? 160 : v;
}

static uint16_t ring_interval(uint8_t tier) {
    uint16_t v = (uint16_t)(220 - (uint16_t)tier * 10);
    return v < 120 ? 120 : v;
}

int32_t world_beaver_x(void) BANKED { return W.scroll + (int32_t)W.beaver.x; }

uint8_t world_wave_tier(void) BANKED { return W.phase == PHASE_TITLE ? 0 : W.tier; }

int16_t world_max_speed(void) BANKED { return (int16_t)(MAX_SPEED + (int16_t)W.combo * (VEL / 3)); }

/* Advances tick and keeps tier equal to min(tick / 900, 8). */
static void advance_tick(void) {
    W.tick++;
    if (++W.tier_ticks == 900) {
        W.tier_ticks = 0;
        if (W.tier < 8) W.tier++;
    }
}

void world_new(void) BANKED {
    memset(&W, 0, sizeof(World));
    W.speed = SCROLL_SPEED;
    W.beaver.x = BEAVER_X0;
    W.beaver.y = HORIZON;
    W.meter = -1;
    W.hearts = START_HEARTS;
    W.phase = PHASE_TITLE;
    W.stage = 1;
    course_length = COURSE_STAGE_PX(1) * FP;
    W.wave_scale = 16;
    W.gull_timer = gull_interval(0);
    W.ring_timer = ring_interval(0);
    W.rng = RNG_SEED;
    surf_reset();
    race_reset();
    sea_build(0, 0);
    W.beaver.y = sea_at(world_beaver_x());
}

static void start_stage(uint8_t stage) {
    surf_reset();
    race_reset();
    W.phase = PHASE_BEACH_START;
    W.stage = stage;
    course_length = COURSE_STAGE_PX(stage) * FP;
    W.beach_timer = 0;
    W.course_distance = 0;
    W.scroll = 0;
    W.beach_pan = 0;
    W.wave_scale = 0;
    W.facing_left = 0;
    W.stun = W.invuln = W.combo = W.banner = W.splash.present = 0;
    memset(W.gull_present,0,sizeof W.gull_present);
    memset(W.ring_present,0,sizeof W.ring_present);
    W.heart_present=0;
    W.gull_timer=gull_interval(stage>8?8:stage-1);
    W.ring_timer=ring_interval(stage>8?8:stage-1);
    W.beaver.x = BEACH_START_X;
    W.beaver.y = HORIZON;
    W.beaver.vx = 0;
    W.beaver.vy = 0;
    shop_direction=0;
    W.beaver.airborne = 0;
    W.beaver.lane = LANE_TOP;
    W.beaver.lane_to = LANE_TOP;
    W.speed = SCROLL_SPEED;
    W.tier = stage > 8 ? 8 : (uint8_t)(stage - 1);
    W.tier_ticks = 0;
    sea_build(W.tick, W.tier);
}

static void beach_step(uint8_t ending, uint8_t input) {
    W.tick++;
    W.beaver.vx=0;
    W.beaver.vy=0;
    if ((input & INPUT_RIGHT) && !(input & INPUT_LEFT)) {
        W.facing_left=0;
        shop_direction=0;
        if(!race_beach_blocked((int16_t)(W.beaver.x+W.beach_pan+FP),W.beaver.y)) {
            W.beaver.vx=VEL;
            if(W.beaver.x<BEACH_CAMERA_X) W.beaver.x+=FP;
            else {W.beach_pan+=FP;W.visual_scroll+=FP;}
        }
    } else if ((input & INPUT_LEFT) && !(input & INPUT_RIGHT)) {
        W.facing_left=1;
        shop_direction=0;
        if(!race_beach_blocked((int16_t)(W.beaver.x+W.beach_pan-FP),W.beaver.y)) {
            if(W.beach_pan) {W.beach_pan-=FP;W.visual_scroll-=FP;W.beaver.vx=-VEL;}
            else if(W.beaver.x>PX(24)) {W.beaver.x-=FP;W.beaver.vx=-VEL;}
        }
    }
    if ((input & INPUT_UP) && !(input & INPUT_DOWN)) {
        if(W.beaver.y>PX(104) && !race_beach_blocked((int16_t)(W.beaver.x+W.beach_pan),W.beaver.y-FP)) {W.beaver.y-=FP;W.beaver.vy=-VEL;}
        shop_direction=2;
    } else if ((input & INPUT_DOWN) && !(input & INPUT_UP)) {
        if(W.beaver.y<PX(144) && !race_beach_blocked((int16_t)(W.beaver.x+W.beach_pan),W.beaver.y+FP)) {W.beaver.y+=FP;W.beaver.vy=VEL;}
        shop_direction=1;
    }
    sea_refresh(W.tick, W.tier);
    if(shop_beach(input)) return;
    if (!ending && (uint16_t)W.beaver.x+W.beach_pan >= BEACH_WATER_X) {
        W.phase = PHASE_PLAY;
        W.beach_timer = 0;
        W.course_distance = 0;
        W.beaver.vx = 0;
        W.beaver.vy = 0;
        W.beaver.lane = W.beaver.y>=PX(126)?LANE_BOTTOM:LANE_TOP;
        W.beaver.lane_to = W.beaver.lane;
        W.beaver.y = lane_surface(world_beaver_x(),W.beaver.lane);
        W.speed = (int16_t)(SCROLL_SPEED + (int16_t)(W.stage - 1) * 12);
        race_begin_surf();
    } else if (ending && W.beach_pan >= BEACH_EXIT_X) {
        uint8_t next = W.stage == 255 ? 255 : (uint8_t)(W.stage + 1);
        start_stage(next);
    }
}

static void show(uint8_t banner, uint16_t points, uint8_t mult, uint8_t air) {
    W.banner = banner;
    W.banner_points = points;
    W.banner_mult = mult;
    W.banner_air = air;
    W.banner_timer = BANNER_FRAMES;
}

static void lose_half_heart(Events *events) {
    if(RACING) {W.invuln=INVULN_FRAMES;return;}
    if (W.hearts > 0) W.hearts--;
    if (W.invuln < INVULN_FRAMES) W.invuln = INVULN_FRAMES;
    if (W.hearts == 0) {
        W.phase = PHASE_OVER;
        events->died = 1;
        if (W.score > W.best) W.best = W.score;
    }
}

static void glide_meter(int8_t target) {
    if (W.meter < 0) {
        W.meter = target;
    } else {
        int16_t delta = (int16_t)(target - W.meter);
        if (delta < -METER_RATE) delta = -METER_RATE;
        if (delta > METER_RATE) delta = METER_RATE;
        W.meter = (int8_t)(W.meter + delta);
    }
}

/* Screen-space swimming with momentum: gradual to build, quick to reverse. */
static void steer(uint8_t input) {
    int16_t dir = 0;
    uint8_t left = (input & INPUT_LEFT) != 0;
    uint8_t right = (input & INPUT_RIGHT) != 0;
    if (left && !right) dir = -1;
    if (right && !left) dir = 1;
    if (dir == 0) {
        W.beaver.vx = div4((int16_t)(W.beaver.vx * 3));
        if (abs16(W.beaver.vx) < VEL / 16) W.beaver.vx = 0;
    } else {
        int16_t scale = W.beaver.airborne ? 160 : steer_scale(lane_slope(world_beaver_x(), W.beaver.lane));
        int16_t accel;
        if ((dir < 0) ? (W.beaver.vx > 0) : (W.beaver.vx < 0)) {
            accel = STEER_ACCEL * 3;
        } else {
            /* scale is at most 256, so the product stays inside 16 bits. */
            accel = (int16_t)(((int16_t)(STEER_ACCEL * scale)) >> 8);
        }
        W.beaver.vx = clamp16((int16_t)(W.beaver.vx + (dir < 0 ? (int16_t)-accel : accel)),
                              -STEER_MAX, STEER_MAX);
    }
    W.beaver.x = (int16_t)(W.beaver.x + (W.beaver.vx >> 2));
    if (W.beaver.x <= LANE_MIN || W.beaver.x >= LANE_MAX) {
        W.beaver.x = clamp16(W.beaver.x, LANE_MIN, LANE_MAX);
        W.beaver.vx = 0;
    }
}

/* Downhill water adds surf speed and uphill water takes it away. */
static void ride(int16_t face, uint8_t input) {
    int16_t accel = slope_accel(face);
    uint8_t right = (input & INPUT_RIGHT) != 0;
    uint8_t dive = (input & INPUT_DIVE) != 0;
    /* accel / 2 toward zero; accel is negative here. */
    if (face < 0 && right) accel = (int16_t)-((int16_t)(-accel) >> 1);
    if (face > 0 && (right || dive)) accel = (int16_t)(accel + PUMP_ACCEL);
    W.speed = (int16_t)(W.speed + accel + div_small((int16_t)(SCROLL_SPEED - W.speed), 40));
    if (W.combo_timer > 0) {
        W.combo_timer--;
        if (W.combo_timer == 0) W.combo = 0;
    }
}

/* Take-off height grows with surf speed, so speed from the last ramp carries into the next. */
static void launch(int32_t world_x, uint8_t input) {
    int16_t excess = (int16_t)(W.speed - SCROLL_SPEED);
    int16_t tug = (input & INPUT_RIGHT) ? RIGHT_TUG_VY : 0;
    Samples s;
    if (excess < 0) excess = 0;
    if (W.beaver.lane == LANE_BOTTOM) {
        W.beaver.perfect = front_in_top_fifth(world_x);
    } else {
        sea_sample(world_x, &s);
        W.beaver.perfect = sea_in_top_fifth_s(&s);
    }
    W.beaver.lane_to = W.beaver.lane;
    W.beaver.cleared = 0;
    W.beaver.air_time = 0;
    W.beaver.rings = 0;
    if (W.beaver.perfect) {
        /* excess is non-negative, so shifts truncate like the divisions. */
        W.beaver.vy = (int16_t)(JUMP_VY - (int16_t)((int16_t)(excess * 3) >> 2) - tug);
        W.speed = (int16_t)(W.speed + VEL / 2);
    } else {
        W.beaver.vy = (int16_t)(EARLY_JUMP_VY - (int16_t)(excess >> 2) - (int16_t)(tug >> 1));
        W.speed = div4((int16_t)(W.speed * 3));
    }
    if (W.beaver.vy < MAX_JUMP_VY) W.beaver.vy = MAX_JUMP_VY;
    W.beaver.airborne = 1;
    W.beaver.y = (int16_t)(W.beaver.y + (W.beaver.vy >> 2));
}

static void splash_at(uint8_t big) {
    W.splash.present = 1;
    W.splash.x = W.beaver.x;
    W.splash.y = W.beaver.y;
    W.splash.age = 0;
    W.splash.big = big;
}

static void settle(int16_t surface, uint8_t lane) {
    W.beaver.y = surface;
    W.beaver.vy = 0;
    W.beaver.airborne = 0;
    W.beaver.lane = lane;
    W.beaver.lane_to = lane;
    W.beaver.cleared = 0;
}

/* Landing on a downhill face is a clean landing: a speed kick and a growing combo.
 * failed is a lane jump that never cleared the top wave. */
static void land(int16_t face, Events *events, uint8_t failed) {
    uint8_t flips = W.beaver.flips;
    uint8_t mid_flip = (uint8_t)(W.beaver.flip > 0 || failed);
    uint16_t air_points, base;
    uint8_t clean;
    events->landed = 1;
    W.beaver.flip = 0;
    W.beaver.flips = 0;
    if (mid_flip) {
        events->wipeout = 1;
        W.combo = 0;
        W.combo_timer = 0;
        W.speed = MIN_SPEED;
        W.stun = RACING?12:WIPEOUT_FRAMES;
        show(BANNER_WIPEOUT, 0, 0, 0);
        splash_at(1);
        lose_half_heart(events);
        if (!RACING && W.invuln < WIPEOUT_FRAMES + INVULN_FRAMES) W.invuln = WIPEOUT_FRAMES + INVULN_FRAMES;
        return;
    }
    air_points = W.beaver.perfect ? (uint16_t)(W.beaver.air_time / AIR_FRAMES_PER_POINT) : 0;
    base = (uint16_t)(air_points + FLIP_POINTS * (uint16_t)flips);
    if (base > 0) {
        uint8_t mult = (uint8_t)(1 + flips + (W.beaver.rings>3?3:W.beaver.rings));
        uint16_t points = (uint16_t)(base * (uint16_t)mult);
        events->trick = 1;
        W.speed = (int16_t)(W.speed + FLIP_SPEED * (int16_t)flips);
        W.score = sat_add32(W.score, points);
        show(BANNER_TRICK, points, mult, air_points > 0);
    }
    clean = face > FP / 2;
    if (clean) {
        W.combo = (uint8_t)(W.combo + 1);
        if (W.combo > MAX_COMBO) W.combo = MAX_COMBO;
        W.speed = (int16_t)(W.speed + VEL + (int16_t)W.combo * (VEL / 4));
        W.score = sat_add32(W.score, 10UL * W.combo);
        events->clean_landing = 1;
        if(!events->trick)show(BANNER_CLEAN,10u*W.combo,W.combo,0);
    } else if (face < -FP) {
        W.combo = 0;
        W.speed = (int16_t)(W.speed * 4 / 5);
    } else {
        W.speed = (int16_t)(W.speed + VEL / 6);
    }
    W.combo_timer = COMBO_FRAMES;
    splash_at(clean);
}

static void age_splash(int16_t scroll_step) {
    if (!W.splash.present) return;
    W.splash.age++;
    W.splash.x = (int16_t)(W.splash.x - scroll_step);
    if (W.splash.age >= SPLASH_FRAMES) W.splash.present = 0;
}

static uint8_t spawn(uint8_t *present, Actor *slots, uint8_t count, int16_t x, int16_t y) {
    uint8_t i;
    for (i = 0; i < count; i++) {
        if (!present[i]) {
            present[i] = 1;
            slots[i].x = x;
            slots[i].y = y;
            return i;
        }
    }
    return 0xFF;
}

/* Next pseudo-random value: xorshift16 after folding in the beaver's position. */
static uint16_t roll(void) {
    uint16_t x = (uint16_t)(W.rng ^ (uint16_t)W.beaver.x);
    if (x == 0) x = RNG_SEED;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    W.rng = x;
    return x;
}

static void spawn_gull(int16_t x, int8_t lane, int8_t glide, uint8_t turn);

static void move_actors(int16_t scroll_step) {
    uint8_t i;
    for (i = 0; i < GULL_SLOTS; i++) {
        if (!W.gull_present[i]) continue;
        W.gulls[i].x = (int16_t)(W.gulls[i].x - (scroll_step + GULL_AIR_SPEED));
        if (W.gulls[i].x < PX(-24)) {
            W.gull_present[i] = 0;
            W.gull_vy[i] = 0;
            W.gull_turn[i] = 0;
        } else if (W.gull_vy[i] != 0) {
            if (W.gull_turn[i]) {
                if (--W.gull_turn[i] == 0) {
                    W.gull_turn[i] = GULL_ZIG_TICKS;
                    W.gull_vy[i] = (int16_t)-W.gull_vy[i];
                }
            }
            W.gulls[i].y = (int16_t)(W.gulls[i].y + W.gull_vy[i]);
            if (W.gulls[i].y < GULL_Y_MIN) {
                W.gulls[i].y = GULL_Y_MIN;
                W.gull_vy[i] = (int16_t)-W.gull_vy[i];
            } else if (W.gulls[i].y > GULL_Y_MAX) {
                W.gulls[i].y = GULL_Y_MAX;
                W.gull_vy[i] = (int16_t)-W.gull_vy[i];
            }
        }
    }
    if (W.heart_present) {
        W.heart.x = (int16_t)(W.heart.x - scroll_step);
        if (W.heart.x < PX(-24)) {
            W.heart_present = 0;
            W.heart_lane = LANE_TOP;
        } else {
            W.heart.y = (int16_t)(lane_surface(W.scroll + (int32_t)W.heart.x, W.heart_lane) - RING_LIFT);
        }
    }

    if (W.gull_timer > 0) W.gull_timer--;
    if (W.gull_timer == 0) {
        W.gull_timer = gull_interval(W.tier);
        {
            uint16_t r = roll();
            int8_t lane = (int8_t)(r % GULL_LANES);
            int8_t sign = ((r >> 10) & 1) ? -1 : 1;
            int8_t glide = 0;
            uint8_t turn = 0;
            /* One flight in four is straight, one zig-zags across its lane, the rest glide
             * diagonally from the edge they fly away from. */
            switch ((r >> 12) & 3) {
            case 0: break;
            case 2:
                glide = (int8_t)(sign * GULL_ZIG);
                turn = GULL_ZIG_TICKS;
                break;
            default:
                glide = (int8_t)(sign * (int8_t)(GULL_STEEP + (int8_t)((r >> 8) % 3)));
                if (glide > 0) {
                    lane = (int8_t)(lane % GULL_EDGE_LANES);
                } else {
                    lane = (int8_t)(GULL_LANES - 1 - lane % GULL_EDGE_LANES);
                }
                break;
            }
            spawn_gull(PX(SCREEN_W + 16), lane, glide, turn);
            if (W.tier >= GULL_PAIR_TIER) {
                /* The partner mirrors the leader: other side of the band, opposite glide. */
                spawn_gull((int16_t)(PX(SCREEN_W + 16) + GULL_PAIR_GAP), (int8_t)(GULL_LANES - 1 - lane),
                           (int8_t)-glide, turn);
            }
        }
    }

    if (W.ring_timer > 0) W.ring_timer--;
    if (W.ring_timer == 0) {
        W.ring_timer = ring_interval(W.tier);
        W.ring_count++;
        if (W.hearts < START_HEARTS && W.ring_count % HEART_EVERY_RINGS == 0 && !W.heart_present) {
            W.heart_present=1; W.heart_lane=(uint8_t)(W.ring_count&1);
            W.heart.x=PX(264);
            W.heart.y=(int16_t)(lane_surface(W.scroll+W.heart.x,W.heart_lane)-RING_LIFT);
        }
    }
}

static uint8_t overlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah, int16_t bx, int16_t by,
                       int16_t bw, int16_t bh) {
    return ax < (int16_t)(bx + bw) && (int16_t)(ax + aw) > bx && ay < (int16_t)(by + bh) &&
           (int16_t)(ay + ah) > by;
}

static void spawn_gull(int16_t x, int8_t lane, int8_t glide, uint8_t turn) {
    uint8_t i;
    /* One 32px gull is already four hardware sprites. Keep a single flight in
     * the first GBC course so it cannot crowd the beaver, rings, and board. */
    for (i = 0; i < GULL_SLOTS; i++) {
        if (W.gull_present[i]) return;
    }
    for (i = 0; i < GULL_SLOTS; i++) {
        if (!W.gull_present[i]) {
            W.gull_present[i] = 1;
            W.gulls[i].x = x;
            W.gulls[i].y = (int16_t)(GULL_Y_MIN + (int16_t)lane * (2 * FP));
            W.gull_vy[i] = (int16_t)(glide * GULL_GLIDE);
            W.gull_turn[i] = turn;
            return;
        }
    }
}

static uint8_t touching_gull(uint8_t input) {
    uint8_t i;
    for (i = 0; i < GULL_SLOTS; i++) {
        if (!W.gull_present[i]) continue;
        if ((surf_gull_shown&(1u<<i)) && actor_touches(input,RACING?ACTOR_RACE_GULL:ACTOR_GULL,W.gulls[i].x,W.gulls[i].y))
            return 1;
    }
    return 0;
}

static uint8_t touching_heart(uint8_t input) {
    if (!W.heart_present) return 0;
    return actor_touches(input,ACTOR_HEART,W.heart.x,W.heart.y);
}

static void rider_step(uint8_t input,Events *events) {
    uint8_t jump_edge,dive_edge,down_edge,up_edge,stunned;
    uint8_t jump=(input&INPUT_JUMP)!=0,dive=(input&INPUT_DIVE)!=0;
    Samples s;
    int16_t surface=0,face=0,top;
    int32_t world_x;
    uint8_t lane;
    jump_edge = jump && !W.jump_held;
    W.jump_held = jump;
    dive_edge = (uint8_t)(dive && !W.dive_held);
    W.dive_held = dive;
    down_edge=(uint8_t)((input&INPUT_DOWN) && !surf_down_held);
    up_edge=(uint8_t)((input&INPUT_UP) && !surf_up_held);
    surf_down_held=(uint8_t)(input&INPUT_DOWN);
    surf_up_held=(uint8_t)(input&INPUT_UP);
    if(jump_edge && !W.beaver.airborne)surf_jump_buffer=6;
    else if(surf_jump_buffer)surf_jump_buffer--;
    if (dive_edge) W.down_tap = DOUBLE_TAP;
    else if (W.down_tap > 0) W.down_tap--;
    if (jump_edge) W.up_tap = DOUBLE_TAP;
    else if (W.up_tap > 0) W.up_tap--;
    if (W.banner_timer > 0) {
        W.banner_timer--;
        if (W.banner_timer == 0) W.banner = BANNER_NONE;
    }

    stunned = W.stun > 0;
    if (stunned) W.stun--;
    if (W.course_distance > course_length - 128UL * FP) {
        /* Calm-water approach: merge either lane smoothly before reaching sand.
         * Running normal ride physics here would snap the lower lane upward. */
        W.beaver.x += clamp16((int16_t)(BEACH_CAMERA_X - W.beaver.x), -FP, FP);
        W.beaver.y += clamp16((int16_t)(HORIZON - W.beaver.y), -FP/2, FP/2);
        W.beaver.vx = W.beaver.vy = 0;
        W.beaver.airborne = W.beaver.flip = 0;
        W.beaver.lane = W.beaver.lane_to = LANE_TOP;
    } else {
    steer(stunned ? 0 : input);

    world_x = world_beaver_x();
    lane = W.beaver.lane;
    if (lane == LANE_BOTTOM && W.beaver.airborne) {
        surface = front_at(world_x);
    } else if(W.beaver.airborne) {
        surface=sea_at(world_x);
    } else if (lane != LANE_BOTTOM) {
        sea_sample(world_x, &s);
        surface = s.here;
        face = sea_slope_s(&s);
    }

    if (W.beaver.airborne) {
        uint8_t climbing, dropping;
        int16_t bottom;
        top = lane==LANE_TOP?surface:sea_at(world_x);
        if (down_edge && W.beaver.lane_to == LANE_TOP) {
                W.beaver.lane_to=LANE_BOTTOM;
        } else if (up_edge && W.beaver.lane == LANE_BOTTOM && W.beaver.vy<0 && !W.beaver.flip) {
                W.beaver.lane_to = LANE_TOP;
                W.beaver.cleared = 0;
        } else if (jump_edge && !(input&(INPUT_UP|INPUT_DOWN)) && W.beaver.flip == 0 && W.beaver.lane == W.beaver.lane_to) {
            if(surf_flip_safe())W.beaver.flip = FLIP_FRAMES;
            else show(BANNER_LOW,0,0,0);
        }
        if (W.beaver.flip > 0) {
            W.beaver.flip--;
            if (W.beaver.flip == 0) W.beaver.flips = sat_add8(W.beaver.flips, 1);
        }
        W.beaver.air_time = sat_add16(W.beaver.air_time, 1);
        W.beaver.vy = (int16_t)(W.beaver.vy + ((dive && W.beaver.lane == W.beaver.lane_to) ? DIVE_GRAVITY : GRAVITY));
        W.beaver.y = (int16_t)(W.beaver.y + (W.beaver.vy >> 2));
        climbing = (uint8_t)(W.beaver.lane == LANE_BOTTOM && W.beaver.lane_to == LANE_TOP);
        dropping = (uint8_t)(W.beaver.lane == LANE_TOP && W.beaver.lane_to == LANE_BOTTOM);
        bottom = lane==LANE_BOTTOM?surface:dropping?front_at(world_x):0;
        if (climbing && W.beaver.y < top) W.beaver.cleared = 1;
        if (climbing && W.beaver.cleared && W.beaver.y >= top && W.beaver.vy > 0) {
            settle(top, LANE_TOP);
            land(sea_slope(world_x), events, 0);
        } else if (dropping && W.beaver.y >= bottom && W.beaver.vy > 0) {
            settle(bottom, LANE_BOTTOM);
            land(lane_slope(world_x, LANE_BOTTOM), events, 0);
        } else if (climbing && !W.beaver.cleared && W.beaver.y >= bottom && W.beaver.vy > 0) {
            settle(bottom, LANE_BOTTOM);
            land(lane_slope(world_x, LANE_BOTTOM), events, 1);
        } else if (!dropping && !climbing && W.beaver.y >= surface && W.beaver.vy > 0) {
            settle(surface, lane);
            land(lane_slope(world_x,lane), events, 0);
        }
    } else {
        uint8_t allowed;
        int8_t meter;
        if (lane == LANE_BOTTOM) {
            FrontRide ride;
            if(rival_stepping)front_motion(world_x,&ride);
            else front_ride(world_x, &ride);
            surface = ride.surface;
            face = ride.slope;
            allowed = ride.allowed;
            meter = ride.meter;
        } else {
            allowed = sea_jump_allowed_s(&s);
            meter = rival_stepping?0:sea_jump_meter_s(&s);
        }
        W.beaver.y = surface;
        W.beaver.vy = 0;
        ride(face, input);
        if(!rival_stepping)glide_meter(meter);
        if ((jump || surf_jump_buffer) && !stunned && allowed) {
            launch(world_x, input);
            if(lane==LANE_TOP && (input&INPUT_DOWN))W.beaver.lane_to=LANE_BOTTOM;
            surf_jump_buffer=0;
            events->jumped = 1;
        }
    }

    }
    W.speed = clamp16(W.speed, MIN_SPEED, world_max_speed());
}

/* Exchange only compact rider state, never World, the course or its clock.
 * Both racers execute exactly the same movement/landing/flip code above. */
static void exchange_rider(void) {
    Beaver b=W.beaver;
    uint32_t score=W.score;
    int16_t speed=W.speed;
    uint8_t v;
    uint16_t v16;
    RiderState *r=&race.rival;
    W.beaver=r->beaver;r->beaver=b;
    W.score=r->score;r->score=score;
    W.speed=r->speed;r->speed=speed;
#define SWAP8(f) v=(uint8_t)W.f;W.f=r->f;r->f=v
#define SWAP16(f) v16=W.f;W.f=r->f;r->f=v16
    SWAP8(combo);SWAP16(combo_timer);SWAP8(meter);SWAP8(stun);SWAP8(invuln);
    SWAP8(banner);SWAP16(banner_points);SWAP8(banner_mult);SWAP8(banner_air);SWAP8(banner_timer);
    SWAP8(jump_held);SWAP8(dive_held);SWAP8(down_tap);SWAP8(up_tap);
#undef SWAP8
#undef SWAP16
    v=surf_jump_buffer;surf_jump_buffer=r->buffer;r->buffer=v;
    v=surf_up_held;surf_up_held=r->up;r->up=v;
    v=surf_down_held;surf_down_held=r->down;r->down=v;
}
static void check_hit(uint8_t input,Events *events) {
    if(W.invuln)W.invuln--;
    else if(W.phase==PHASE_PLAY && touching_gull(input)) {
        events->hit=1;W.score=W.score>=50?W.score-50:0;
        W.combo=0;W.combo_timer=0;show(BANNER_HIT,50,0,0);lose_half_heart(events);
    }
}
void world_rival_step(uint8_t input,Events *events) BANKED {
    Splash saved=W.splash;
    memset(events,0,sizeof *events);
    exchange_rider();rival_stepping=1;rider_step(input,events);rival_stepping=0;
    check_hit(input,events);exchange_rider();
    W.splash=saved;
}
static void simulate(uint8_t input, Events *events) {
    int16_t scroll_step;

    advance_tick();
    {
        uint8_t scale=16,exit_scale=16;
        if(W.course_distance<256UL*FP) {
            uint16_t distance=(uint16_t)(W.course_distance>>6);
            scale=distance<128?0:(uint8_t)((distance-128)>>3);
        }
        if(W.course_distance>course_length-384UL*FP) {
            uint16_t remaining=W.course_distance<course_length?
                (uint16_t)((course_length-W.course_distance)>>6):0;
            exit_scale=remaining<128?0:(uint8_t)((remaining-128)>>4);
        }
        W.wave_scale=scale<exit_scale?scale:exit_scale;
    }
    sea_refresh(W.tick, W.tier);
    scroll_step = RACING?PX(2):(int16_t)(W.speed >> 2);
    W.scroll += (int32_t)scroll_step;
    W.visual_scroll += (uint16_t)scroll_step;
    W.course_distance = sat_add32(W.course_distance, (uint32_t)(scroll_step > 0 ? scroll_step : 0));
    if (!RACING && (W.tick & 3) == 0) {
        W.score = sat_add32(W.score, W.combo >= FLOW_COMBO ? 2 : 1);
    }
    rider_step(input,events);
    age_splash(scroll_step);
    if(RACING) { /* Sequential race targets are scheduled after the shared pickup move. */ }
    else if(W.course_distance>256UL*FP && W.course_distance<course_length-384UL*FP) move_actors(scroll_step);
    else { memset(W.gull_present,0,sizeof W.gull_present);memset(W.ring_present,0,sizeof W.ring_present);W.heart_present=0; }
    surf_tick(scroll_step,RACING);
    if(RACING)race_targets(scroll_step);
    check_hit(input,events);
    {
        uint8_t other=0,n,npc_input=0;
        Events npc_events;
        if(RACING) {npc_input=race_ai();world_rival_step(npc_input,&npc_events);}
        n=surf_collect(RACING?&race.rival.beaver:0,input,npc_input,&other);
        if(n) {
            W.score=sat_add32(W.score,(uint32_t)n*RING_POINTS);events->ring=n;
            if(W.beaver.airborne)W.beaver.rings=sat_add8(W.beaver.rings,n);
            if(W.banner!=BANNER_TRICK && W.banner!=BANNER_HIT)show(BANNER_COINS,(uint16_t)n*RING_POINTS,n,0);
        }
        if(RACING) {
            race.rival.score=sat_add32(race.rival.score,(uint32_t)other*RING_POINTS);
            if(race.rival.beaver.airborne)race.rival.beaver.rings=sat_add8(race.rival.beaver.rings,other);
            npc_events.ring=other;race_record(&npc_events,1);race_record(events,0);
        }
    }
    if (touching_heart(input)) {
        W.heart_present = 0;
        W.heart_lane = LANE_TOP;
        W.hearts = (uint8_t)(W.hearts + 1);
        if (W.hearts > START_HEARTS) W.hearts = START_HEARTS;
        show(BANNER_HEART, 0, 0, 0);
        events->heart = 1;
    }
    if (W.course_distance >= course_length) {
        W.phase = PHASE_BEACH_END;
        W.beach_timer = 0;
        W.beach_pan=0;
        W.course_distance=course_length;
        W.wave_scale=0;
        W.beaver.x = BEACH_CAMERA_X;
        W.beaver.vx = 0;
        W.beaver.airborne = 0;
        W.beaver.y = HORIZON;
        race_finish();
    }
}

void world_step(uint8_t input, Events *events) BANKED {
    uint8_t start = (input & INPUT_START) != 0;
    uint8_t start_edge = start && !W.start_held;
    memset(events, 0, sizeof(Events));
    W.start_held = start;
    if(race_beach(input,events))return;
    if(race.state==RACE_RUNIN)input=INPUT_RIGHT;

    switch (W.phase) {
    case PHASE_TITLE:
        W.tick++;
        W.scroll += (int32_t)(SCROLL_SPEED >> 2);
        W.visual_scroll += SCROLL_SPEED >> 2;
        if (start_edge) {
            uint32_t best = W.best;
            world_new();
            W.best = best;
            start_stage(1);
            W.start_held = start;
        }
        sea_refresh(W.tick, 0);
        W.beaver.y = sea_at(world_beaver_x());
        break;
    case PHASE_OVER:
        if (start_edge) {
            W.phase = PHASE_TITLE;
            W.tick = 0;
            W.tier = 0;
            W.tier_ticks = 0;
            sea_build(0, 0);
        }
        break;
    case PHASE_BEACH_START:
        beach_step(0, input);
        break;
    case PHASE_BEACH_END:
        beach_step(1, input);
        break;
    case PHASE_SHOP:
        shop_step(input);
        break;
    default:
        simulate(input, events);
        break;
    }
}

/* State record, byte for byte the layout of beaver_sim::World::encode_state. */
static uint8_t *put16(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)(v >> 8);
    return out + 2;
}

static uint8_t *put32(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
    return out + 4;
}

static uint8_t *put_actor(uint8_t *out, uint8_t present, const Actor *actor) {
    *out++ = present;
    out = put16(out, present ? (uint16_t)actor->x : 0);
    out = put16(out, present ? (uint16_t)actor->y : 0);
    return out;
}

void world_encode(uint8_t *out) BANKED {
    uint8_t *p = out;
    uint8_t i;
    p = put32(p, W.tick);
    p = put32(p, (uint32_t)W.scroll);
    p = put16(p, (uint16_t)W.speed);

    p = put16(p, (uint16_t)W.beaver.x);
    p = put16(p, (uint16_t)W.beaver.y);
    p = put16(p, (uint16_t)W.beaver.vx);
    p = put16(p, (uint16_t)W.beaver.vy);
    *p++ = W.beaver.airborne;
    *p++ = W.beaver.flip;
    *p++ = W.beaver.flips;
    *p++ = W.beaver.perfect;
    p = put16(p, W.beaver.air_time);
    *p++ = W.beaver.rings;
    *p++ = W.beaver.lane;
    *p++ = W.beaver.lane_to;
    *p++ = W.beaver.cleared;

    for (i = 0; i < GULL_SLOTS; i++) p = put_actor(p, W.gull_present[i], &W.gulls[i]);
    for (i = 0; i < GULL_SLOTS; i++) p = put16(p, (uint16_t)W.gull_vy[i]);
    for (i = 0; i < GULL_SLOTS; i++) *p++ = W.gull_turn[i];
    for (i = 0; i < RING_SLOTS; i++) p = put_actor(p, W.ring_present[i], &W.rings[i]);
    for (i = 0; i < RING_SLOTS; i++) *p++ = W.ring_lane[i];
    p = put_actor(p, W.heart_present, &W.heart);
    *p++ = W.heart_lane;
    p = put16(p, W.ring_count);

    *p++ = W.splash.present;
    p = put16(p, W.splash.present ? (uint16_t)W.splash.x : 0);
    p = put16(p, W.splash.present ? (uint16_t)W.splash.y : 0);
    *p++ = W.splash.present ? W.splash.age : 0;
    *p++ = W.splash.present ? W.splash.big : 0;

    *p++ = W.combo;
    p = put16(p, W.combo_timer);
    *p++ = (uint8_t)W.meter;
    *p++ = W.stun;

    *p++ = W.banner;
    p = put16(p, W.banner == BANNER_TRICK ? W.banner_points : 0);
    *p++ = W.banner == BANNER_TRICK ? W.banner_mult : 0;
    *p++ = W.banner == BANNER_TRICK ? W.banner_air : 0;
    *p++ = W.banner_timer;
    *p++ = W.hearts;
    p = put32(p, W.score);
    p = put32(p, W.best);
    *p++ = W.phase;
    *p++ = W.invuln;
    p = put16(p, W.gull_timer);
    p = put16(p, W.ring_timer);
    *p++ = W.start_held;
    *p++ = W.jump_held;
    *p++ = W.dive_held;
    *p++ = W.down_tap;
    *p++ = W.up_tap;
    p = put16(p, W.rng);
}
