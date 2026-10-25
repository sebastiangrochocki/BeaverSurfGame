#include "ramp.h"
#include "render.h"

#include <stddef.h>

#include <gb/cgb.h>
#include <gb/gb.h>
#include <gb/metasprites.h>
#include <string.h>

#include "arrow.h"
#include "beaver.h"
#include "bg.h"
#include "bg0.h"
#include "board.h"
#include "gull.h"
#include "heart.h"
#include "pop.h"
#include "ring.h"
#include "rules.h"
#include "splash.h"
#include "tiles.h"

#define TILE_BEAVER 0
#define TILE_BOARD (TILE_BEAVER + beaver_TILE_COUNT)
#define TILE_GULL (TILE_BOARD + board_TILE_COUNT)
#define TILE_RING (TILE_GULL + gull_TILE_COUNT)
#define TILE_HEART (TILE_RING + ring_TILE_COUNT)
#define TILE_SPLASH (TILE_HEART + heart_TILE_COUNT)
#define TILE_ARROW (TILE_SPLASH + splash_TILE_COUNT)
#define TILE_POP (TILE_ARROW + arrow_TILE_COUNT)

/* Beaver sheet: seven 32x32 frames. Flip steps 180 and 270 reuse the upright and 90 degree
 * frames with both axes mirrored. Board sheet: flat, nose up, nose down. */
#define BOARD_FLAT 0
#define BOARD_UP 8
#define BOARD_DOWN 16
/* Deck packed to the top of the 16px cell, so the blank rows sit in the water under the feet
 * instead of across the belly. Art row 0 is the old row 7, which used to land at feet_y - 3. */
#define BOARD_TOP_ROW 3
#define FRAME_TILES 16
#define FRAME_SURF 0
#define FRAME_TUG 1
#define FRAME_JUMP 2
#define FRAME_FLIP 3
#define FRAME_FLIP90 4
#define FRAME_LEFT 5
#define FRAME_RIGHT 6
#define OAM_FLIP_XY 0x60
typedef char beaver_sheet_ok[(beaver_TILE_COUNT == 7 * FRAME_TILES && board_TILE_COUNT == 24) ? 1 : -1];

/* Bank-0 ramp tiles start where the sprite patterns end; the generator assumes that index. */
typedef char ramp0_base_ok[(TILE_POP + pop_TILE_COUNT <= TILE_RAMP0) ? 1 : -1];
typedef char pop_sheet_ok[(pop_TILE_COUNT == 8) ? 1 : -1];
typedef char ramp0_fits[(TILE_RAMP0 + bg0_TILE_COUNT <= 256) ? 1 : -1];

/* Map rows 9-17 (lines 72-143) hold both waves: the surf line with its sky above, and the
 * bottom lane painted over whatever of it the near water hides. */
#define WAVE_ROW 9
#define WAVE_ROWS 9
#define COLS 20

extern volatile uint8_t scx_sky;
extern volatile uint8_t scx_island;

/* Fixed WRAM above GBDK's data (which ends well under 0xC900; see build/beaver.map).
 *
 * Nothing touches VRAM while the LCD runs except render_present, which copies these shadow
 * maps with general-purpose DMA in the first few hundred cycles of vblank. The shadow maps
 * use the map's own 32-byte row stride so one DMA per band moves a whole band; the attribute
 * plane sits exactly 0x200 above the tile plane so the painter flips one address bit.
 *
 *   C900, CA00  two 160-byte OAM pages, swapped each frame (the VBL OAM DMA reads the front)
 *   CB00        palette attribute for every background tile (bg_map_attributes | 0x08)
 *   CC00        shadow tiles: HUD rows 0-1 at +000, wave rows 9-17 at +040, shore rows 5-8 at +160
 *   CE00        shadow attributes, same layout. The two planes meet: tiles fill CC00-CDFF.
 */
#define OAM_PAGE_A 0xC900
#define OAM_PAGE_B 0xCA00
#define ATTR_LUT 0xCB00
#define SHADOW_TILES 0xCC00
#define SHADOW_ATTRS 0xCE00
#define SHADOW_HUD 0x000
#define SHADOW_WAVE 0x040
#define SHADOW_SHORE 0x160
#define SHADOW_LEN 0x200

#define ATTR_OF(tile) (((volatile uint8_t *)ATTR_LUT)[tile])

/* Everything prepare_hud reads. The rows are rebuilt only when one of these moved. */
typedef struct {
    uint32_t score, best;
    uint16_t banner_points;
    uint8_t phase, blink, hearts, banner, banner_air, banner_mult, combo, combo_hot, pause;
} HudState;
static HudState hud_last;
static uint8_t hud_valid;

static uint8_t hud_changed(void) {
    HudState now;
    now.score = world.phase == PHASE_PLAY ? 0 : world.score;
    now.best = world.best;
    now.banner_points = world.banner_points;
    now.phase = world.phase;
    now.blink = (uint8_t)((world.tick >> 5) & 1);
    now.hearts = world.hearts;
    now.banner = world.banner;
    now.banner_air = world.banner_air;
    now.banner_mult = world.banner_mult;
    now.combo = world.combo;
    now.combo_hot = world.combo_timer > 90;
    now.pause = pause_show;
    if (hud_valid && memcmp(&now, &hud_last, sizeof now) == 0) return 0;
    hud_last = now;
    hud_valid = 1;
    return 1;
}



static uint8_t want_hud[2][COLS];

static uint8_t hy[21];
static uint8_t fy[21];
static uint8_t fy_half[11];
static uint8_t *oam_back;
static uint8_t oam_n;
static uint8_t oam_used[2];

/* Direct map write. Only for render_init, while the LCD is off. */
static void poke(uint16_t base, uint8_t x, uint8_t y, uint8_t tile) {
    volatile uint8_t *cell = (volatile uint8_t *)(base + (uint16_t)y * 32u + x);
    *cell = tile;
    VBK_REG = 1;
    *cell = ATTR_OF(tile);
    VBK_REG = 0;
}

static void put_bg(uint8_t x, uint8_t y, uint8_t tile) { poke(0x9800u, x, y, tile); }

static void stamp_block(uint8_t x, uint8_t y, uint8_t origin) {
    uint8_t row;
    uint8_t col;
    for (row = 0; row < 2; row++) {
        for (col = 0; col < 2; col++) {
            put_bg((uint8_t)(x + col), (uint8_t)(y + row), (uint8_t)(origin + row * 2 + col));
        }
    }
}

static uint8_t glyph(char ch) {
    if (ch >= '0' && ch <= '9') return (uint8_t)(TILE_DIGIT + (ch - '0'));
    if (ch >= 'A' && ch <= 'Z') return (uint8_t)(TILE_LETTER + (ch - 'A'));
    return TILE_HUD;
}

static void fill_row(uint8_t row, uint8_t tile) {
    uint8_t col;
    for (col = 0; col < COLS; col++) want_hud[row][col] = tile;
}

static void write_text(uint8_t row, uint8_t col, const char *text) {
    while (*text && col < COLS) {
        want_hud[row][col] = glyph(*text);
        text++;
        col++;
    }
}

