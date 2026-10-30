#ifndef BEAVER_BEACH_H
#define BEAVER_BEACH_H
#include "rules.h"
/* Beach actors reuse the gull/pickup slots, restored before surfing. */
#define BEACH_ACTOR_TILE 136
#define BEACH_BOARD_TILE 168
extern uint8_t beach_active;
extern uint8_t beach_scx;
extern uint8_t beach_pending;
extern uint8_t beach_loading;
extern uint8_t beach_map_dirty;
void beach_refresh_palettes(void) BANKED;
uint8_t beach_is_visible(void) BANKED;
void beach_init(void) BANKED;
void beach_prepare(void) BANKED;
void beach_present(void) BANKED;
#endif
