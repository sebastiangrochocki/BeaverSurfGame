#include "ramp.h"
#include "render.h"
#include "sprites.h"

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
#include "beach.h"
#include "actors.h"
#include "shop.h"
#include "race.h"
#include "gull.h"
#include "heart.h"
#include "pop.h"
#include "ring.h"
#include "rules.h"
#include "splash.h"
#include "tiles.h"
#include "progress_data.h"

#define TILE_BEAVER 0
#define TILE_BOARD (TILE_BEAVER + beaver_TILE_COUNT)
#define TILE_GULL (TILE_BOARD + board_TILE_COUNT)
#define TILE_RING (TILE_GULL + gull_TILE_COUNT)
#define TILE_HEART 172 /* 164-171 are reserved for the rival's independent deck cache. */
#define TILE_SPLASH (TILE_HEART + heart_TILE_COUNT)
#define TILE_ARROW (TILE_SPLASH + splash_TILE_COUNT)
#define TILE_POP (TILE_ARROW + arrow_TILE_COUNT)

/* Beaver sheet: seven 32x32 frames. Flip steps 180 and 270 reuse the upright and 90 degree
 * frames with both axes mirrored. Board sheet: flat, nose up, nose down. */
#define BOARD_FLAT 0
#define BOARD_UP 8
#define BOARD_DOWN 16
/* Body top is feet-31. Board top is body top+16: a 16px overlap rather than
 * stacking beneath the body. Its visible lower eight rows finish at the waterline. */
#define BOARD_TOP_ROW 15
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
/* The assembly painter multiplies LUT rows by 48 and adds 32 to each surface sample. */
typedef char ramp_layout_ok[(RAMP_SUB_COUNT == 48 && RAMP_SUB_MIN == -32) ? 1 : -1];

/* Map rows 9-17 (lines 72-143) hold both waves: the surf line with its sky above, and the
 * bottom lane painted over whatever of it the near water hides. */
#define WAVE_ROW 9
#define WAVE_ROWS 9
#define COLS 20

extern volatile uint8_t scx_sky;
extern volatile uint8_t scx_island;
extern volatile uint8_t scx_playfield;
extern volatile uint8_t scx_beach;
/* Prepared raster state belongs to the shadow map, not the frame still visible.
 * Commit both together in VBlank, especially when SCX wraps from 7 to 0. */
static uint8_t raster_sky, raster_island, raster_playfield, raster_beach;
#define DIRTY_HUD 1
#define DIRTY_WAVE 2
#define DIRTY_SHORE 4
#define DIRTY_FOOTER 8
static uint8_t prepared_bands;
static uint8_t shore_first,shore_count;

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

/* Bottom two rows share the existing wave DMA. Extra HUD patterns occupy the
 * unused end of bank 0, after scenery; they use the sky palette with opaque ink. */
#define PROGRESS_FONT (TILE_RAMP0 + bg0_TILE_COUNT)
#define PROGRESS_EMPTY (PROGRESS_FONT + 40)
#define PROGRESS_HEAD 8
#define PROGRESS_CELLS 16
typedef char progress_tiles_fit[(PROGRESS_FONT + 42 <= 256) ? 1 : -1];
volatile uint8_t hud_icons;
static uint8_t prepared_hud_icons;

/* Only palettes 0 (top HUD) and 5 (sun) are borrowed below the playfield.
 * Restore them every VBlank; shop interiors never use the landmark raster. */