static void center_text(uint8_t row, const char *text) {
    uint8_t len = 0;
    while (text[len]) len++;
    fill_row(row, TILE_HUD);
    if (len < COLS) write_text(row, (uint8_t)((COLS - len) / 2), text);
}

static void write_num(uint8_t row, uint8_t col, uint32_t value, uint8_t digits);

/* Label, a blank tile, then six digits, centered as one line. "BEST" at a fixed column
 * sat against the number, so the title read BEST001820. */
static void center_label_num(uint8_t row, const char *label, uint32_t value) {
    uint8_t label_len = 0;
    uint8_t len;
    uint8_t col;
    while (label[label_len]) label_len++;
    len = (uint8_t)(label_len + 1 + 6);
    fill_row(row, TILE_HUD);
    col = (uint8_t)((COLS - len) / 2);
    write_text(row, col, label);
    write_num(row, (uint8_t)(col + label_len + 1), value, 6);
}

/* Decimal digits of the in-play score, carried across frames: a small increase just adds
 * into the cached digits instead of running six 32-bit subtraction loops. */
static uint32_t sd_value;
static uint8_t sd_digits[6];
static uint8_t sd_valid;

static void write_score(uint32_t score) {
    uint8_t i;
    if (!sd_valid || score < sd_value || score - sd_value > 9) {
        write_num(0, 14, score, 6);
        for (i = 0; i < 6; i++) sd_digits[i] = want_hud[0][14 + i];
        sd_value = score;
        sd_valid = 1;
        return;
    }
    if (score != sd_value) {
        uint8_t add = (uint8_t)(score - sd_value);
        i = 5;
        for (;;) {
            uint8_t v = (uint8_t)(sd_digits[i] + add);
            if (v < TILE_DIGIT + 10) {
                sd_digits[i] = v;
                break;
            }
            sd_digits[i] = (uint8_t)(v - 10);
            add = 1;
            if (i == 0) break;
            i--;
        }
        sd_value = score;
    }
    for (i = 0; i < 6; i++) want_hud[0][14 + i] = sd_digits[i];
}

/* Pushes just the six score cells to the shadow map. */
static void score_commit(void) {
    uint8_t i;
    volatile uint8_t *tiles = (volatile uint8_t *)(SHADOW_TILES + SHADOW_HUD + 14);
    volatile uint8_t *attrs = (volatile uint8_t *)(SHADOW_ATTRS + SHADOW_HUD + 14);
    for (i = 0; i < 6; i++) {
        uint8_t tile = sd_digits[i];
        tiles[i] = tile;
        attrs[i] = ATTR_OF(tile);
    }
}

static void write_num(uint8_t row, uint8_t col, uint32_t value, uint8_t digits) {
    static const uint32_t place[6] = {100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL};
    uint8_t d;
    uint8_t first = (uint8_t)(6u - digits);
    for (d = 0; d < digits; d++) {
        uint32_t unit = place[first + d];
        uint8_t n = 0;
        while (value >= unit) {
            value -= unit;
            n++;
        }
        want_hud[row][col + d] = (uint8_t)(TILE_DIGIT + n);
    }
}

/* Sprites already queued on each scanline. The hardware draws at most ten per line and cuts
 * the rest off from the end of OAM, which is what slices a gull or a ring in half. */
static uint8_t line_n[144];

/* Collected hoop, in world X and screen-rule Y. Counts down once per drawn frame. */
static int32_t pop_wx;
static int16_t pop_y;
static uint8_t pop_age;

#define POP_FRAMES 15

void render_ring_pop(void) {
    pop_wx = world_beaver_x();
    pop_y = (int16_t)(world.beaver.y - PX(14));
    pop_age = POP_FRAMES;
}

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
    *mirror = 0;
    if (world.phase != PHASE_PLAY) return FRAME_SURF;
    if (world.beaver.airborne && world.beaver.flip > 0) {
        /* Four quarter-turns. 180 and 270 are the first two frames mirrored on both axes. */
        uint8_t turned = (uint8_t)(((uint16_t)(FLIP_FRAMES - world.beaver.flip) * 4) / FLIP_FRAMES);
        if (turned > 3) turned = 3;
        if (turned >= 2) *mirror = OAM_FLIP_XY;
        return (turned & 1) ? FRAME_FLIP90 : FRAME_FLIP;
    }
    /* Down is the crouch: speed on a downhill face, and a shorter body under the gulls. */
    if (last_input & INPUT_DIVE) return FRAME_TUG;
    if (world.beaver.airborne) return FRAME_JUMP;
    if ((last_input & INPUT_LEFT) && !(last_input & INPUT_RIGHT)) return FRAME_LEFT;
    if ((last_input & INPUT_RIGHT) && !(last_input & INPUT_LEFT)) return FRAME_RIGHT;
    return FRAME_SURF;
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
    int16_t x;
    int16_t y;
    uint8_t age;
    int16_t spread;
    if (pop_age == 0) return;
    age = pop_age--;
    x = view_x((int16_t)((pop_wx - world.scroll) >> 6));
    y = view_y((int16_t)(pop_y >> 6));
    if (age > 10) {
        add_block((uint8_t)TILE_RING, (int16_t)(x - 12), (int16_t)(y - 21), 3, 2, 2, 0);
    } else if (age > 5) {
        add_block((uint8_t)TILE_POP, (int16_t)(x - 8), (int16_t)(y - 8), 2, 1, 2, 0);
    } else {
        spread = (int16_t)((6 - age) << 1);
        add_block((uint8_t)(TILE_POP + 4), (int16_t)(x - 8 - spread), (int16_t)(y - 8), 1, 1, 2, 0);
        add_block((uint8_t)(TILE_POP + 6), (int16_t)(x + spread), (int16_t)(y - 8), 1, 1, 2, 0);
    }
}

