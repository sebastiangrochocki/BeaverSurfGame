/* Surfing rules. Every function mirrors the method of the same name in sim/src/lib.rs.
 * The world is a global (see rules.h); `W` keeps the code readable.
 *
 * On the Game Boy this file lives in ROM bank 1 so the fixed bank stays below 16 KB. */
#ifdef __SDCC
#pragma bank 1
#endif
#include "rules.h"

#include <string.h>

World world;
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
    W.gull_timer = gull_interval(0);
    W.ring_timer = ring_interval(0);
    W.rng = RNG_SEED;
    sea_build(0, 0);
    W.beaver.y = sea_at(world_beaver_x());
}

static void show(uint8_t banner, uint16_t points, uint8_t mult, uint8_t air) {
    W.banner = banner;
    W.banner_points = points;
    W.banner_mult = mult;
    W.banner_air = air;
    W.banner_timer = BANNER_FRAMES;
}

static void lose_half_heart(Events *events) {
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

/* Leaves the top lane for the bottom one with a short hop. */
static void hop_down(void) {
    W.beaver.lane_to = LANE_BOTTOM;
    W.beaver.cleared = 0;
    W.beaver.flip = 0;
    W.beaver.flips = 0;
    W.beaver.perfect = 0;
    W.beaver.air_time = 0;
    W.beaver.rings = 0;
    W.beaver.vy = HOP_VY;
    W.beaver.airborne = 1;
    W.beaver.y = (int16_t)(W.beaver.y + (W.beaver.vy >> 2));
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
        W.stun = WIPEOUT_FRAMES;
        show(BANNER_WIPEOUT, 0, 0, 0);
        splash_at(1);
        lose_half_heart(events);
        if (W.invuln < WIPEOUT_FRAMES + INVULN_FRAMES) W.invuln = WIPEOUT_FRAMES + INVULN_FRAMES;
        return;
    }
    air_points = W.beaver.perfect ? (uint16_t)(W.beaver.air_time / AIR_FRAMES_PER_POINT) : 0;
    base = (uint16_t)(air_points + FLIP_POINTS * (uint16_t)flips);
    if (base > 0) {
        uint8_t mult = (uint8_t)(1 + flips + W.beaver.rings);
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
    for (i = 0; i < RING_SLOTS; i++) {
        if (!W.ring_present[i]) continue;
        W.rings[i].x = (int16_t)(W.rings[i].x - scroll_step);
        if (W.rings[i].x < PX(-24)) {
            W.ring_present[i] = 0;
            W.ring_lane[i] = LANE_TOP;
        } else {
            W.rings[i].y = (int16_t)(lane_surface(W.scroll + (int32_t)W.rings[i].x, W.ring_lane[i]) - RING_LIFT);
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
        int16_t x = PX(SCREEN_W + 24);
        uint8_t hurt;
        uint8_t lane = (uint8_t)(roll() & 1);
        uint8_t slot;
        W.ring_timer = ring_interval(W.tier);
        W.ring_count++;
        slot = spawn(W.ring_present, W.rings, RING_SLOTS, x,
                     (int16_t)(lane_surface(W.scroll + (int32_t)x, lane) - RING_LIFT));
        if (slot != 0xFF) W.ring_lane[slot] = lane;
        hurt = W.hearts < START_HEARTS;
        if (hurt && W.ring_count % HEART_EVERY_RINGS == 0 && !W.heart_present) {
            x = (int16_t)(x + HEART_GAP);
            W.heart_present = 1;
            W.heart_lane = lane;
            W.heart.x = x;
            W.heart.y = (int16_t)(lane_surface(W.scroll + (int32_t)x, lane) - RING_LIFT);
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

static uint8_t touching_gull(uint8_t tucked) {
    int16_t top = tucked ? PX(16) : PX(24);
    int16_t tall = tucked ? PX(12) : PX(20);
    int16_t bx = (int16_t)(W.beaver.x - PX(8));
    int16_t by = (int16_t)(W.beaver.y - top);
    uint8_t i;
    for (i = 0; i < GULL_SLOTS; i++) {
        if (!W.gull_present[i]) continue;
        if (overlap(bx, by, PX(16), tall, (int16_t)(W.gulls[i].x - PX(10)),
                    (int16_t)(W.gulls[i].y - PX(3)), PX(20), PX(6)))
            return 1;
    }
    return 0;
}

static uint8_t touching_heart(void) {
    if (!W.heart_present) return 0;
    return overlap((int16_t)(W.beaver.x - PX(8)), (int16_t)(W.beaver.y - PX(24)), PX(16), PX(20),
                   (int16_t)(W.heart.x - PX(8)), (int16_t)(W.heart.y - PX(8)), PX(16), PX(16));
}

/* Score only when the jumping beaver's body passes through the hoop. */
static uint8_t through_ring(void) {
    int16_t cx = W.beaver.x;
    int16_t cy = (int16_t)(W.beaver.y - PX(14));
    uint8_t scored = 0;
    uint8_t i;
    if (!W.beaver.airborne) return 0;
    for (i = 0; i < RING_SLOTS; i++) {
        if (!W.ring_present[i]) continue;
        if (abs16((int16_t)(cx - W.rings[i].x)) <= PX(10) &&
            abs16((int16_t)(cy - W.rings[i].y)) <= PX(10)) {
            W.ring_present[i] = 0;
            W.ring_lane[i] = LANE_TOP;
            scored = 1;
        }
    }
    return scored;
}

static void simulate(uint8_t input, Events *events) {
    int16_t scroll_step;
    uint8_t jump_edge, dive_edge, double_down;
    uint8_t stunned;
    Samples s;
    int16_t surface = 0, face = 0;
    int32_t world_x;
    int16_t top;
    uint8_t lane;
    uint8_t jump = (input & INPUT_JUMP) != 0;
    uint8_t dive = (input & INPUT_DIVE) != 0;

    advance_tick();
    sea_refresh(W.tick, W.tier);
    scroll_step = (int16_t)(W.speed >> 2);
    W.scroll += (int32_t)scroll_step;
    if ((W.tick & 3) == 0) {
        W.score = sat_add32(W.score, W.combo >= FLOW_COMBO ? 2 : 1);
    }
    jump_edge = jump && !W.jump_held;
    W.jump_held = jump;
    dive_edge = (uint8_t)(dive && !W.dive_held);
    W.dive_held = dive;
    double_down = (uint8_t)(dive_edge && W.down_tap > 0);
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
    steer(stunned ? 0 : input);

    world_x = world_beaver_x();
    lane = W.beaver.lane;
    if (lane == LANE_BOTTOM && W.beaver.airborne) {
        surface = front_at(world_x);
        face = lane_slope(world_x, LANE_BOTTOM);
    } else if (lane != LANE_BOTTOM) {
        sea_sample(world_x, &s);
        surface = s.here;
        face = sea_slope_s(&s);
    }

    if (W.beaver.airborne) {
        uint8_t climbing, dropping;
        int16_t bottom;
        top = sea_at(world_x);
        if (jump_edge && W.beaver.flip == 0 && W.beaver.lane == W.beaver.lane_to) {
            if (W.beaver.lane == LANE_BOTTOM) {
                W.beaver.lane_to = LANE_TOP;
                W.beaver.cleared = 0;
            } else {
                W.beaver.flip = FLIP_FRAMES;
            }
        }
        if (W.beaver.flip > 0) {
            W.beaver.flip--;
            if (W.beaver.flip == 0) W.beaver.flips = sat_add8(W.beaver.flips, 1);
        }
        W.beaver.air_time = sat_add16(W.beaver.air_time, 1);
        W.beaver.vy = (int16_t)(W.beaver.vy + ((dive && W.beaver.lane == W.beaver.lane_to) ? DIVE_GRAVITY : GRAVITY));
        W.beaver.y = (int16_t)(W.beaver.y + (W.beaver.vy >> 2));
        climbing = (uint8_t)(W.beaver.lane == LANE_BOTTOM && W.beaver.lane_to == LANE_TOP);
        if (double_down && W.beaver.lane == LANE_TOP && W.beaver.lane_to == LANE_TOP && W.beaver.vy > 0) {
            int16_t gap = (int16_t)(top - W.beaver.y);
            if (gap >= 0 && gap < DROP_WINDOW) W.beaver.lane_to = LANE_BOTTOM;
        }
        dropping = (uint8_t)(W.beaver.lane == LANE_TOP && W.beaver.lane_to == LANE_BOTTOM);
        bottom = front_at(world_x);
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
            land(face, events, 0);
        }
    } else {
        uint8_t allowed;
        int8_t meter;
        if (lane == LANE_BOTTOM) {
            FrontRide ride;
            front_ride(world_x, &ride);
            surface = ride.surface;
            face = ride.slope;
            allowed = ride.allowed;
            meter = ride.meter;
        } else {
            allowed = sea_jump_allowed_s(&s);
            meter = sea_jump_meter_s(&s);
        }
        W.beaver.y = surface;
        W.beaver.vy = 0;
        ride(face, input);
        glide_meter(meter);
        if (!stunned && double_down && lane == LANE_TOP) {
            hop_down();
        } else if (jump && !stunned && allowed) {
            launch(world_x, input);
            events->jumped = 1;
        }
    }

    W.speed = clamp16(W.speed, MIN_SPEED, world_max_speed());
    age_splash(scroll_step);
    move_actors(scroll_step);
    if (W.invuln > 0) {
        W.invuln--;
    } else if (W.phase == PHASE_PLAY && touching_gull((uint8_t)(dive && W.beaver.flip == 0 && !double_down))) {
        events->hit = 1;
        lose_half_heart(events);
    }
    if (through_ring()) {
        W.score = sat_add32(W.score, RING_POINTS);
        events->ring = 1;
        if (W.beaver.airborne) W.beaver.rings = sat_add8(W.beaver.rings, 1);
    }
    if (touching_heart()) {
        W.heart_present = 0;
        W.heart_lane = LANE_TOP;
        W.hearts = (uint8_t)(W.hearts + 1);
        if (W.hearts > START_HEARTS) W.hearts = START_HEARTS;
        show(BANNER_HEART, 0, 0, 0);
        events->heart = 1;
    }
}

void world_step(uint8_t input, Events *events) BANKED {
    uint8_t start = (input & INPUT_START) != 0;
    uint8_t start_edge = start && !W.start_held;
    memset(events, 0, sizeof(Events));
    W.start_held = start;

    switch (W.phase) {
    case PHASE_TITLE:
        W.tick++;
        W.scroll += (int32_t)(SCROLL_SPEED >> 2);
        if (start_edge) {
            uint32_t best = W.best;
            world_new();
            W.best = best;
            W.phase = PHASE_PLAY;
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