void render_hud_palettes(uint8_t bottom) __naked {
    /* SDCC passes the byte in A. An unrolled port copy keeps both interrupt
     * paths short, including a stage reset rebuilding the whole scene. */
    __asm
        ld b, a
        or a
        ld hl, #_progress_game_palettes
        jr Z, hp_selected
        ld hl, #_progress_hud_palettes
hp_selected:
        ld a, #0x80
        ld (#0xFF68), a
        ld c, #0x69
        ld a, b
        or a
        jr Z, hp_write_flag
hp_flag_active:
        ld a, (#0xFF41)
        and a, #3
        jr Z, hp_flag_active
hp_flag_blank:
        ld a, (#0xFF41)
        and a, #3
        jr NZ, hp_flag_blank
hp_write_flag:
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, #0xA8
        ld (#0xFF68), a
        ld a, b
        or a
        jr Z, hp_write_island
hp_island_active:
        ld a, (#0xFF41)
        and a, #3
        jr Z, hp_island_active
hp_island_blank:
        ld a, (#0xFF41)
        and a, #3
        jr NZ, hp_island_blank
hp_write_island:
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ld a, (hl+)
        ldh (c), a
        ret
    __endasm;
}

#define ATTR_OF(tile) (((volatile uint8_t *)ATTR_LUT)[tile])

/* Everything prepare_hud reads. The rows are rebuilt only when one of these moved. */
typedef struct {
    uint32_t score, best;
    uint16_t banner_points;
    uint8_t phase, blink, hearts, stage, banner, banner_air, banner_mult, combo, combo_hot, pause, beach_hint;
} HudState;
static HudState hud_last;
static uint8_t hud_valid;

static uint8_t hud_changed(void) {
    HudState now;
    now.score = world.phase == PHASE_PLAY && !RACING ? 0 : world.score;
    now.best = RACING?race.rival.score:world.best;
    now.banner_points = world.banner_points;
    now.phase = world.phase;
    now.blink = (uint8_t)((world.tick >> 5) & 1);
    now.hearts = world.hearts;
    now.stage = world.stage;
    now.banner = world.banner;
    now.banner_air = world.banner_air;
    now.banner_mult = world.banner_mult;
    now.combo = world.combo;
    now.combo_hot = world.combo_timer > 90;
    now.pause = pause_show;
    now.beach_hint=(world.phase==PHASE_BEACH_START || world.phase==PHASE_BEACH_END)?
        (shop_near_door()?1:race_near()?2:(uint8_t)(3+((world.tick>>6)&1))):0;
    if (hud_valid && memcmp(&now, &hud_last, sizeof now) == 0) return 0;
    hud_last = now;
    hud_valid = 1;
    return 1;
}



static uint8_t want_hud[2][COLS];

static uint8_t hy[21];
static uint8_t fy[21];
static uint8_t fy_half[11];
uint8_t *oam_back;
uint8_t oam_n;
uint8_t oam_used[2];

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
    write_text(row,len<COLS?(uint8_t)((COLS-len)/2):0,text);
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
    prepared_bands|=DIRTY_HUD;
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
    } else if (world.banner == BANNER_LOW) {
        strcpy(out, "TOO LOW");
    } else if (world.banner == BANNER_HIT) {
        strcpy(out, "HIT 50 LOST");
    } else if (world.banner == BANNER_COINS) {
        uint16_t points=world.banner_points;
        strcpy(out,"COINS ");
        if(points>=100) {out[6]=(char)('0'+points/100);out[7]=(char)('0'+(points/10)%10);out[8]=(char)('0'+points%10);out[9]=0;}
        else {out[6]=(char)('0'+points/10);out[7]=(char)('0'+points%10);out[8]=0;}
    } else if (world.banner == BANNER_CLEAN) {
        strcpy(out, "CLEAN LAND");
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
    char text[32],race_bottom[32];
    uint8_t i;
    uint8_t blink = (uint8_t)((world.tick >> 5) & 1);
    if(race_hud(text,race_bottom,0)) {
        center_text(0,text);
        if(RACING && !pause_show) {banner_text(text);center_text(1,text[0]?text:race_bottom);}
        else center_text(1,pause_show?"PAUSED":race_bottom);
        return;
    }
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

    if(world.phase==PHASE_SHOP) {
        center_text(0,shop_mode==SHOP_BROWSE?"CHOOSE YOUR BOARD":"BEAVER SURF SHOP");
        center_label_num(1,"POINTS",world.score);
        return;
    }

    if (world.phase == PHASE_BEACH_START) {
        center_text(0, world.stage == 1 ? "BEACH START" : "NEXT BEACH");
        center_text(1, race_near()?"A TALK TO RIVAL":shop_near_door()?"A ENTER SURF SHOP":(world.tick>>6)&1?"JUMP THEN DOWN LANE":"A JUMP THEN UP LANE");
        return;
    }
    if (world.phase == PHASE_BEACH_END) {
        center_text(0, "BEACH CLEAR");
        if(shop_near_door())center_text(1,"A ENTER SURF SHOP");
        else if(race_near())center_text(1,"A TALK TO RIVAL");
        else {
            uint8_t next=world.stage==255?255:(uint8_t)(world.stage+1);
            strcpy(text,"NEXT STAGE 000");
            text[11]=(char)('0'+next/100);
            text[12]=(char)('0'+(next/10)%10);
            text[13]=(char)('0'+next%10);
            center_text(1,text);
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

static uint8_t sl_left, sl_step, sl_front;
static uint8_t *sl_out;

/* DE = world pixel (0..511), BC = destination. sl_left samples, sl_step pixels apart, through
 * the same table lerp as the rules (le_core in wave.c): h0 + ((h1 - h0) * frac >> 3). Heights
 * stay in 0..16383, so the pixel row is the byte straddling bits 6..13.
 * sl_front = 0: screen y = row - 24, clamped to 0..200 (the surf line).
 * sl_front = 1: the shaped lower crest/trough scaled by front_num/16. The caller
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
        bit 7, h
        jr NZ, sl_crest
        sra h
        rr l
        sra h
        rr l
sl_crest:
        ld e, l
        ld d, h
        add hl, hl
        add hl, de
        add hl, hl
        add hl, hl
        ld a, (#_front_num)
        sub a, #12
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
        bit 7, h
        jr NZ, sl_add
        cp a, #11
        jr C, sl_add
        ld a, #10
sl_add:
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

/* One tile-map pass over both waves. The shared ramps contain the complete 23px
 * depth profile, so shade boundaries move with each surface. Tile pixels stay static. */
static uint8_t p_col, p_fronttop;
static uint8_t p_mainheight,p_frontheight;
static uint16_t p_mainlut, p_frontlut;

static void paint_all(void) __naked {
    __asm
        xor a
        ld (#_p_col), a
wg_col:
        ld a, (#_p_col)
        ld e, a
        ld d, #0
        ld hl, #_hy
        add hl, de
        ld a, (hl+)
        ld c, a
        add a, #32
        ld (#_p_mainheight), a
        ld a, (hl)
        sub a, c
        call wg_lut
        ld a, l
        ld (#_p_mainlut), a
        ld a, h
        ld (#_p_mainlut + 1), a
        ld a, (#_p_col)
        ld e, a
        ld d, #0
        ld hl, #_fy
        add hl, de
        ld a, (hl+)
        ld c, a
        add a, #32
        ld (#_p_frontheight), a
        ld a, (hl)
        cp a, c
        jr C, wg_frontmin
        ld a, c
wg_frontmin:
        and a, #0xf8
        ld (#_p_fronttop), a
        ld a, (hl)
        sub a, c
        call wg_lut
        ld a, l
        ld (#_p_frontlut), a
        ld a, h
        ld (#_p_frontlut + 1), a
        ld a, (#_p_col)
        add a, #SHADOW_WAVE
        ld l, a
        ld h, #(SHADOW_TILES >> 8)
        ld e, #72
        ; The final two rows are fixed HUD, never water. Leave their cache intact.
        ld b, #(WAVE_ROWS - 2)
wg_row:
        push hl
        ld a, (#_p_fronttop)
        cp a, e
        jr C, wg_front
        jr Z, wg_front
        ld hl, #_p_mainlut
        ld a, (#_p_mainheight)
        jr wg_sample
wg_front:
        ld hl, #_p_frontlut
        ld a, (#_p_frontheight)
wg_sample:
        sub a, e
        cp a, #RAMP_SUB_COUNT
        jr NC, wg_solid
        add a, (hl)
        inc hl
        ld h, (hl)
        ld l, a
        jr NC, wg_lookup
        inc h
wg_lookup:
        ld a, (hl)
        pop hl
        cp a, #TILE_RAMP1_COUNT
        jr NC, wg_bank0
        add a, #TILE_RAMP
        ld (hl), a
        set 1, h
        ld (hl), #0x0e
        jr wg_next
wg_bank0:
        add a, #(TILE_RAMP0 - TILE_RAMP1_COUNT)
        ld (hl), a
        set 1, h
        ld (hl), #0x06
        jr wg_next
wg_solid:
        bit 7, a
        pop hl
        jr Z, wg_sea
        ld (hl), #TILE_FRONT_DEEP
        set 1, h
        ld (hl), #0x0e
        jr wg_next
wg_sea:
        ld (hl), #TILE_FAR_BODY
        set 1, h
        ld (hl), #0x0b
wg_next:
        res 1, h
        ld a, l
        add a, #32
        ld l, a
        jr NC, wg_samerow
        inc h
wg_samerow:
        ld a, e
        add a, #8
        ld e, a
        dec b
        jp NZ, wg_row
        ld a, (#_p_col)
        inc a
        ld (#_p_col), a
        cp a, #20
        jp NZ, wg_col
        ret
        ; A = signed slope. Each LUT row has 48 entries: index * (32 + 16).
wg_lut:
        bit 7, a
        jr Z, wg_positive
        cp a, #(256 + RAMP_DY_MIN)
        jr NC, wg_clamped
        ld a, #(256 + RAMP_DY_MIN)
        jr wg_clamped
wg_positive:
        cp a, #(RAMP_DY_MAX + 1)
        jr C, wg_clamped
        ld a, #RAMP_DY_MAX
wg_clamped:
        add a, #(0 - RAMP_DY_MIN)
        ld l, a
        ld h, #0
        add hl, hl
        add hl, hl
        add hl, hl
        add hl, hl
        ld e, l
        ld d, h
        add hl, hl
        add hl, de
        ld de, #_RAMP_LUT
        add hl, de
        ret
    __endasm;
}

static void prepare_wave(uint16_t pixels) {
    sample_heights((uint16_t)(pixels&511));
    sample_front(pixels);
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
    if(!shore_count)shore_first=col&31;
    else if(((shore_first+shore_count)&31)!=(col&31))shore_count=32;
    if(shore_count<32)shore_count++;
    prepared_bands|=DIRTY_SHORE;
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

static void progress_cell(uint8_t row, uint8_t col, uint8_t tile) {
    uint16_t offset = SHADOW_WAVE + (uint16_t)(7 + row) * 32 + col;
    prepared_bands|=DIRTY_FOOTER;
    ((volatile uint8_t *)SHADOW_TILES)[offset] = tile;
    ((volatile uint8_t *)SHADOW_ATTRS)[offset] = 0x80 | PAL_SKY;
}

static uint8_t progress_marker=255,progress_stage;
static uint32_t progress_next,progress_step,progress_distance;
static void prepare_progress(void) {
    uint32_t distance=world.course_distance;
    uint8_t marker, col, row, redraw;
    if(distance>course_length)distance=course_length;
    redraw=(uint8_t)(progress_marker==255 || world.stage!=progress_stage || distance<progress_distance);
    marker=redraw?0:progress_marker;
    if(redraw) {
        progress_step=course_length/(PROGRESS_CELLS-1);
        progress_next=progress_step;
    }
    while(marker<PROGRESS_CELLS-1 && distance>=progress_next) {
        marker++;
        progress_next=marker==PROGRESS_CELLS-2?course_length:progress_next+progress_step;
    }
    progress_distance=distance;
    if(!redraw && marker==progress_marker)return;
    progress_marker=marker;progress_stage=world.stage;
    if(redraw) {
    for (col = 0; col < COLS; col++) {
        progress_cell(0, col, PROGRESS_FONT);
        progress_cell(1, col, PROGRESS_FONT);
    }
    for(row=0;row<2;row++)for(col=0;col<2;col++) {
        uint16_t offset=SHADOW_WAVE+(uint16_t)(7+row)*32;
        progress_cell(row,col,(uint8_t)(row*2+col));
        progress_cell(row,col+18,(uint8_t)(4+row*2+col));
        ((volatile uint8_t *)SHADOW_ATTRS)[offset+col]=0x80|5;
        ((volatile uint8_t *)SHADOW_ATTRS)[offset+col+18]=0x80|0;
    }
    progress_cell(1,5,PROGRESS_FONT+glyph('S'));
    progress_cell(1,6,PROGRESS_FONT+glyph('T'));
    progress_cell(1,7,PROGRESS_FONT+glyph('A'));
    progress_cell(1,8,PROGRESS_FONT+glyph('G'));
    progress_cell(1,9,PROGRESS_FONT+glyph('E'));
    progress_cell(1,11,PROGRESS_FONT+TILE_DIGIT+world.stage/100);
    progress_cell(1,12,PROGRESS_FONT+TILE_DIGIT+(world.stage/10)%10);
    progress_cell(1,13,PROGRESS_FONT+TILE_DIGIT+world.stage%10);
    }
    for(col=0;col<PROGRESS_CELLS;col++) {
        uint16_t offset=SHADOW_WAVE+7u*32u+col+2;
        progress_cell(0,col+2,col==marker?PROGRESS_HEAD:PROGRESS_EMPTY);
        if(col==marker)((volatile uint8_t *)SHADOW_ATTRS)[offset]=0x80;
    }
}

static void shop_line(uint8_t row,const char *text) {
    uint8_t col;
    prepared_bands|=DIRTY_FOOTER;
    for(col=0;col<20;col++) {
        uint16_t offset=SHADOW_WAVE+(uint16_t)(7+row)*32+col;
        uint8_t tile=PROGRESS_FONT;
        if(*text)tile=(uint8_t)(PROGRESS_FONT+glyph(*text++));
        ((volatile uint8_t *)SHADOW_TILES)[offset]=tile;
        ((volatile uint8_t *)SHADOW_ATTRS)[offset]=0x80|7;
    }
}

static void prepare_shop_dialog(void) {
    progress_marker=255;
    if(shop_mode==SHOP_WALK) {
        shop_line(0,shop_x>=SHOP_COUNTER_TALK_X*FP?"A TALK TO SHOPKEEPER":"WELCOME TO SURF SHOP");
        shop_line(1,"WALK LEFT TO EXIT");
    } else {
        static const char *names[]={"CLASSIC FREE","LAGOON 4000 POINTS","FOREST 6000 POINTS","SUNSET 10000 POINTS"};
        shop_line(0,names[shop_selected]);
        shop_line(1,shop_message==SHOP_SHORT?"NOT ENOUGH POINTS":
            shop_message==SHOP_BOUGHT?"BOUGHT  B BACK":
            shop_message==SHOP_EQUIPPED?"EQUIPPED  B BACK":
            (shop_owned&(1u<<shop_selected))?"A EQUIP   B BACK":"A BUY     B BACK");
    }
}

void render_prepare(void) {
    prepared_hud_icons=(uint8_t)(world.phase!=PHASE_SHOP);
    static uint8_t was_beach;
    uint32_t pixels = (uint32_t)(world.scroll >> 6);
    uint32_t visual_pixels=world.visual_scroll>>6;
    prepared_bands=0;
    shore_count=0;
    beach_prepare();
    if(beach_map_dirty) {prepared_bands|=DIRTY_WAVE|DIRTY_SHORE;shore_count=32;}
    if(world.phase>=PHASE_BEACH_START || shop_scene)shop_prepare();
    raster_sky = (uint8_t)(visual_pixels >> 3);
    raster_playfield=beach_scx;
    raster_beach=beach_active;
    if(beach_active || world.phase==PHASE_SHOP) raster_island=0;
    else {
        if(was_beach) isl_valid=0;
        raster_island=island_x(visual_pixels);
        if(isl_rebuild) shore_rebuild();
    }
    was_beach=beach_active;
    if (hud_changed() || world.phase==PHASE_SHOP) {
        prepare_hud();
        hud_commit();
        prepared_bands|=DIRTY_HUD;
    } else if (world.phase == PHASE_PLAY && !RACING && world.score != sd_value) {
        write_score(world.score);
        score_commit();
    }
    if(world.phase==PHASE_SHOP) {
        /* Rows 2-4 contain the room's repeating wooden wall, not a fixed sky.
         * Apply the same fine camera offset to every interior band. The HUD
         * remains fixed above y=16 and below y=128 in lcd_isr. */
        raster_sky=raster_island=raster_playfield=shop_scroll;
        raster_beach=1;
        prepared_bands|=DIRTY_WAVE|DIRTY_SHORE;
        shore_count=32;
    } else if (!beach_active) {
        prepare_wave((uint16_t)pixels);
        prepared_bands|=DIRTY_WAVE;
    }
    if(world.phase==PHASE_SHOP)prepare_shop_dialog();
    else {
        char top[32],bottom[32];
        if(race_hud(top,bottom,1)) {
            uint8_t row,col;
            const char *s;
            progress_marker=255;
            for(row=0;row<2;row++) {
                s=row?bottom:top;
                for(col=0;col<COLS;col++)progress_cell(row,col,(uint8_t)(PROGRESS_FONT+glyph(*s?*s++:' ')));
            }
        } else prepare_progress();
    }
    sprites_prepare();
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

/* Ordinary scrolling changes one hidden scenery column. Eight scattered bytes
 * cost less than DMAing both complete 128-byte shore planes. */
static void present_shore_columns(void) {
    uint8_t col=shore_first,n=shore_count;
    do {
        VBK_REG=0;
        ((volatile uint8_t *)0x98A0u)[col]=((volatile uint8_t *)0xCD60u)[col];
        ((volatile uint8_t *)0x98C0u)[col]=((volatile uint8_t *)0xCD80u)[col];
        ((volatile uint8_t *)0x98E0u)[col]=((volatile uint8_t *)0xCDA0u)[col];
        ((volatile uint8_t *)0x9900u)[col]=((volatile uint8_t *)0xCDC0u)[col];
        VBK_REG=1;
        ((volatile uint8_t *)0x98A0u)[col]=((volatile uint8_t *)0xCF60u)[col];
        ((volatile uint8_t *)0x98C0u)[col]=((volatile uint8_t *)0xCF80u)[col];
        ((volatile uint8_t *)0x98E0u)[col]=((volatile uint8_t *)0xCFA0u)[col];
        ((volatile uint8_t *)0x9900u)[col]=((volatile uint8_t *)0xCFC0u)[col];
        col=(col+1)&31;
    } while(--n);
}

/* Call at the start of VBlank (or with the LCD off). Transfer only changed bands:
 * the budget includes DMA stalls, palette routines, bank switching and OAM.
 * The default VBL OAM transfer is disabled; present commits it once per frame. */
void render_present(void) {
    uint8_t scene_change=(uint8_t)(beach_loading || shop_loading || (actor_pending&4));
    uint8_t lcd_was_on=(uint8_t)(LCDC_REG&LCDCF_ON);
    hud_icons=prepared_hud_icons;
    /* Bulk scene swaps cannot fit a single VBlank. A brief blank is intentional:
     * never tear the live VRAM or leave the raster and tilemap one frame apart. */
    if(scene_change)LCDC_REG&=(uint8_t)~LCDCF_ON;
    if(shop_loading || shop_palette_pending)shop_present();
    if(beach_pending) beach_present();
    if(actor_pending) actor_present();
    VBK_REG = 0;
    if(prepared_bands&DIRTY_HUD)GDMA(SHADOW_TILES + SHADOW_HUD, 0x9800, 4);
    if(prepared_bands&DIRTY_WAVE)GDMA(SHADOW_TILES + SHADOW_WAVE, 0x9920, 2 * (WAVE_ROWS-2));
    if(prepared_bands&DIRTY_FOOTER)GDMA(SHADOW_TILES + SHADOW_WAVE + 224, 0x9A00, 4);
    if((prepared_bands&DIRTY_SHORE) && shore_count>=3)GDMA(SHADOW_TILES + SHADOW_SHORE, 0x98A0, 8);
    VBK_REG = 1;
    /* All top-HUD glyphs use bank 1 / palette 0; attributes were set at init. */
    if(prepared_bands&DIRTY_WAVE)GDMA(SHADOW_ATTRS + SHADOW_WAVE, 0x9920, 2 * (WAVE_ROWS-2));
    if(prepared_bands&DIRTY_FOOTER)GDMA(SHADOW_ATTRS + SHADOW_WAVE + 224, 0x9A00, 4);
    if((prepared_bands&DIRTY_SHORE) && shore_count>=3)GDMA(SHADOW_ATTRS + SHADOW_SHORE, 0x98A0, 8);
    if(shore_count && shore_count<3)present_shore_columns();
    scx_sky=raster_sky;
    scx_island=raster_island;
    scx_playfield=raster_playfield;
    scx_beach=raster_beach;
    VBK_REG = 0;
    /* Copy the prepared OAM page so sprites and map land in the same blank.
     * The other page is free to build on.
     * GBDK's C-callable refresh_OAM() always copies from 0xC000, so call the HRAM routine,
     * which reads _shadow_OAM_base, directly. */
    _shadow_OAM_base = (uint8_t)((uint16_t)oam_back >> 8);
    __asm__("call .refresh_OAM");
    DISABLE_OAM_DMA;
    oam_back = (uint8_t *)((uint16_t)oam_back == OAM_PAGE_A ? OAM_PAGE_B : OAM_PAGE_A);
    if(scene_change && lcd_was_on) {
        SCX_REG=0;LYC_REG=16;
        LCDC_REG|=LCDCF_ON;
    }
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
        RGB(0, 0, 0),  RGB(0, 0, 0),   RGB(19, 11, 7), RGB(19, 0, 6),
        RGB(0, 0, 0),  RGB(6, 5, 4),   RGB(31, 31, 31), RGB(31, 20, 4),
        RGB(0, 0, 0),  RGB(17, 10, 1), RGB(31, 23, 4), RGB(31, 29, 17),
        RGB(0, 0, 0),  RGB(15, 2, 3),  RGB(29, 6, 7),  RGB(31, 31, 31),
        RGB(0, 0, 0),  RGB(2, 10, 18), RGB(22, 27, 30), RGB(31, 31, 31),
        RGB(0, 0, 0),  RGB(1, 4, 9),   RGB(29, 6, 7),  RGB(31, 31, 31),
        RGB(0, 0, 0),  RGB(1, 3, 6),   RGB(17, 13, 9), RGB(3, 25, 24),
        RGB(0, 0, 0),  RGB(10, 7, 9),  RGB(31, 28, 17), RGB(31, 31, 31),
    };
    const uint8_t *bytes = (const uint8_t *)pals;
    uint8_t i;
    OCPS_REG = 0x80;
    for (i = 0; i < 64; i++) OCPD_REG = bytes[i];
}

static void lcd_off(void) {
    /* Hardware requires LCD shutdown in VBlank. Initialisation may start with
     * the boot screen active, so wait for mode 1 rather than a visible HBlank. */
    if (LCDC_REG & LCDCF_ON) {
        while (LY_REG < 144) ;
        LCDC_REG &= (uint8_t)~LCDCF_ON;
    }
}

void render_init(void) {
    uint8_t x;
    uint16_t cell;
    uint8_t saved;
    oam_back = (uint8_t *)OAM_PAGE_A;
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
    /* Remap the existing font: background 0 -> pale 3, ink 1 -> blue 1.
     * Both are nonzero, so BG priority keeps actors behind this fixed HUD. */
    VBK_REG = 0;
    for (cell = 0; cell < 42 * 8; cell++) {
        uint16_t dest = 0x8000u + PROGRESS_FONT * 16u + cell * 2;
        ((volatile uint8_t *)dest)[0] = (uint8_t)~bg_tiles[cell * 2 + 1];
        ((volatile uint8_t *)dest)[1] = (uint8_t)~bg_tiles[cell * 2];
    }
    {
        for (x = 0; x < 8; x++) {
            uint16_t empty = 0x8000u + PROGRESS_EMPTY * 16u + x * 2;
            uint8_t line = (x == 3 || x == 4) ? 0xff : 0;
            ((volatile uint8_t *)empty)[0] = (uint8_t)~line;
            ((volatile uint8_t *)empty)[1] = 0xff;
        }
    }
    vram_copy(0,256,progress_patterns,sizeof(progress_patterns));
    set_bkg_palette(0, bg_PALETTE_COUNT, bg_palettes);
    sprite_pals();
    /* Page-aligned so the painter looks a tile's attribute byte up by loading L. */
    for (cell = 0; cell < TILE_COUNT; cell++) {
        ATTR_OF(cell) = (uint8_t)(bg_map_attributes[cell] | 0x08);
    }
    SWITCH_ROM(saved);

    beach_init();

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
    shop_init();
    VBK_REG=1;
    memset((void *)0x9800u,0x08,64);
    VBK_REG=0;
}
