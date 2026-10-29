#ifndef BEAVER_ACTORS_H
#define BEAVER_ACTORS_H
#include "rules.h"
#define ACTOR_GULL 0
#define ACTOR_HEART 1
#define ACTOR_RING 2
#define ACTOR_RACE_GULL 3
extern uint8_t actor_pending;
uint8_t actor_pose(uint8_t input, uint8_t *mirror) BANKED;
uint8_t actor_pose_for(const Beaver *rider,uint8_t input,uint8_t *mirror) BANKED;
uint8_t actor_touches_for(const Beaver *rider,uint8_t input,uint8_t kind,int16_t x,int16_t y) BANKED;
uint8_t actor_pickup_touches(const Beaver *rider,uint8_t input,int16_t x,int16_t y) BANKED;
uint8_t actor_touches(uint8_t input, uint8_t kind, int16_t x, int16_t y) BANKED;
/* frame=255 streams a bare deck; other frames merge the body's lower half. */
void actor_prepare(uint8_t frame, uint8_t deck) BANKED;
void actor_prepare_rival(uint8_t frame,uint8_t deck) BANKED;
void actor_present(void) BANKED;
#endif
