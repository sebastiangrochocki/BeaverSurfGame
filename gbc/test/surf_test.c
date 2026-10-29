#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/rules.h"
#include "../src/surf.h"
#include "../src/actors.h"
static Events e;
static void play(void) {
    world_new();world.phase=PHASE_PLAY;world.course_distance=500UL*FP;
    world.gull_timer=world.ring_timer=60000;world.tick=1;
    world.beaver.x=PX(96);world.beaver.y=sea_at(world_beaver_x());
}
int main(void) {
    uint8_t i,n,other;
    uint8_t encoded[STATE_LEN+2];
    memset(encoded,0xa5,sizeof encoded);world_new();world_encode(encoded+1);
    assert(encoded[0]==0xa5 && encoded[STATE_LEN+1]==0xa5 && STATE_LEN==126);
    play();
    assert(!actor_touches(INPUT_DIVE,ACTOR_GULL,PX(96),(int16_t)(world.beaver.y-PX(31))));
    assert(actor_touches(0,ACTOR_GULL,PX(96),(int16_t)(world.beaver.y-PX(31))));
    for(i=0;i<3;i++) {pickups[i].x=world.beaver.x;pickups[i].y=world.beaver.y-PX(5);pickups[i].flags=PICKUP_PRESENT|PICKUP_SHOWN;}
    n=surf_collect(0,0,0,&other);assert(n==3 && other==0);
    for(i=0;i<3;i++)assert(pickup_pops[i].life==12 && !pickups[i].flags);
    for(i=0;i<12;i++)surf_tick(0,1);
    assert(!pickup_pops[0].life);
    play();world.beaver.airborne=1;world.beaver.y=PX(40);world.beaver.vy=-1024;
    assert(surf_flip_safe());world.beaver.y=PX(105);world.beaver.vy=256;
    assert(!surf_flip_safe());
    play();world.beaver.airborne=1;world.beaver.y=PX(40);world.beaver.vy=-1024;
    world_step(INPUT_JUMP,&e);assert(world.beaver.flip>0);
    play();world.beaver.lane=world.beaver.lane_to=LANE_BOTTOM;
    world.beaver.y=front_at(world_beaver_x());
    world_step(INPUT_UP,&e);assert(!world.beaver.airborne && world.beaver.lane_to==LANE_BOTTOM);
    world.beaver.airborne=1;world.beaver.y=PX(65);world.beaver.vy=-1024;
    world_step(0,&e);world_step(INPUT_UP,&e);assert(world.beaver.lane_to==LANE_TOP && !world.beaver.flip);
    play();
    for(i=0;i<120;i++) {world_step(INPUT_DOWN|INPUT_DIVE,&e);assert(!world.beaver.airborne && world.beaver.lane_to==LANE_TOP);}
    /* A held tuck never drops a lane by itself, including release/re-press. */
    world_step(0,&e);world_step(INPUT_DOWN|INPUT_DIVE,&e);assert(!world.beaver.airborne && world.beaver.lane_to==LANE_TOP);
    play();world.beaver.airborne=1;world.beaver.y=PX(60);world.beaver.vy=-512;
    world_step(INPUT_DOWN|INPUT_DIVE,&e);assert(world.beaver.lane_to==LANE_BOTTOM && !world.beaver.flip);
    play();
    /* Simultaneous A+Down, including Down held before A, takes a real jump. */
    for(i=0;i<120 && !world.beaver.airborne;i++)world_step(INPUT_JUMP|INPUT_DOWN|INPUT_DIVE,&e);
    assert(world.beaver.airborne && world.beaver.lane_to==LANE_BOTTOM && !world.beaver.flip);
    play();world_step(INPUT_DOWN|INPUT_DIVE,&e);
    for(i=0;i<120 && !world.beaver.airborne;i++)world_step(INPUT_JUMP|INPUT_DOWN|INPUT_DIVE,&e);
    assert(world.beaver.airborne && world.beaver.lane_to==LANE_BOTTOM);
    play();world.beaver.airborne=1;world.beaver.y=PX(48);
    world.gull_present[0]=1;world.gulls[0].x=world.beaver.x+PX(2);world.gulls[0].y=PX(24);
    world.score=100;world.combo=3;world_step(0,&e);
    assert(e.hit && world.score==50 && !world.combo && world.hearts==5);
    world_step(0,&e);assert(!e.hit && world.score>=50);
    play();world.stage=3;surf_tick(0,0);
    n=0;for(i=0;i<PICKUP_SLOTS;i++)if(pickups[i].flags&PICKUP_PRESENT)n++;
    assert(n==4 && pickups[1].x-pickups[0].x==PX(36));
    world.course_distance+=256UL*FP;surf_tick(0,0);
    for(i=4;i<8;i++)assert(pickups[i].flags&PICKUP_BOTTOM);
    puts("Core: source-mask safety, touch/board pickups, counted collection, 12-tick pops, flip guard, explicit lane controls, gull penalty/immunity, four-ring balanced trails pass");
    return 0;
}
