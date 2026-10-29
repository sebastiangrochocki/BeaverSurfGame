#ifndef BEAVER_RENDER_H
#define BEAVER_RENDER_H

#include <stdint.h>

/* Buttons given to the most recent world_step (INPUT_* bits), owned by main.c. */
extern uint8_t last_input;
/* Owned by main.c: 0 while the game runs, else the paused HUD's blink phase (1 off, 2 on). */
extern uint8_t pause_show;
/* Committed bottom-HUD raster mode; shop dialogue uses its normal palettes. */
extern volatile uint8_t hud_icons;
void render_hud_palettes(uint8_t bottom);

void render_init(void);
/* Builds the tile queues and the sprite list from the current world. Call before vblank. */
void render_prepare(void);
/* DMAs the shadow maps and OAM page into place. Call at the start of vblank or with the LCD off. */
void render_present(void);

#endif