static void prepare_sprites(void) {
    int16_t feet_x = view_x((int16_t)(world.beaver.x >> 6));
    int16_t feet_y = view_y((int16_t)(world.beaver.y >> 6));
    uint8_t playing = world.phase == PHASE_PLAY;
    uint8_t show_board = 1;
    uint8_t i;
    uint8_t drawn;
    oam_n = 0;
    memset(line_n, 0, sizeof line_n);

    /* A few pixels of rise and fall on the title, matching the Advance build. */
    if (world.phase == PHASE_TITLE) {
        uint8_t t = (uint8_t)(world.tick % 24);
        feet_y = (int16_t)(feet_y + (t < 12 ? (t >> 2) : ((24 - t) >> 2)));
    }

    /* The meter arrow sits on the top rows, clear of the actors, so it takes its slot first. */
    if (playing && world.meter >= 0) {
        add_block((uint8_t)TILE_ARROW, (int16_t)(28 + world.meter), 2, 1, 1, 5, 0);
    }

    /* OAM order is who keeps their pixels when a line is full: the beaver, then rings and
     * gulls, then the board. The board is four sprites wide and used to be queued first,
     * so a gull or a ring crossing the rider was the one the hardware cut in half.
     * Invuln blinking is play-only. After a death the counter is left where it stopped,
     * and the title would otherwise hide the beaver for good. */
    if (!(playing && world.invuln && ((world.invuln >> 2) & 1))) {
        uint8_t mirror = 0;
        uint8_t frame = beaver_pose(&mirror);
        int16_t body_y = feet_y;
        uint8_t show_body = 1;
        if (playing && world.banner == BANNER_WIPEOUT && world.stun) {
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
        /* The 32x32 frame's bottom sits on the feet. The beaver is queued first so it stays
         * in front of the board where the belly overlaps the deck. */
        if (frame == FRAME_FLIP || frame == FRAME_FLIP90) show_board = 0;
        if (show_body) {
            add_block((uint8_t)(TILE_BEAVER + frame * FRAME_TILES), (int16_t)(feet_x - 16), (int16_t)(body_y - 31), 4,
                      2, mirror, 0);
        }
    } else {
        show_board = 0;
    }

    /* Gulls, pickups and spray belong to a run. Game over leaves them in the world, and the
     * title that follows would otherwise keep drawing a heart over an empty sea. Gulls go
     * before the pickups: a gull is always drawn, and a ring or heart that would push a
     * line past ten sprites is skipped whole instead of losing a side. */
    if (playing) {
        drawn = 0;
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
            drawn = (uint8_t)(drawn + add_block((uint8_t)(TILE_GULL + frame * 8), (int16_t)(x - 15),
                                                (int16_t)(y - 8), 4, 1, 1, 0));
        }

        drawn = 0;
        for (i = 0; i < 3 && drawn < 2; i++) {
            int16_t x;
            int16_t y;
            if (!world.ring_present[i]) continue;
            x = view_x((int16_t)(world.rings[i].x >> 6));
            y = view_y((int16_t)(world.rings[i].y >> 6));
            if (x < -24 || x > 184) continue;
            /* The sheet is shifted down nine rows, so the top is nine pixels higher and the
             * blank tail stays above the ring instead of covering the beaver's head. */
            drawn = (uint8_t)(drawn + add_block((uint8_t)TILE_RING, (int16_t)(x - 12), (int16_t)(y - 21), 3, 2, 2, 1));
        }

        if (world.heart_present) {
            int16_t x = (int16_t)(world.heart.x >> 6);
            int16_t y = (int16_t)(world.heart.y >> 6);
            add_block((uint8_t)TILE_HEART, (int16_t)(view_x(x) - 8), (int16_t)(view_y(y) - 8), 2, 1, 3, 1);
        }
    }

    draw_ring_pop();

    if (show_board) {
        add_block((uint8_t)(TILE_BOARD + board_for_path()), (int16_t)(feet_x - 16),
                  (int16_t)(feet_y - BOARD_TOP_ROW), 4, 1, 0, 2);
    }

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

/* want_hud (20 wide) into the shadow HUD rows with their palette bytes. */
static void hud_commit(void) __naked {
    __asm
        ld de, #_want_hud
        ld hl, #(SHADOW_TILES + SHADOW_HUD)
        ld b, #2
hc_row:
        ld c, #20
hc_col:
        ld a, (de)
        inc de
        ld (hl), a
        push hl
        ld h, #(ATTR_LUT >> 8)
        ld l, a
        ld a, (hl)
        pop hl
        set 1, h
        ld (hl), a
        res 1, h
        inc hl
        dec c
        jr NZ, hc_col
        ld a, l
        add a, #12
        ld l, a
        dec b
        jr NZ, hc_row
        ret
    __endasm;
}

static void banner_text(char *out) {
    out[0] = 0;
    if (world.banner == BANNER_WIPEOUT) {
        strcpy(out, "WIPEOUT");
    } else if (world.banner == BANNER_HEART) {
        strcpy(out, "HALF HEART");
    } else if (world.banner == BANNER_TRICK) {
        const char *kind = world.banner_air ? "AIR " : "FLIP ";
        uint8_t n = 0;
        uint16_t points = world.banner_points;
        char num[6];
        uint8_t nd = 0;
        while (*kind) out[n++] = *kind++;
        if (points == 0) num[nd++] = '0';
        while (points) {
            num[nd++] = (char)('0' + points % 10);
            points /= 10;
        }
        while (nd) out[n++] = num[--nd];
        out[n++] = ' ';
        out[n++] = 'X';
        out[n++] = (char)('0' + world.banner_mult);
        out[n] = 0;
    } else if (world.combo >= 2 && world.combo_timer > 90) {
        out[0] = 'C';
        out[1] = 'O';
        out[2] = 'M';
        out[3] = 'B';
        out[4] = 'O';
        out[5] = ' ';
        out[6] = 'X';
        out[7] = (char)('0' + world.combo);
        out[8] = 0;
    }
}

static void prepare_hud(void) {
    char text[12];
    uint8_t i;
    uint8_t blink = (uint8_t)((world.tick >> 5) & 1);
    if (world.phase == PHASE_TITLE) {
        center_text(0, "BEAVER SURF");
        if (blink) center_text(1, "PRESS START");
        else center_label_num(1, "BEST", world.best);
        return;
    }
    if (world.phase == PHASE_OVER) {
        center_text(0, blink ? "PRESS START" : "WIPED OUT");
        if (blink) {
            if (world.score && world.score >= world.best) center_text(1, "NEW BEST");
            else center_label_num(1, "BEST", world.best);
        } else {
            center_label_num(1, "SCORE", world.score);
        }
        return;
    }

    fill_row(0, TILE_HUD);
    fill_row(1, TILE_HUD);
    for (i = 0; i < 3; i++) {
        uint8_t halves = (uint8_t)(i * 2);
        if (world.hearts >= halves + 2) want_hud[0][i] = TILE_HEART_FULL;
        else if (world.hearts == halves + 1) want_hud[0][i] = TILE_HEART_HALF;
        else want_hud[0][i] = TILE_HEART_EMPTY;
    }
    write_score(world.score);
    banner_text(text);
    if (pause_show) {
        /* Paused: the blinking word takes the banner's place. */
        if (pause_show == 2) write_text(0, 6, "PAUSE");
    } else if (text[0]) {
        uint8_t len = 0;
        while (text[len]) len++;
        write_text(0, (uint8_t)(3 + (11 - len) / 2), text);
    }
    for (i = 0; i < 12; i++) {
        want_hud[1][4 + i] = (i >= 4 && i < 8) ? TILE_METER_GREEN : TILE_METER_DARK;
    }
}

typedef char heights_at_117[(offsetof(World, heights) == 117) ? 1 : -1];

/* s * 21 for s in 0..23. Kept as a table so the waterline loop does not call a multiply. */
static const uint16_t MUL21[24] = {0,   21,  42,  63,  84,  105, 126, 147, 168, 189, 210, 231,
                                    252, 273, 294, 315, 336, 357, 378, 399, 420, 441, 462, 483};

static uint8_t clamp_y(int16_t y) {
    if (y < 0) return 0;
    if (y > 200) return 200;
    return (uint8_t)y;
}


static uint8_t sl_left, sl_step, sl_front;
static uint8_t *sl_out;

/* DE = world pixel (0..511), BC = destination. sl_left samples, sl_step pixels apart, through
 * the same table lerp as the rules (le_core in wave.c): h0 + ((h1 - h0) * frac >> 3). Heights
 * stay in 0..16383, so the pixel row is the byte straddling bits 6..13.
 * sl_front = 0: screen y = row - 24, clamped to 0..200 (the surf line).
 * sl_front = 1: screen y = 116 + (row - 112) * front_num / 16, the bottom lane. The caller
 * passes (world_x >> 1) + FRONT_PHASE so this matches front_at.
 * The height table is world+117; heights_at_117 fails the build if the struct moves. */
static void sample_line(uint16_t px, uint8_t *out) __naked {
    __asm
        ld a, c
        ld (#_sl_out), a
        ld a, b
        ld (#_sl_out + 1), a
sl_loop:
        ld a, e
        and a, #0x07
        ld b, a
        ld a, d
        rra
        ld a, e
        rra
        srl a
        srl a
        ld c, a
        push de
        add a, a
        ld l, a
        ld h, #0x00
        ld de, #(_world + 117)
        add hl, de
        ld a, (hl+)
        ld e, a
        ld d, (hl)
        ld a, c
        inc a
        and a, #0x3f
        add a, a
        ld l, a
        ld h, #0x00
        push de
        ld de, #(_world + 117)
        add hl, de
        pop de
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld a, l
        sub a, e
        ld l, a
        ld a, h
        sbc a, d
        ld h, a
        push de
        ld d, h
        ld e, l
        ld hl, #0x0000
        bit 0, b
        jr Z, sl_f1
        add hl, de
sl_f1:
        sla e
        rl d
        bit 1, b
        jr Z, sl_f2
        add hl, de
sl_f2:
        sla e
        rl d
        bit 2, b
        jr Z, sl_f3
        add hl, de
sl_f3:
        sra h
        rr l
        sra h
        rr l
        sra h
        rr l
        pop de
        add hl, de
        ld a, l
        rlca
        rlca
        and a, #0x03
        ld c, a
        ld a, h
        rlca
        rlca
        and a, #0xfc
        or a, c
        ld hl, #_sl_front
        bit 0, (hl)
        jr NZ, sl_fr
        sub a, #24
        jr C, sl_zero
        cp a, #201
        jr C, sl_store
        ld a, #200
        jr sl_store
sl_zero:
        xor a
        jr sl_store
sl_fr:
        sub a, #112
        ld l, a
        rlca
        sbc a, a
        ld h, a
        ld e, l
        ld d, h
        add hl, hl
        add hl, hl
        add hl, de
        ld a, (#_front_num)
        sub a, #5
        jr Z, sl_scaled
sl_more:
        add hl, de
        dec a
        jr NZ, sl_more
sl_scaled:
        sra h
        rr l
        sra h
        rr l
        sra h
        rr l
        sra h
        rr l
        ld a, l
        add a, #116
        jr sl_store
sl_store:
        ld hl, #_sl_out
        ld c, a
        ld a, (hl+)
        ld h, (hl)
        ld l, a
        ld (hl), c
        inc hl
        ld a, l
        ld (#_sl_out), a
        ld a, h
        ld (#_sl_out + 1), a
        pop de
        ld hl, #_sl_step
        ld a, e
        add a, (hl)
        ld e, a
        ld a, d
        adc a, #0x00
        and a, #0x01
        ld d, a
        ld hl, #_sl_left
        dec (hl)
        jp NZ, sl_loop
        ret
    __endasm;
}

/* 21 column edges of the surf line, 12 world pixels apart (the 2/3 view). */
static void sample_heights(uint16_t base) {
    sl_left = 21;
    sl_step = 12;
    sl_front = 0;
    sample_line(base, hy);
}

/* Bottom lane, no parallax: front_at at the same world x as the surf line. Samples land on
 * every other column (24 world px is 12 px in the half-speed table) and the gaps are averaged. */
static void sample_front(uint16_t base) {
    uint8_t k;
    uint16_t sample = (uint16_t)(((base >> 1) + FRONT_PHASE) & 511);
    sl_left = 11;
    sl_step = 12;
    sl_front = 1;
    sample_line(sample, fy_half);
    for (k = 0; k < 10; k++) {
        uint8_t a = fy_half[k];
        uint8_t b = fy_half[k + 1];
        fy[k << 1] = a;
        fy[(k << 1) + 1] = (uint8_t)(((uint16_t)a + b) >> 1);
    }
    fy[20] = fy_half[10];
}

static uint8_t p_col, p_front, p_spray, p_span, p_sky, p_wat, p_skytop, p_wattop, hy_col;
/* Line tops of the rows the surf pass painted this column, [p_band_lo, p_band_hi), and the
 * row index of p_band_hi. */
static uint8_t p_band_lo, p_band_hi, p_brow_hi;
/* Front pass scratch: first row to visit, and the line top of last frame's first deep row. */
static uint8_t p_fstart, p_fhi_old;
static uint16_t p_lut;
/* Per column, the rows that held sky-with-spray, ramps or anything else not fixed by row
 * last frame: [pb_lo, pb_hi). Only the union with the new band is repainted, since sky
 * stays sky and the water gradient is fixed by row. */
static uint8_t pb_lo[20], pb_hi[20];
/* Per column, the bottom lane's rows last frame: ramps from fb_lo, near face from fb_hi down.
 * Rows above fb_lo that the lane has left are put back; rows under fb_hi stay as they are
 * unless the surf pass wrote over them this frame. */
static uint8_t fb_lo[20], fb_hi[20];

/* Repaints the 20 columns of the shadow wave band.
 *
 * Per column: sky rows above the line, a spray tile in the last sky row on steep slopes,
 * ramp tiles where the line crosses and in the row under it (the dashed echoes hang there),
 * then a depth gradient fixed by row below. Only rows in the union of last frame's and this
 * frame's ramp band are rewritten (pb_lo / pb_hi).
 * The bottom lane is then painted over the same rows: front ramps where its line crosses, the
 * near face below it, and above it the surf pass's work is left alone where that pass ran this
 * frame or put back to far sea / gradient where it did not (the lane may have covered it).
 * This pass is incremental too (fb_lo / fb_hi): it visits the ramp rows, the rows the lane
 * has left since last frame, and the near-face rows the surf pass wrote over.
 * Ramp ids come from RAMP_LUT / FRONT_LUT rows chosen once per column (p_lut).
 * Registers through the row loops: HL cell pointer, E row top, C y0, B rows left. */
static void paint_all(void) __naked {
    __asm
        ld c, #0
pa_col:
        ld b, #0
        ld hl, #_hy
        add hl, bc
        ld a, (hl+)
        ld e, a
        ld (#_hy_col), a
        ld d, (hl)
        ld hl, #_fy
        add hl, bc
        ld a, (hl)
        ld (#_p_front), a
        ld a, c
        ld (#_p_col), a
        ld a, d
        sub a, e
        jr NC, pa_down
        cpl
        inc a
        cp a, #(RAMP_DY_MAX + 1)
        jr C, pa_up_ok
        ld a, #RAMP_DY_MAX
pa_up_ok:
        ld (#_p_span), a
        cpl
        inc a
        jr pa_dy
pa_down:
        cp a, #(RAMP_DY_MAX + 1)
        jr C, pa_dy_ok
        ld a, #RAMP_DY_MAX
pa_dy_ok:
        ld (#_p_span), a
pa_dy:
        ld d, a
        push de
        add a, #(0 - RAMP_DY_MIN)
        ld l, a
        ld h, #0x00
        add hl, hl
        add hl, hl
        add hl, hl
        ld e, l
        ld d, h
        add hl, hl
        add hl, de
        ld de, #_RAMP_LUT
        add hl, de
        ld a, l
        ld (#_p_lut), a
        ld a, h
        ld (#_p_lut + 1), a
        pop de
        ld a, d
        add a, e
        cp a, e
        jr C, pa_rise
        ld l, e
        ld h, a
        jr pa_edges
pa_rise:
        ld l, a
        ld h, e
pa_edges:
        ld a, l
        sub a, #81
        jr C, pa_sky0
        srl a
        srl a
        srl a
        inc a
        cp a, #(WAVE_ROWS + 1)
        jr C, pa_skydone
        ld a, #WAVE_ROWS
        jr pa_skydone
pa_sky0:
        xor a
pa_skydone:
        ld b, a
        ld (#_p_sky), a
        ; Ramp rows run one past the row holding the lower edge, for the echoes under it.
        ld a, h
        sub a, #72
        jr C, pa_wat0
        srl a
        srl a
        srl a
        inc a
        inc a
        cp a, #(WAVE_ROWS + 1)
        jr C, pa_watdone
        ld a, #WAVE_ROWS
        jr pa_watdone
pa_wat0:
        ld a, #1
pa_watdone:
        cp a, b
        jr NC, pa_watok
        ld a, b
pa_watok:
        ld (#_p_wat), a
        add a, a
        add a, a
        add a, a
        add a, #72
        ld (#_p_wattop), a
        ld a, b
        add a, a
        add a, a
        add a, a
        add a, #72
        ld (#_p_skytop), a
        ld a, (#_p_span)
        cp a, #3
        jr C, pa_band
        ld a, b
        or a
        jr Z, pa_band
        dec b
pa_band:
        ld a, (#_p_col)
        ld e, a
        ld d, #0x00
        ld hl, #_pb_lo
        add hl, de
        ld a, (hl)
        ld (hl), b
        cp a, b
        jr C, pa_lo_ok
        ld a, b
pa_lo_ok:
        ld c, a
        ld hl, #_pb_hi
        add hl, de
        ld a, (#_p_wat)
        ld b, a
        ld a, (hl)
        ld (hl), b
        cp a, b
        jr NC, pa_hi_ok
        ld a, b
pa_hi_ok:
        ; Band rows [c, a) as line tops, for the front pass.
        ld b, a
        ld (#_p_brow_hi), a
        ld a, c
        add a, a
        add a, a
        add a, a
        add a, #72
        ld (#_p_band_lo), a
        ld e, a
        ld a, b
        add a, a
        add a, a
        add a, a
        add a, #72
        ld (#_p_band_hi), a
        ld a, b
        sub a, c
        jp Z, pa_front
        ld b, a
        ; HL = SHADOW_TILES + SHADOW_WAVE + col + 32 * c; nine rows cross a page.
        ld a, c
        add a, a
        add a, a
        add a, a
        ld l, a
        ld h, #0x00
        add hl, hl
        add hl, hl
        ld a, (#_p_col)
        add a, #SHADOW_WAVE
        add a, l
        ld l, a
        ld a, h
        adc a, #(SHADOW_TILES >> 8)
        ld h, a
        ld a, (#_hy_col)
        ld c, a
pa_rloop:
        ld a, (#_p_skytop)
        cp a, e
        jr Z, pa_notsky
        jr C, pa_notsky
        sub a, #8
        cp a, e
        jr NZ, pa_sky
        ld a, (#_p_span)
        cp a, #3
        jr C, pa_sky
        ld a, (#_p_spray)
        ld d, a
        ld a, (#_p_col)
        add a, d
        and a, #0x01
        add a, #TILE_SPRAY
        ld (hl), a
        set 1, h
        ld (hl), #0x0f
        jr pa_rnext
pa_sky:
        ; Above the surf line the far sea of the island band carries on (TILE_FAR_BODY).
        ld (hl), #63
        set 1, h
        ld (hl), #0x0b
        jr pa_rnext
pa_notsky:
        ld a, (#_p_wattop)
        cp a, e
        jr Z, pa_water
        jr C, pa_water
        ld a, c
        add a, #8
        sub a, e
        cp a, #RAMP_SUB_COUNT
        jr C, pa_lut
        bit 7, a
        jr NZ, pa_face
        ld (hl), #63
        set 1, h
        ld (hl), #0x0b
        jr pa_rnext
pa_lut:
        push hl
        ld hl, #_p_lut
        add a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        jr NC, pa_lut_ok
        inc h
pa_lut_ok:
        ld a, (hl)
        pop hl
        cp a, #TILE_RAMP1_COUNT
        jr NC, pa_ramp0
        add a, #TILE_RAMP
        ld (hl), a
        set 1, h
        ld (hl), #0x0f
        jr pa_rnext
pa_ramp0:
        add a, #(TILE_RAMP0 - TILE_RAMP1_COUNT)
        ld (hl), a
        set 1, h
        ld (hl), #0x07
        jr pa_rnext
pa_face:
        ld (hl), #64
        set 1, h
        ld (hl), #0x0e
        jr pa_rnext
pa_water:
        ld a, e
        ld d, #64
        cp a, #96
        jr C, pa_wtile
        ld d, #65
        cp a, #104
        jr C, pa_wtile
        ld d, #66
pa_wtile:
        ld (hl), d
        set 1, h
        ld (hl), #0x0e
pa_rnext:
        res 1, h
        ld a, l
        add a, #32
        ld l, a
        jr NC, pa_rsame
        inc h
pa_rsame:
        ld a, e
        add a, #8
        ld e, a
        dec b
        jp NZ, pa_rloop
pa_front:
        ld a, (#_p_col)
        ld c, a
        ld b, #0x00
        ld hl, #_fy
        add hl, bc
        ld a, (hl+)
        ld (#_p_front), a
        ld e, a
        ld a, (hl)
        sub a, e
        jr NC, pa_fdown
        cp a, #(256 + FRONT_DY_MIN)
        jr NC, pa_fdy
        ld a, #(256 + FRONT_DY_MIN)
        jr pa_fdy
pa_fdown:
        cp a, #(FRONT_DY_MAX + 1)
        jr C, pa_fdy
        ld a, #FRONT_DY_MAX
pa_fdy:
        add a, #(0 - FRONT_DY_MIN)
        ld l, a
        ld h, #0x00
        add hl, hl
        add hl, hl
        add hl, hl
        ld e, l
        ld d, h
        add hl, hl
        add hl, de
        ld de, #_FRONT_LUT
        add hl, de
        ld a, l
        ld (#_p_lut), a
        ld a, h
        ld (#_p_lut + 1), a
        ; Ramp rows are those with sub 0..23: line tops from y-15 to y+8, so rows
        ; (y-80)>>3 up to but not including (y-56)>>3, both clipped to 0..WAVE_ROWS.
        ld a, (#_p_front)
        sub a, #80
        jr NC, pf_lo_pos
        xor a
pf_lo_pos:
        srl a
        srl a
        srl a
        cp a, #WAVE_ROWS
        jr C, pf_lo_ok
        ld a, #WAVE_ROWS
pf_lo_ok:
        ld d, a
        ld a, (#_p_front)
        sub a, #56
        jr NC, pf_hi_pos
        xor a
pf_hi_pos:
        srl a
        srl a
        srl a
        cp a, #WAVE_ROWS
        jr C, pf_hi_ok
        ld a, #WAVE_ROWS
pf_hi_ok:
        ld e, a
        ; Visit from the lower of last frame's and this frame's first ramp row, down to the
        ; highest of this frame's first deep row, last frame's, and the end of the surf band.
        ld a, (#_p_col)
        ld c, a
        ld b, #0x00
        ld hl, #_fb_lo
        add hl, bc
        ld a, (hl)
        ld (hl), d
        cp a, d
        jr C, pf_start_ok
        ld a, d
pf_start_ok:
        ld (#_p_fstart), a
        ld hl, #_fb_hi
        add hl, bc
        ld a, (hl)
        ld (hl), e
        ld b, a
        add a, a
        add a, a
        add a, a
        add a, #72
        ld (#_p_fhi_old), a
        ld a, b
        cp a, e
        jr NC, pf_end_old
        ld a, e
pf_end_old:
        ld b, a
        ld a, (#_p_brow_hi)
        cp a, b
        jr NC, pf_end_ok
        ld a, b
pf_end_ok:
        ld b, a
        ld a, (#_p_fstart)
        ld c, a
        ld a, b
        sub a, c
        jp Z, pa_fdone
        ld b, a
        ; HL = SHADOW_TILES + SHADOW_WAVE + col + 32 * start row, E = its line top.
        ld a, c
        add a, a
        add a, a
        add a, a
        ld e, a
        add a, #72
        ld (#_p_fstart), a
        ld l, e
        ld h, #0x00
        add hl, hl
        add hl, hl
        ld a, (#_p_col)
        add a, #SHADOW_WAVE
        add a, l
        ld l, a
        ld a, h
        adc a, #(SHADOW_TILES >> 8)
        ld h, a
        ld a, (#_p_fstart)
        ld e, a
        ld a, (#_p_front)
        ld c, a
pa_floop:
        ld a, c
        add a, #8
        sub a, e
        cp a, #RAMP_SUB_COUNT
        jr NC, pa_fnot
        push hl
        ld hl, #_p_lut
        add a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        jr NC, pa_flut_ok
        inc h
pa_flut_ok:
        ld a, (hl)
        pop hl
        add a, #TILE_RAMP0
        ld (hl), a
        set 1, h
        ld (hl), #0x06
        jr pa_fnext
pa_fnot:
        cp a, #0x80
        jr C, pa_fabove
        ; Line above this cell (sub negative, wrapped high): the shadowed near face. Already
        ; there from last frame unless the surf pass wrote the cell this frame.
        ld a, (#_p_fhi_old)
        cp a, e
        jr Z, pa_fdeep_chk
        jr C, pa_fdeep_chk
        jr pa_fdeep
pa_fdeep_chk:
        ld a, (#_p_band_lo)
        cp a, e
        jr Z, pa_fdeep_band
        jr NC, pa_fnext
pa_fdeep_band:
        ld a, (#_p_band_hi)
        cp a, e
        jr Z, pa_fnext
        jr C, pa_fnext
pa_fdeep:
        ld (hl), #TILE_FRONT_DEEP
        set 1, h
        ld (hl), #0x0e
        jr pa_fnext
pa_fabove:
        ; Line more than a tile below: this cell belongs to the surf wave. Inside the band the
        ; surf pass just painted it; outside, it is far sea above the band or gradient below,
        ; and is written in case the lane covered it last frame.
        ld a, (#_p_band_lo)
        cp a, e
        jr Z, pa_fnext
        jr NC, pa_fsky
        ld a, (#_p_band_hi)
        cp a, e
        jr Z, pa_fgrad
        jr NC, pa_fnext
pa_fgrad:
        ld a, e
        ld d, #64
        cp a, #96
        jr C, pa_fgput
        ld d, #65
        cp a, #104
        jr C, pa_fgput
        ld d, #66
pa_fgput:
        ld (hl), d
        set 1, h
        ld (hl), #0x0e
        jr pa_fnext
pa_fsky:
        ld (hl), #63
        set 1, h
        ld (hl), #0x0b
pa_fnext:
        res 1, h
        ld a, l
        add a, #32
        ld l, a
        jr NC, pa_fsame
        inc h
pa_fsame:
        ld a, e
        add a, #8
        ld e, a
        dec b
        jp NZ, pa_floop
pa_fdone:
        ld a, (#_p_col)
        inc a
        ld c, a
        cp a, #20
        jp NZ, pa_col
        ret
    __endasm;
}

static void prepare_wave(uint16_t pixels) {
    uint16_t base = (uint16_t)(pixels & 511);
    p_spray = (uint8_t)((world.tick >> 2) & 1);
    sample_heights(base);
    sample_front(base);
    paint_all();
}

static uint32_t isl_pixels;
static uint8_t isl_rem, isl_x, isl_valid;
/* Scene column (0..SCENE_COLS-1) under map column isl_x >> 3, and whether the whole strip
 * needs rewriting. */
static uint8_t isl_scene, isl_rebuild;

/* The far shore repeats every SCENE_COLS columns of the island band (one column is 8 band
 * pixels, 24 surf pixels): open water with islands and ships, then a long green country with
 * hills, a skyline and the tower, then water again. */
#define SCENE_COLS 128
enum {
    K_SEA, K_ISL_L, K_ISL_R, K_SHIP_L, K_SHIP_R, K_LAND, K_LAND_L, K_LAND_R,
    K_HILL_L, K_HILL_R, K_TOWER, K_CITY_A, K_CITY_B, K_KINDS
};
#define HAZE (TILE_SKY0 + 1)
/* Map rows 5..8 for each column kind. */
static const uint8_t KIND_TILES[K_KINDS][4] = {
    {HAZE, TILE_FAR_TOP, TILE_FAR_BODY, TILE_FAR_BODY},
    {HAZE, TILE_ISLAND, TILE_ISLAND + 2, TILE_FAR_BODY},
    {HAZE, TILE_ISLAND + 1, TILE_ISLAND + 3, TILE_FAR_BODY},
    {HAZE, TILE_SHIP, TILE_SHIP + 2, TILE_FAR_BODY},
    {HAZE, TILE_SHIP + 1, TILE_SHIP + 3, TILE_FAR_BODY},
    {HAZE, TILE_FAR_TOP, SC_LAND_TOP, SC_LAND},
    {HAZE, TILE_FAR_TOP, SC_LAND_TOP_L, SC_LAND_L},
    {HAZE, TILE_FAR_TOP, SC_LAND_TOP_R, SC_LAND_R},
    {HAZE, SC_HILL6_L, SC_HILL7_L, SC_LAND},
    {HAZE, SC_HILL6_R, SC_HILL7_R, SC_LAND},
    {SC_SPIRE, SC_TOWER_MID, SC_TOWER_BASE, SC_LAND},
    {HAZE, TILE_FAR_TOP, SC_CITY_A, SC_LAND},
    {HAZE, TILE_FAR_TOP, SC_CITY_B, SC_LAND},
};
/* Attributes: bank-1 tiles carry 0x08, the scenery lives in bank 0. */
#define A_SKY1 (0x08 | PAL_SKY)
#define A_ISL1 (0x08 | PAL_ISLAND)
#define A_SHIP1 (0x08 | PAL_SHIP)
#define A_SKY0 PAL_SKY
#define A_ISL0 PAL_ISLAND
static const uint8_t KIND_ATTRS[K_KINDS][4] = {
    {A_SKY1, A_ISL1, A_ISL1, A_ISL1},
    {A_SKY1, A_ISL1, A_ISL1, A_ISL1},
    {A_SKY1, A_ISL1, A_ISL1, A_ISL1},
    {A_SKY1, A_SHIP1, A_SHIP1, A_ISL1},
    {A_SKY1, A_SHIP1, A_SHIP1, A_ISL1},
    {A_SKY1, A_ISL1, A_ISL0, A_ISL0},
    {A_SKY1, A_ISL1, A_ISL0, A_ISL0},
    {A_SKY1, A_ISL1, A_ISL0, A_ISL0},
    {A_SKY1, A_ISL0, A_ISL0, A_ISL0},
    {A_SKY1, A_ISL0, A_ISL0, A_ISL0},
    {A_SKY0, A_ISL0, A_ISL0, A_ISL0},
    {A_SKY1, A_ISL1, A_ISL0, A_ISL0},
    {A_SKY1, A_ISL1, A_ISL0, A_ISL0},
};

/* Column kind for a scene column. */
static uint8_t scene_kind(uint8_t col) {
    if (col >= 64 && col <= 105) {
        switch (col) {
        case 64: return K_LAND_L;
        case 105: return K_LAND_R;
        case 70: case 94: return K_HILL_L;
        case 71: case 95: return K_HILL_R;
        case 81: case 84: return K_CITY_B;
        case 82: case 85: return K_CITY_A;
        case 83: return K_TOWER;
        default: return K_LAND;
        }
    }
    switch (col) {
    case 3: case 20: case 44: case 122: return K_ISL_L;
    case 4: case 21: case 45: case 123: return K_ISL_R;
    case 10: case 30: case 54: case 112: return K_SHIP_L;
    case 11: case 31: case 55: case 113: return K_SHIP_R;
    default: return K_SEA;
    }
}

/* Writes scene column `col` into its map column of the shore shadow rows. */
static void write_scene_column(uint8_t col) {
    uint8_t kind = scene_kind(col);
    volatile uint8_t *tiles = (volatile uint8_t *)(SHADOW_TILES + SHADOW_SHORE + (col & 31));
    volatile uint8_t *attrs = (volatile uint8_t *)(SHADOW_ATTRS + SHADOW_SHORE + (col & 31));
    uint8_t row;
    for (row = 0; row < 4; row++) {
        tiles[row * 32] = KIND_TILES[kind][row];
        attrs[row * 32] = KIND_ATTRS[kind][row];
    }
}

/* Visible map columns are c..c+20; the eleven beyond are written ahead of time. Column c+21
 * is the one that just slid off the left, so it is free to take the scene column 21 ahead. */
#define SCENE_AHEAD 21

/* (n / 3) mod 256 depends only on n mod 768, and 2^16 ≡ 256 (mod 768). Also sets the scene
 * column: 196608 is a multiple of 3072 = 24 * SCENE_COLS. */
static uint8_t island_scroll(uint32_t pixels) {
    uint16_t hi = (uint16_t)((uint16_t)(pixels >> 16) % 768u);
    uint32_t acc = ((uint32_t)hi << 8) + (uint16_t)pixels;
    uint16_t scene;
    while (acc >= 196608UL) acc -= 196608UL;
    while (acc >= 49152UL) acc -= 49152UL;
    while (acc >= 3072UL) acc -= 3072UL;
    scene = (uint16_t)acc;
    isl_scene = (uint8_t)(scene / 24u);
    scene -= isl_scene * 24u;
    isl_rem = (uint8_t)(scene % 3u);
    return (uint8_t)((isl_scene << 3) + (uint8_t)(scene / 3u));
}

/* The scroll only grows a few pixels a frame, so the island offset is carried forward; a
 * reset (world_new) or an unexpected jump recomputes it from scratch and rewrites the strip. */
static uint8_t island_x(uint32_t pixels) {
    if (!isl_valid || pixels < isl_pixels || pixels - isl_pixels > 200) {
        isl_x = island_scroll(pixels);
        isl_valid = 1;
        isl_rebuild = 1;
    } else {
        isl_rem = (uint8_t)(isl_rem + (uint8_t)(pixels - isl_pixels));
        while (isl_rem >= 3) {
            isl_rem -= 3;
            isl_x++;
            if ((isl_x & 7) == 0) {
                isl_scene++;
                if (isl_scene == SCENE_COLS) isl_scene = 0;
                write_scene_column((uint8_t)((isl_scene + SCENE_AHEAD) % SCENE_COLS));
            }
        }
    }
    isl_pixels = pixels;
    return isl_x;
}

static void shore_rebuild(void) {
    uint8_t i;
    uint8_t col = (uint8_t)((isl_scene + SCENE_COLS - 11) % SCENE_COLS);
    for (i = 0; i < 32; i++) {
        write_scene_column(col);
        if (++col == SCENE_COLS) col = 0;
    }
    isl_rebuild = 0;
}

void render_prepare(void) {
    uint32_t pixels = (uint32_t)(world.scroll >> 6);
    scx_sky = (uint8_t)(pixels >> 3);
    scx_island = island_x(pixels);
    if (isl_rebuild) shore_rebuild();
    if (hud_changed()) {
        prepare_hud();
        hud_commit();
    } else if (world.phase == PHASE_PLAY && world.score != sd_value) {
        write_score(world.score);
        score_commit();
    }
    prepare_wave((uint16_t)pixels);
    prepare_sprites();
}


/* One general-purpose DMA: `blocks` x 16 bytes. The CPU stalls until it is done. */
#define GDMA(src, dst, blocks)                      \
    do {                                            \
        HDMA1_REG = (uint8_t)((src) >> 8);          \
        HDMA2_REG = (uint8_t)(src);                 \
        HDMA3_REG = (uint8_t)((dst) >> 8);          \
        HDMA4_REG = (uint8_t)(dst);                 \
        HDMA5_REG = (uint8_t)((blocks) - 1);        \
    } while (0)

/* Call at the start of vblank (or with the LCD off). 60 DMA blocks and one OAM DMA take about
 * 1200 double-speed M-cycles, inside the ~2280 the blank offers, so nothing is ever written
 * while the LCD owns VRAM. */
void render_present(void) {
    VBK_REG = 0;
    GDMA(SHADOW_TILES + SHADOW_HUD, 0x9800, 4);
    GDMA(SHADOW_TILES + SHADOW_WAVE, 0x9920, 2 * WAVE_ROWS);
    GDMA(SHADOW_TILES + SHADOW_SHORE, 0x98A0, 8);
    VBK_REG = 1;
    GDMA(SHADOW_ATTRS + SHADOW_HUD, 0x9800, 4);
    GDMA(SHADOW_ATTRS + SHADOW_WAVE, 0x9920, 2 * WAVE_ROWS);
    GDMA(SHADOW_ATTRS + SHADOW_SHORE, 0x98A0, 8);
    VBK_REG = 0;
    /* The page just prepared becomes the one GBDK's VBL handler DMAs every frame; copy it now
     * too so sprites and map land in the same blank. The other page is free to build on.
     * GBDK's C-callable refresh_OAM() always copies from 0xC000, so call the HRAM routine,
     * which reads _shadow_OAM_base, directly. */
    _shadow_OAM_base = (uint8_t)((uint16_t)oam_back >> 8);
    __asm__("call .refresh_OAM");
    oam_back = (uint8_t *)((uint16_t)oam_back == OAM_PAGE_A ? OAM_PAGE_B : OAM_PAGE_A);
}

/* Display is off, so VRAM can be filled directly. GBDK's tile copy ignores the VRAM bank bit. */
static void vram_copy(uint8_t vram_bank, uint16_t dest_tile, const uint8_t *src, uint16_t bytes) {
    uint8_t *dst = (uint8_t *)(0x8000u + dest_tile * 16u);
    VBK_REG = vram_bank;
    while (bytes--) *dst++ = *src++;
    VBK_REG = 0;
}

static void sprite_pals(void) {
    /* Six sprite palettes, colours 0 transparent. Kept here so the load does not depend on
     * which ROM bank the generated palette arrays landed in. */
    static const uint16_t pals[] = {
        RGB(0, 0, 0),  RGB(0, 0, 0),   RGB(12, 5, 1),  RGB(9, 2, 4),
        RGB(0, 0, 0),  RGB(6, 5, 4),   RGB(31, 31, 31), RGB(31, 20, 4),
        RGB(0, 0, 0),  RGB(17, 10, 1), RGB(31, 23, 4), RGB(31, 29, 17),
        RGB(0, 0, 0),  RGB(15, 2, 3),  RGB(29, 6, 7),  RGB(31, 31, 31),
        RGB(0, 0, 0),  RGB(2, 10, 18), RGB(22, 27, 30), RGB(31, 31, 31),
        RGB(0, 0, 0),  RGB(1, 4, 9),   RGB(29, 6, 7),  RGB(31, 31, 31),
    };
    const uint8_t *bytes = (const uint8_t *)pals;
    uint8_t i;
    OCPS_REG = 0x80;
    for (i = 0; i < 48; i++) OCPD_REG = bytes[i];
}

static void lcd_off(void) {
    /* The LCD only turns off cleanly during mode 0 or 1. Clearing the bit at any other time
     * leaves the screen running, and a raw VRAM copy then drops almost every write. */
    if (LCDC_REG & LCDCF_ON) {
        while (STAT_REG & 0x03) ;
        LCDC_REG &= (uint8_t)~LCDCF_ON;
    }
}

void render_init(void) {
    uint8_t x;
    uint16_t cell;
    uint8_t saved;
    oam_back = (uint8_t *)OAM_PAGE_A;
    memset(pb_lo, 0, sizeof(pb_lo));
    memset(pb_hi, WAVE_ROWS, sizeof(pb_hi));
    /* Nothing of the lane is on the shadow yet: every row counts as left behind. */
    memset(fb_lo, 0, sizeof(fb_lo));
    memset(fb_hi, WAVE_ROWS, sizeof(fb_hi));
    memset((void *)OAM_PAGE_A, 0, 160);
    memset((void *)OAM_PAGE_B, 0, 160);
    lcd_off();
    saved = CURRENT_BANK;
    SWITCH_ROM(BANK(bg));
    vram_copy(1, 0, bg_tiles, (uint16_t)bg_TILE_COUNT * 16u);
    vram_copy(0, TILE_BEAVER, beaver_tiles, (uint16_t)beaver_TILE_COUNT * 16u);
    vram_copy(0, TILE_BOARD, board_tiles, (uint16_t)board_TILE_COUNT * 16u);
    vram_copy(0, TILE_GULL, gull_tiles, (uint16_t)gull_TILE_COUNT * 16u);
    vram_copy(0, TILE_RING, ring_tiles, (uint16_t)ring_TILE_COUNT * 16u);
    vram_copy(0, TILE_HEART, heart_tiles, (uint16_t)heart_TILE_COUNT * 16u);
    vram_copy(0, TILE_SPLASH, splash_tiles, (uint16_t)splash_TILE_COUNT * 16u);
    vram_copy(0, TILE_ARROW, arrow_tiles, (uint16_t)arrow_TILE_COUNT * 16u);
    vram_copy(0, TILE_POP, pop_tiles, (uint16_t)pop_TILE_COUNT * 16u);
    vram_copy(0, TILE_RAMP0, bg0_tiles, (uint16_t)bg0_TILE_COUNT * 16u);
    set_bkg_palette(0, bg_PALETTE_COUNT, bg_palettes);
    sprite_pals();
    /* Page-aligned so the painter looks a tile's attribute byte up by loading L. */
    for (cell = 0; cell < TILE_COUNT; cell++) {
        ATTR_OF(cell) = (uint8_t)(bg_map_attributes[cell] | 0x08);
    }
    SWITCH_ROM(saved);

    for (x = 0; x < 32; x++) {
        put_bg(x, 2, TILE_SKY0);
        put_bg(x, 3, TILE_SKY0 + 2);
        put_bg(x, 4, TILE_SKY0 + 2);
    }
    stamp_block(1, 3, TILE_CLOUD);
    stamp_block(9, 3, TILE_CLOUD);
    stamp_block(22, 3, TILE_CLOUD);
    stamp_block(16, 3, TILE_SUN);

    /* Shadow maps: all 32 columns, so the invisible right-hand cells hold sane tiles too. The
     * shore rows are filled by the first render_prepare, which always rebuilds the strip. */
    for (cell = 0; cell < SHADOW_LEN; cell++) {
        uint8_t tile = cell < SHADOW_WAVE   ? TILE_HUD
                       : cell < SHADOW_SHORE ? TILE_ABYSS
                                             : TILE_FAR_BODY;
        ((volatile uint8_t *)SHADOW_TILES)[cell] = tile;
        ((volatile uint8_t *)SHADOW_ATTRS)[cell] = ATTR_OF(tile);
    }
    isl_valid = 0;

    /* Tile patterns live at 0x8000, so index 0 is the first pattern, not the signed 0x8800 set. */
    LCDC_REG = LCDCF_BG8000 | LCDCF_BG9800 | LCDCF_OBJ16 | LCDCF_OBJON | LCDCF_BGON;
}
