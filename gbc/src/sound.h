#ifndef BEAVER_SOUND_H
#define BEAVER_SOUND_H

#include "rules.h"

void sound_init(void);
void sound_events(const Events *events);
/* Advance the music by one frame; call once per rules step, after sound_events. */
void sound_tick(void);
/* Silence the held notes while the game is paused. */
void sound_pause(void);

#endif
