#pragma bank 6
#include <gb/gb.h>
#include <string.h>
#include "render.h"
#include "sprites.h"
#include "rules.h"
#include "actors.h"
#include "beach.h"
#include "shop.h"
#include "surf.h"
#include "race.h"
#define TILE_BEAVER 0
#define TILE_BOARD 112
#define TILE_GULL 136
#define TILE_RING 160
#define TILE_HEART 172
#define TILE_SPLASH 176
#define TILE_ARROW 184
#define TILE_POP 186
#define BOARD_FLAT 0
#define BOARD_UP 8
#define BOARD_DOWN 16
#define BOARD_TOP_ROW 15
#define FRAME_TILES 16
#define FRAME_SURF 0
#define FRAME_TUG 1
#define FRAME_JUMP 2
#define FRAME_FLIP 3
#define FRAME_FLIP90 4
#define FRAME_LEFT 5
#define FRAME_RIGHT 6
#define OAM_PAGE_A 0xC900
extern uint8_t *oam_back;
extern uint8_t oam_n,oam_used[2];

/* Sprites already queued on each scanline. The hardware draws at most ten per line and cuts
 * the rest off from the end of OAM, which is what slices a gull or a ring in half. */
static uint8_t line_n[144];

/* Collected hoop, in world X and screen-rule Y. Counts down once per drawn frame. */

/* The 16 scanlines an 8x16 sprite covers. A C loop here was most of a frame; the count only
 * has to stay honest so a ring or the board can tell when a line is full. */
static uint8_t bp_base, bp_ox, bp_cols, bp_rows, bp_prop, bp_rowi, bp_coli, bp_sy;
static int16_t bp_y;

