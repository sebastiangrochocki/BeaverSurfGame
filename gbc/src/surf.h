#ifndef BEAVER_SURF_H
#define BEAVER_SURF_H
#include "rules.h"
#define PICKUP_SLOTS 12
#define POP_SLOTS 3
#define PICKUP_PRESENT 1
#define PICKUP_BOTTOM 2
#define PICKUP_HIGH 4
#define PICKUP_BOB 8
#define PICKUP_SHOWN 16
typedef struct { int16_t x,y; uint8_t flags; } Pickup;
typedef struct { int32_t wx; int16_t y; uint8_t life; } PickupPop;
extern Pickup pickups[PICKUP_SLOTS];
extern PickupPop pickup_pops[POP_SLOTS];
extern uint8_t surf_jump_buffer,surf_up_held,surf_down_held,surf_gull_shown;
void surf_reset(void) BANKED;
void surf_tick(int16_t step,uint8_t racing) BANKED;
uint8_t surf_collect(const Beaver *rival,uint8_t player_input,uint8_t rival_input,uint8_t *rival_count) BANKED;
uint8_t surf_flip_safe(void) BANKED;
void surf_clear(void) BANKED;
#endif
