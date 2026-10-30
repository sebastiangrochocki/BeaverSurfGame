#include <assert.h>
#include <stdio.h>
#include "../src/race.h"
#include "../src/surf.h"
static void accuracy_budget(uint8_t precision) {
    unsigned i,precise=0;
    world_new();world.phase=PHASE_PLAY;race.state=RACE_ACTIVE;
    race.precision=precision;race.ai_accuracy=0;race.ai_seen=race.sequence;
    race.rival.beaver=world.beaver;
    pickups[0].x=PX(200);pickups[0].y=HORIZON;pickups[0].flags=PICKUP_PRESENT;
    for(i=0;i<TABLE_LEN;i++)world.heights[i]=HORIZON;
    for(i=0;i<100;i++) {
        race.ai_wait=race.ai_think=0;
        if(race_ai()&INPUT_RIGHT)precise++;
    }
    assert(precise==precision);
}
static uint32_t duel(uint8_t policy,uint8_t stage,uint32_t *npc) {
    Events e;unsigned tick;uint8_t i,input=0;
    world_new();world.phase=PHASE_PLAY;world.stage=stage;
    course_length=COURSE_STAGE_PX(stage)*FP;
    world.tier=(uint8_t)(stage-1);sea_build(0,world.tier);
    race.state=RACE_ACTIVE;race.saved_health=START_HEARTS;race.wallet=2000;
    race.rival.beaver=world.beaver;race.rival.beaver.lane=race.rival.beaver.lane_to=LANE_BOTTOM;
    race.rival.beaver.y=FRONT_HORIZON;race.rival.speed=SCROLL_SPEED;race.ai_seen=255;
    for(tick=0;RACING && tick<10000;tick++) {
        input=0;
        /* Emulate display admission; the race admits exactly one visible target. */
        for(i=0;i<PICKUP_SLOTS;i++)if((pickups[i].flags&PICKUP_PRESENT) && pickups[i].x>=0 && pickups[i].x<=PX(240))pickups[i].flags|=PICKUP_SHOWN;
        if(policy==1)input=INPUT_JUMP|INPUT_RIGHT;
        if(policy==2) {
            if(pickups[0].flags&PICKUP_PRESENT) {
                if(pickups[0].x>world.beaver.x+PX(10))input|=INPUT_RIGHT;
                else if(pickups[0].x<world.beaver.x-PX(10))input|=INPUT_LEFT;
                if(pickups[0].flags&PICKUP_BOTTOM) {
                    if(world.beaver.lane_to==0)input|=world.beaver.airborne?(INPUT_DOWN|INPUT_DIVE):INPUT_JUMP;
                }
                else if(world.beaver.lane==LANE_BOTTOM) {
                    if(!world.beaver.airborne)input|=INPUT_JUMP;
                    else if(world.beaver.vy<0)input|=INPUT_UP;
                }
            }
            if(!world.beaver.airborne && (world.beaver.lane==LANE_BOTTOM?front_in_top_fifth(world_beaver_x()):sea_is_crest(world_beaver_x())))input|=INPUT_JUMP;
            if(world.beaver.airborne && world.beaver.air_time==5 && !world.beaver.flip && surf_flip_safe())input|=INPUT_JUMP;
        }
        surf_gull_shown=15;world_step(input,&e);
    }
    assert(race.state==RACE_RESULTS);*npc=race.rival.score;
    printf("stage %u policy %u: player %lu rival %lu; rings %u/%u tricks %u/%u hits %u/%u\n",stage,policy,(unsigned long)race.player_score,(unsigned long)*npc,race.player_stats.coins,race.rival_stats.coins,race.player_stats.tricks,race.rival_stats.tricks,race.player_stats.hits,race.rival_stats.hits);
    return race.player_score;
}
int main(void) {
    uint32_t npc,score,repeat,npc_repeat;uint8_t policy,stage,wins=0;
    accuracy_budget(80);accuracy_budget(90);accuracy_budget(100);
    for(stage=2;stage<=4;stage++)for(policy=0;policy<3;policy++) {
        score=duel(policy,stage,&npc);repeat=duel(policy,stage,&npc_repeat);
        assert(score==repeat && npc==npc_repeat);
        if(policy && score>npc)wins++;
    }
    assert(wins);puts("AI: exact 80/90/100% precise decision budgets, deterministic real scoring, beatable without point handicaps");
}