/* fit 0, no flip: the beaver, the gulls and the arrow. The C per-cell path was a frame by itself. */
static void blit_plain(void) __naked {
    __asm
        xor a
        ld (#_bp_rowi), a
bp_row_loop:
        ld a, (#_bp_rowi)
        ld hl, #_bp_rows
        cp a, (hl)
        jp NC, bp_done
        ld l, a
        ld h, #0x00
        add hl, hl
        add hl, hl
        add hl, hl
        add hl, hl
        ld a, (#_bp_y)
        add a, l
        ld e, a
        ld a, (#_bp_y + 1)
        adc a, h
        ld d, a
        ld a, e
        add a, #16
        ld (#_bp_sy), a
        cp a, #176
        jr NC, bp_no_count
        ld a, d
        or a
        jr NZ, bp_no_count
        ld a, e
        cp a, #144
        jr NC, bp_no_count
        ld hl, #_line_n
        add hl, de
        ld a, (#_bp_cols)
        ld b, a
        ld c, #16
bp_count:
        ld a, (hl)
        add a, b
        ld (hl+), a
        inc e
        ld a, e
        cp a, #144
        jr NC, bp_no_count
        dec c
        jr NZ, bp_count
bp_no_count:
        xor a
        ld (#_bp_coli), a
bp_col_loop:
        ld a, (#_bp_coli)
        ld hl, #_bp_cols
        cp a, (hl)
        jp NC, bp_next_row
        add a, a
        add a, a
        add a, a
        ld hl, #_bp_ox
        add a, (hl)
        ld c, a
        cp a, #176
        jr NC, bp_next_col
        ld a, (#_bp_sy)
        cp a, #176
        jr NC, bp_next_col
        ld a, (#_oam_n)
        cp a, #40
        jr NC, bp_next_col
        ld b, a
        add a, a
        add a, a
        ld l, a
        ld h, #0x00
        ld a, (#_oam_back)
        add a, l
        ld l, a
        ld a, (#_oam_back + 1)
        adc a, h
        ld h, a
        ld a, (#_bp_sy)
        ld (hl+), a
        ld a, c
        ld (hl+), a
        ld a, (#_bp_base)
        ld (hl+), a
        ld a, (#_bp_prop)
        ld (hl), a
        ld hl, #_oam_n
        inc (hl)
bp_next_col:
        ld hl, #_bp_base
        inc (hl)
        inc (hl)
        ld hl, #_bp_coli
        inc (hl)
        jp bp_col_loop
bp_next_row:
        ld hl, #_bp_rowi
        inc (hl)
        jp bp_row_loop
bp_done:
        ret
    __endasm;
}

static uint8_t row_free(int16_t top) __naked {
    __asm
        ld a, d
        or a
        jr NZ, rf_none
        ld a, e
        cp a, #144
        jr NC, rf_none
        ld hl, #_line_n
        add hl, de
        ld c, #16
        ld b, #10
rf_fast:
        ld a, #10
        sub a, (hl)
        inc hl
        cp a, b
        jr NC, rf_keep
        ld b, a
rf_keep:
        inc e
        ld a, e
        cp a, #144
        jr NC, rf_out
        dec c
        jr NZ, rf_fast
rf_out:
        ld a, b
        ret
rf_none:
        ld a, #10
        ret
    __endasm;
}

static void take_row(int16_t top) __naked {
    __asm
        ld a, d
        or a
        ret NZ
        ld a, e
        cp a, #144
        ret NC
        ld hl, #_line_n
        add hl, de
        ld c, #16
tr_fast:
        inc (hl)
        inc hl
        inc e
        ld a, e
        cp a, #144
        ret NC
        dec c
        jr NZ, tr_fast
        ret
    __endasm;
}

/* True when every row of a no-flip block has room for all of its columns. */
static uint8_t block_fits(int16_t y, uint8_t cols, uint8_t rows) {
    uint8_t row;
    for (row = 0; row < rows; row++) {
        if (row_free((int16_t)(y + (int16_t)((uint8_t)row << 4))) < cols) return 0;
    }
    return 1;
}

/* One 8x16 sprite. `x` and `y` are the top-left in screen pixels. Written straight into the
 * back OAM page in hardware order; the hardware shows a sprite at (OAM x - 8, OAM y - 16). */
/* `cols` by `rows` of 8x16 sprites. Tile numbers are even and packed in that order. Once the
 * block is known to touch the screen the OAM bytes are built in 8-bit arithmetic; parts that
 * hang off the right or bottom edge are skipped, parts off the left or top are clipped by the
 * hardware. `fit` 0 always queues. `fit` 1 queues the whole block or nothing, so a gull or a
 * ring is never sliced. `fit` 2 queues cells while the row still has a free slot. */
static uint8_t add_block(uint8_t base, int16_t x, int16_t y, uint8_t cols, uint8_t rows, uint8_t prop,
                         uint8_t fit) {
    uint8_t row;
    uint8_t ox;
    uint8_t *o;
    uint8_t hflip = (uint8_t)(prop & 0x20);
    uint8_t vflip = (uint8_t)(prop & 0x40);
    /* Only a block entirely off screen is dropped; a part hanging off the left or top wraps
     * to an OAM coordinate above 175 and is skipped below, the rest still shows. A flip bit
     * in prop also swaps columns or rows, so the whole picture turns rather than each cell. */
    if (x <= -8 * (int16_t)cols || x >= 168 || y <= -16 * (int16_t)rows || y >= 144) return 0;
    /* fit 1 draws the whole block or nothing. Both that and a board that fully fits skip the
     * per-cell scanline walk; a partial board still uses the slow path below. */
    if ((prop & 0x60) == 0) {
        if (fit == 0 || ((fit == 1 || fit == 2) && block_fits(y, cols, rows))) {
            bp_base = base;
            bp_ox = (uint8_t)(x + 8);
            bp_y = y;
            bp_cols = cols;
            bp_rows = rows;
            bp_prop = prop;
            blit_plain();
            return 1;
        }
        /* The line is full. A whole-or-nothing sprite is left out rather than sliced, and
         * the slow walk below would only reach the same answer. */
        if (fit == 1) return 0;
    }
    ox = (uint8_t)(x + 8);
    if (fit == 1) {
        for (row = 0; row < rows; row++) {
            uint8_t col;
            uint8_t visible = 0;
            int16_t top = (int16_t)(y + (int16_t)((vflip ? (uint8_t)(rows - 1 - row) : row) << 4));
            uint8_t sy = (uint8_t)(top + 16);
            for (col = 0; col < cols; col++) {
                uint8_t sx = (uint8_t)(ox + (uint8_t)((hflip ? (uint8_t)(cols - 1 - col) : col) << 3));
                if (sx < 176 && sy < 176) visible++;
            }
            if (visible > row_free(top)) return 0;
        }
    }
    o = oam_back + (uint8_t)(oam_n << 2);
    for (row = 0; row < rows; row++) {
        uint8_t col;
        int16_t top = (int16_t)(y + (int16_t)((vflip ? (uint8_t)(rows - 1 - row) : row) << 4));
        uint8_t sy = (uint8_t)(top + 16);
        for (col = 0; col < cols; col++) {
            uint8_t sx = (uint8_t)(ox + (uint8_t)((hflip ? (uint8_t)(cols - 1 - col) : col) << 3));
            if (sx < 176 && sy < 176 && oam_n < 40 && (fit != 2 || row_free(top) > 0)) {
                o[0] = sy;
                o[1] = sx;
                o[2] = base;
                o[3] = prop;
                o += 4;
                oam_n++;
                take_row(top);
            }
            base = (uint8_t)(base + 2);
        }
    }
    return 1;
}

/* Nose up while the beaver is rising, nose down while falling, flat on level water.
 * On the sea that is the slope at the beaver's position times the way they travel along it.
 * In the air it is their vertical speed. Buttons do not pick the board. */
static uint8_t board_for_path(void) {
    int16_t face;
    int16_t along;
    if (world.phase != PHASE_PLAY) return BOARD_FLAT;
    if (world.beaver.airborne) {
        if (world.beaver.vy < 0) return BOARD_UP;
        if (world.beaver.vy > 0) return BOARD_DOWN;
        return BOARD_FLAT;
    }
    face = lane_slope(world_beaver_x(), world.beaver.lane);
    along = (int16_t)((world.speed >> 2) + (world.beaver.vx >> 2));
    if (face > -PX(1) && face < PX(1)) return BOARD_FLAT;
    if (along > -16 && along < 16) return BOARD_FLAT;
    return ((int32_t)face * (int32_t)along < 0) ? BOARD_UP : BOARD_DOWN;
}

/* Which body frame the beaver shows. Title and the game-over screen hold the surf pose:
 * stun and invuln are not stepped there, so a wipeout would otherwise freeze mid-tumble.
 * `mirror` is set for the half-turn and three-quarter-turn of a backflip. */
static uint8_t beaver_pose(uint8_t *mirror) {
    return actor_pose(last_input,mirror);
}

/* Rules live in the 240x160 Game Boy Advance frame. The Color screen is 160 wide, and the
 * surfable wave has to sit in rows 72-119, so actors are squeezed across and lifted up. */
#define VIEW_LIFT 24

/* 2r / 3 for r in 0..47; the 48-pixel groups above it contribute 32 each. */
static const uint8_t TWO_THIRDS[48] = {0,  0,  1,  2,  2,  3,  4,  4,  5,  6,  6,  7,  8,  8,  9,  10,
                                       10, 11, 12, 12, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 20,
                                       21, 22, 22, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31};

/* (px * 2) / 3 without a division; anything far off screen just stays hidden. */
static int16_t view_x(int16_t px) {
    uint16_t v;
    uint8_t groups = 0;
    if (px < -48 || px > 400) return -64;
    v = (uint16_t)(px + 48);
    while (v >= 48) {
        v -= 48;
        groups++;
    }
    return (int16_t)(((int16_t)groups << 5) + TWO_THIRDS[v] - 32);
}

static int16_t view_y(int16_t py) {
    return (int16_t)(py - VIEW_LIFT);
}

/* Full hoop, then the smaller hoop, then the two halves fly apart. */
static void draw_ring_pop(void) {
    uint8_t i;
    for(i=0;i<POP_SLOTS;i++)if(pickup_pops[i].life) {
        int16_t x=view_x((int16_t)((pickup_pops[i].wx-world.scroll)>>6));
        int16_t y=view_y((int16_t)(pickup_pops[i].y>>6));
        add_block((uint8_t)(TILE_POP+(pickup_pops[i].life>6?0:4)),x-8,y-8,2,1,2,1);
    }
}

static void draw_rival(uint8_t walking) {
    const Beaver *b=&race.rival.beaver;
    int16_t x,y;
    uint8_t mirror=0,frame,base,prop=6,deck=BOARD_FLAT;
    if(world.phase==PHASE_SHOP)return;
    if(race.state==RACE_IDLE || race.state==RACE_OFFER) {
        if(world.phase!=PHASE_BEACH_END && (world.phase!=PHASE_BEACH_START || world.stage<2))return;
        x=view_x((int16_t)((world.phase==PHASE_BEACH_END?RIVAL_FINISH_X:RIVAL_BEACH_X)-(int16_t)(world.beach_pan>>6)));y=view_y(RIVAL_BEACH_Y);
    } else if(race.state==RACE_RUNIN) {
        x=view_x((int16_t)(world.beaver.x>>6));y=view_y(140);
    } else if(RACING || race.state==RACE_COUNTDOWN || race.state==RACE_RESULTS) {
        x=view_x((int16_t)(b->x>>6));y=view_y((int16_t)(b->y>>6));
    } else return;
    if(walking) {
        base=(uint8_t)(BEACH_ACTOR_TILE+(race.state==RACE_RUNIN && !race.timer?((world.tick>>3)&1)*16:0));
        add_block(base,x-16,y-23,4,2,6,0);return;
    }
    frame=actor_pose_for(b,race.ai_input,&mirror);
    if(race.rival.invuln && ((race.rival.invuln>>2)&1))prop=7;
    if(mirror || frame==FRAME_FLIP || frame==FRAME_FLIP90) {
        add_block((uint8_t)(frame*16),x-16,y-31,4,2,(uint8_t)(prop|mirror),0);
    } else {
        if(b->vy<0)deck=BOARD_UP;else if(b->vy>0)deck=BOARD_DOWN;
        add_block((uint8_t)(frame*16),x-16,y-31,4,1,prop,0);
        actor_prepare_rival(frame,deck);
        add_block(164,x-16,y-15,4,1,prop,0);
    }
}

void sprites_prepare(void) BANKED {
    int16_t feet_x = view_x((int16_t)(world.beaver.x >> 6));
    int16_t feet_y = view_y((int16_t)(world.beaver.y >> 6));
    uint8_t playing = world.phase == PHASE_PLAY;
    uint8_t walking = world.phase == PHASE_BEACH_START || world.phase == PHASE_BEACH_END || world.phase==PHASE_SHOP;
    uint8_t show_board = 1;
    uint8_t combined_frame = 255;
    uint8_t i;
    uint8_t drawn;
    uint8_t rider_prop=(uint8_t)(playing && world.invuln && ((world.invuln>>2)&1)?7:0);
    oam_n = 0;
    memset(line_n, 0, sizeof line_n);

    /* A few pixels of rise and fall on the title, matching the Advance build. */
    if (world.phase == PHASE_TITLE) {
        uint8_t t = (uint8_t)(world.tick % 24);
        feet_y = (int16_t)(feet_y + (t < 12 ? (t >> 2) : ((24 - t) >> 2)));
    }

    /* The meter arrow sits on the top rows, clear of the actors, so it takes its slot first. */
    if (playing && !RACING && world.meter >= 0) {
        add_block((uint8_t)TILE_ARROW, (int16_t)(28 + world.meter), 0, 1, 1, 5, 0);
    }

    /* Keep the rider and overlapping deck intact first. Other actors are admitted
     * whole only when the remaining scanline budget can hold them.
     * Invuln blinking is play-only. After a death the counter is left where it stopped,
     * and the title would otherwise hide the beaver for good. */
    {
        uint8_t mirror = 0;
        uint8_t frame = beaver_pose(&mirror);
        int16_t body_y = (int16_t)(feet_y + (walking ? 8 : 0));
        uint8_t show_body = 1;
        if (playing && !RACING && world.banner == BANNER_WIPEOUT && world.stun) {
            uint8_t gone = (uint8_t)(WIPEOUT_FRAMES - world.stun);
            /* Thrown off and under, then the beaver climbs back on. */
            frame = FRAME_SURF;
            mirror = 0;
            if (gone < 16) {
                body_y = (int16_t)(feet_y + gone);
            } else if (gone < 48) {
                show_body = 0;
            } else {
                body_y = (int16_t)(feet_y + (WIPEOUT_FRAMES - gone));
            }
        }
        /* Surf art leaves eight blank bottom rows for the overlapping board. The
         * carrying poses contain their own board and align their last ink row to the feet. */
        if (frame == FRAME_FLIP || frame == FRAME_FLIP90) show_board = 0;
        if (show_body) {
            uint8_t base=(uint8_t)(TILE_BEAVER+frame*FRAME_TILES);
            if(walking) {
                if(shop_direction && world.phase!=PHASE_SHOP) {
                    base=(uint8_t)(BEACH_ACTOR_TILE+(shop_direction==1?32:48));
                    mirror=0;
                    /* Supplied front/back poses share the side-walk feet anchor
                     * and already contain their alternating steps. */
                } else {
                    base=(uint8_t)(BEACH_ACTOR_TILE+(world.beaver.vx?((world.tick>>3)&1)*16:0));
                    mirror=world.facing_left?0x20:0;
                }
            }
            /* Merge only the overlapping lower row into the deck's four objects.
             * Source body/deck assets stay independent; this saves four OAM entries
             * per overlapping scanline so a nearby gull remains visible. */
            if(!walking && show_board && body_y==feet_y && mirror==0) combined_frame=frame;
            add_block(base,(int16_t)(feet_x-16),(int16_t)(body_y-31),4,
                      combined_frame==255?2:1,(uint8_t)(mirror|rider_prop),0);
        }
    }

    /* Reserve the whole deck immediately after the body: overlap must not let a
     * pickup consume its OAM budget and slice off a board segment. */
    if (show_board && !walking) {
        actor_prepare(combined_frame,board_for_path());
        add_block((uint8_t)TILE_BOARD, (int16_t)(feet_x - 16),
                  (int16_t)(feet_y - BOARD_TOP_ROW), 4, 1, rider_prop, 0);
    }
    draw_rival(walking);

    /* Gulls, pickups and spray belong to a run. Game over leaves them in the world, and the
     * title that follows would otherwise keep drawing a heart over an empty sea. Gulls go
     * before the pickups: a gull is always drawn, and a ring or heart that would push a
     * line past ten sprites is skipped whole instead of losing a side. */
    if (playing) {
        surf_gull_shown=0;
        drawn = 0;
        /* Keep three gulls visible at most. The renderer still skips a whole actor
         * when the hardware scanline budget is full. Full 32px gulls plus the beaver can
         * exceed the GBC's ten-sprites-per-scanline limit during a beach transition. */
        for (i = 0; i < 4 && drawn < 3; i++) {
            int16_t x;
            int16_t y;
            uint8_t frame;
            if (!world.gull_present[i]) continue;
            x = view_x((int16_t)(world.gulls[i].x >> 6));
            y = view_y((int16_t)(world.gulls[i].y >> 6));
            if (x < -24 || x > 184) continue;
            /* Wing stroke up, level, down, level; the 32x16 frame has its body around x 3-28. */
            frame = (uint8_t)((world.tick >> 3) & 3);
            if (frame == 3) frame = 1;
            if(add_block((uint8_t)(TILE_GULL + frame * (RACING?4:8)),x-(RACING?8:15),y-8,RACING?2:4,1,1,1)) {
                drawn++;surf_gull_shown|=(uint8_t)(1u<<i);
            }
        }

        drawn = 0;
        for (i = 0; i < PICKUP_SLOTS; i++) {
            int16_t x;
            int16_t y;
            pickups[i].flags&=(uint8_t)~PICKUP_SHOWN;
            if (!(pickups[i].flags&PICKUP_PRESENT)) continue;
            x = view_x((int16_t)(pickups[i].x >> 6));
            y = view_y((int16_t)(pickups[i].y >> 6));
            if (x < -24 || x > 184) continue;
            /* The sheet is shifted down nine rows, so the top is nine pixels higher and the
             * blank tail stays above the ring instead of covering the beaver's head. */
            if(add_block(TILE_RING,x-8,y-8,2,1,2,1))pickups[i].flags|=PICKUP_SHOWN;
        }

        if (world.heart_present) {
            int16_t x = (int16_t)(world.heart.x >> 6);
            int16_t y = (int16_t)(world.heart.y >> 6);
            add_block((uint8_t)TILE_HEART, (int16_t)(view_x(x) - 8), (int16_t)(view_y(y) - 8), 2, 1, 3, 1);
        }
    }

    if(playing)draw_ring_pop();

    if (playing && world.splash.present) {
        int16_t sx = view_x((int16_t)(world.splash.x >> 6));
        int16_t sy = view_y((int16_t)(world.splash.y >> 6));
        add_block((uint8_t)TILE_SPLASH, (int16_t)(sx - 16), (int16_t)(sy - 16), 4, 1, 4, 2);
    }
    /* Entries this page used last time but not now hide with y = 0. */
    {
        uint8_t page = (uint16_t)oam_back == OAM_PAGE_A ? 0 : 1;
        for (i = oam_n; i < oam_used[page]; i++) oam_back[(uint8_t)(i << 2)] = 0;
        oam_used[page] = oam_n;
    }
}
