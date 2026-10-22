#include <gb/cgb.h>
#include <gb/gb.h>
#include <string.h>

#include "render.h"
#include "rules.h"
#include "sound.h"

/* Raster bands: HUD lines 0-15 stay put, sky (16-39) and shore (40-71) scroll, the wave does not. */
volatile uint8_t scx_sky;
volatile uint8_t scx_island;

/* Vblanks since the main loop last drained the counter. The rules run once per vblank at
 * 60 Hz no matter how long a frame takes to draw: a two-vblank frame steps twice. */
static volatile uint8_t pending;
/* Buttons passed to the last rules step; the renderer picks the tuck pose from it. */
uint8_t last_input;

static void vbl_tick(void) {
    pending++;
}

/* Blocks until a vblank starts, or returns at once if one began a moment ago. GBDK's vsync()
 * clears its flag after the interrupt may already have fired, which then costs a whole extra
 * frame; waiting on the counter has no such gap. */
static void wait_vblank(void) {
    uint8_t seen = pending;
    if (LY_REG >= 144 && LY_REG < 150) return;
    while (pending == seen) __asm__("halt");
}

void lcd_isr(void) {
    if (LYC_REG == 16) {
        SCX_REG = scx_sky;
        LYC_REG = 40;
    } else if (LYC_REG == 40) {
        SCX_REG = scx_island;
        LYC_REG = 72;
    } else {
        SCX_REG = 0;
        LYC_REG = 16;
    }
}

static uint8_t read_input(void) {
    uint8_t keys = joypad();
    uint8_t input = 0;
    if (keys & J_LEFT) input |= INPUT_LEFT;
    if (keys & J_RIGHT) input |= INPUT_RIGHT;
    if (keys & (J_A | J_UP)) input |= INPUT_JUMP;
    if (keys & (J_B | J_DOWN)) input |= INPUT_DIVE;
    if (keys & J_START) input |= INPUT_START;
    return input;
}

static void load_best(void) {
    uint8_t raw[SAVE_LEN];
    uint32_t best = 0;
    ENABLE_RAM;
    memcpy(raw, (uint8_t *)0xA000, SAVE_LEN);
    DISABLE_RAM;
    if (save_decode(raw, &best)) world.best = best;
}

static void store_best(void) {
    uint8_t raw[SAVE_LEN];
    save_encode(world.best, raw);
    ENABLE_RAM;
    memcpy((uint8_t *)0xA000, raw, SAVE_LEN);
    DISABLE_RAM;
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
    add_VBL(vbl_tick);
    STAT_REG = STATF_LYC;
    LYC_REG = 16;
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    DISPLAY_ON;
    pending = 0;

    while (1) {
        uint8_t steps;
        __critical {
            steps = pending;
            pending = 0;
        }
        /* One step per vblank that passed, at least one and at most two: two steps plus a
         * frame of drawing fit in two vblanks, so the game settles at 30 fps with the rules at
         * 60 Hz. Three would overrun into a third vblank and lock the loop at 20 fps, so the
         * occasional slow frame drops a tick instead. */
        if (steps == 0) steps = 1;
        if (steps > 2) steps = 2;
        do {
            last_input = read_input();
            world_step(last_input, &events);
            sound_events(&events);
            sound_tick();
            if (events.died) store_best();
        } while (--steps);
        render_prepare();
        wait_vblank();
        render_present();
    }
}
