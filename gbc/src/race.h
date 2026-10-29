#ifndef BEAVER_RACE_H
#define BEAVER_RACE_H
#include "rules.h"
enum {RACE_IDLE,RACE_OFFER,RACE_COUNTDOWN,RACE_RUNIN,RACE_ACTIVE,RACE_RESULTS,RACE_DONE};
/* Only per-rider fields are exchanged by the shared physics helper. The course,
 * wave table, actors and clock are never copied or independently advanced. */
typedef struct {
    Beaver beaver;
    uint32_t score;
    int16_t speed;
    uint16_t combo_timer,banner_points;
    uint8_t combo,meter,stun,invuln,banner,banner_mult,banner_air,banner_timer;
    uint8_t jump_held,dive_held,down_tap,up_tap,buffer,up,down;
} RiderState;
typedef struct {uint16_t coins,tricks,hits;} RaceStats;
typedef struct {
    RiderState rival;
    uint32_t wallet,player_score;
    RaceStats player_stats,rival_stats;
    uint16_t timer,payout;
    uint8_t state,held,yes,short_funds,saved_health,page;
    uint8_t ai_input,ai_wait,ai_think,ai_seen,ai_flip,sequence;
} RaceState;
extern RaceState race;
#define RIVAL_BEACH_X 144
#define RIVAL_BEACH_Y 136
#define RACING (race.state==RACE_ACTIVE)
void race_reset(void) BANKED;
uint8_t race_near(void) BANKED;
uint8_t race_beach_blocked(int16_t x,int16_t y) BANKED;
uint8_t race_beach(uint8_t input,Events *events) BANKED;
void race_begin_surf(void) BANKED;
void race_targets(int16_t step) BANKED;
uint8_t race_ai(void) BANKED;
void race_record(const Events *events,uint8_t rival) BANKED;
void race_finish(void) BANKED;
/* Returns 1 when the race owns the two top or bottom HUD rows. */
uint8_t race_hud(char *top,char *bottom,uint8_t footer) BANKED;
void world_rival_step(uint8_t input,Events *events) BANKED;
#endif
