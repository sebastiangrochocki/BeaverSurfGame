#ifndef BEAVER_RENDER_H
#define BEAVER_RENDER_H

#include <stdint.h>

/* Buttons given to the most recent world_step (INPUT_* bits), owned by main.c. */
extern uint8_t last_input;

void render_init(void);
/* Builds the tile queues and the sprite list from the current world. Call before vblank. */
void render_prepare(void);
/* DMAs the shadow maps and OAM page into place. Call at the start of vblank or with the LCD off. */
void render_present(void);

#endif

