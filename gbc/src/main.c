#include <gb/cgb.h>
#include <gb/gb.h>
#include <string.h>

#include "render.h"
#include "beach.h"
#include "rules.h"
#include "sound.h"
#include "shop.h"

/* Raster bands: HUD lines 0-15 stay put, sky (16-39) and shore (40-71) scroll.
 * Beach terrain begins at 48; the surf wave uses its own unscrolled map from 72. */
volatile uint8_t scx_sky;
volatile uint8_t scx_island;
volatile uint8_t scx_playfield;
volatile uint8_t scx_beach;

/* Vblanks the rules have not stepped for yet. The rules run once per vblank at 60 Hz no
 * matter how long a frame takes to draw; the vblank handler counts, the main loop drains. */
static volatile uint8_t pending;
/* A prepared frame sits in the shadow maps waiting for the vblank handler to copy it in. */
static volatile uint8_t frame_ready;
/* Two independent riders can push a rare frame preparation just past three
 * VBlanks. Retain all four owed ticks, then drain them before drawing again;
 * this changes queue capacity, never the 60 Hz physics or shared race clock. */
#define MAX_PENDING 4
/* Buttons passed to the last rules step; the renderer picks the tuck pose from it. */
uint8_t last_input;
/* Ticks thrown away because the loop fell more than MAX_PENDING vblanks behind. Stays 0. */
uint16_t frames_dropped;
/* Start during play freezes the rules and the song until Start is pressed again. The rules
 * never see the paused frames, so a paused run replays the same as an unpaused one. */
static uint8_t paused;
static uint8_t start_held;
static uint8_t pause_ticks;
uint8_t pause_show;

/* Runs at the top of every vblank: a waiting frame goes to VRAM while the LCD is idle, and
 * the rules owe one more tick. */
static void vbl_tick(void) {
    /* The footer uses the spare signed BG patterns and hides all actors. Restore
     * normal addressing, objects and gameplay palettes before the next top row,
     * including vblanks where no new render was prepared. */
    LCDC_REG |= LCDCF_BG8000 | LCDCF_OBJON;
    if(hud_icons)render_hud_palettes(0);
    if (frame_ready) {
        render_present();
        frame_ready = 0;
    }
    pending++;
}

void lcd_isr(void) {
    if (LYC_REG == 16) {
        SCX_REG = scx_sky;
        LYC_REG = 40;
    } else if (LYC_REG == 40) {
        SCX_REG = scx_island;
        LYC_REG = 48;
    } else if (LYC_REG == 48) {
        SCX_REG = scx_beach ? scx_playfield : scx_island;
        LYC_REG = 72;
    } else if (LYC_REG == 72) {
        SCX_REG = scx_playfield;
        LYC_REG = 127;
        /* Sun/HUD palettes are no longer used in this playfield band. Install
         * the footer colors early, rather than writing during its pixel fetch. */
        if(hud_icons)render_hud_palettes(1);
    } else {
        /* Reset during the preceding line's HBlank. At LYC=128 the ISR can
         * arrive after the first HUD tile fetch, slicing/scrolling its ink. */
        while (STAT_REG & 0x03) ;
        SCX_REG = 0;
        LCDC_REG &= (uint8_t)~LCDCF_OBJON;
        if(hud_icons)LCDC_REG &= (uint8_t)~LCDCF_BG8000;
        LYC_REG = 16;
    }
}

static uint8_t read_input(void) {
    uint8_t keys = joypad();
    uint8_t input = 0;
    if (keys & J_LEFT) input |= INPUT_LEFT;
    if (keys & J_RIGHT) input |= INPUT_RIGHT;
    if (keys & J_A) input |= INPUT_JUMP;
    if (keys & (J_B | J_DOWN)) input |= INPUT_DIVE;
    if (keys & J_START) input |= INPUT_START;
    if (keys & J_UP) input |= INPUT_UP;
    if (keys & J_DOWN) input |= INPUT_DOWN;
    if (keys & J_A) input |= INPUT_ACTION;
    return input;
}

/* Toggles the pause on a Start press during play and returns whether this tick is frozen. */
static uint8_t pause_poll(uint8_t input) {
    uint8_t start = (input & INPUT_START) != 0;
    if (start && !start_held && world.phase == PHASE_PLAY) {
        paused = (uint8_t)!paused;
        pause_ticks = 0;
        if (paused) sound_pause();
    }
    start_held = start;
    if (paused) {
        pause_ticks++;
        pause_show = (uint8_t)(((pause_ticks >> 5) & 1) ? 2 : 1);
    } else {
        pause_show = 0;
    }
    return paused;
}

static void load_best(void) {
    uint8_t raw[SAVE_LEN];
    uint32_t best = 0;
    ENABLE_RAM;
    memcpy(raw, (uint8_t *)0xA000, SAVE_LEN);
    DISABLE_RAM;
    if (save_decode(raw, &best)) world.best = best;
    ENABLE_RAM;
    memcpy(raw, (uint8_t *)0xA008, SAVE_LEN);
    DISABLE_RAM;
    shop_load(raw);
}

static void store_best(void) {
    uint8_t raw[SAVE_LEN];
    save_encode(world.best, raw);
    ENABLE_RAM;
    memcpy((uint8_t *)0xA000, raw, SAVE_LEN);
    DISABLE_RAM;
}

/* One rules tick for one vblank off the backlog. */
static void step_once(Events *events) {
    __critical {
        if (pending > MAX_PENDING) {
            frames_dropped += (uint16_t)(pending - MAX_PENDING);
            pending = MAX_PENDING;
        }
        pending--;
    }
    last_input = read_input();
    if (pause_poll(last_input)) return;
    world_step(last_input, events);
    sound_events(events);
    sound_tick();
    if (events->died) store_best();
    if (shop_save_pending) {
        uint8_t raw[SAVE_LEN];
        shop_save(raw);
        ENABLE_RAM;
        memcpy((uint8_t *)0xA008, raw, SAVE_LEN);
        DISABLE_RAM;
        store_best();
        shop_save_pending=0;
    }
}

void main(void) {
    Events events;
    cpu_fast();
    cgb_compatibility();
    sound_init();
    world_new();
    load_best();
    render_init();
    render_prepare();
    render_present();

    add_LCD(lcd_isr);
    add_LCD(nowait_int_handler);
    add_VBL(vbl_tick);
    add_VBL(nowait_int_handler);
    STAT_REG = STATF_LYC;
    LYC_REG = 16;
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    DISPLAY_ON;
    pending = 0;

    /* The rules keep 60 Hz and the display takes whatever is left: the CPU never idles while
     * a tick is owed. A frame is prepared from the current state, then while it waits for its
     * vblank the ticks that arrive are stepped straight away, so a frame that takes a little
     * over two vblanks only costs display rate, never game time. Top lane: about 28 fps;
     * bottom lane, whose steps are heavier: about 20 fps. */
    while (1) {
        while (frame_ready) {
            if (pending) step_once(&events);
            else __asm__("halt\n nop");
        }
        while (pending == 0) __asm__("halt\n nop");
        do step_once(&events);
        while (pending);
        render_prepare();
        frame_ready = 1;
    }
}
