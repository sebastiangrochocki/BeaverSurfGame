/* Exercise real rider integration over whole wave cycles, not only input flags. */
#include <assert.h>
#include <stdio.h>
#include "../src/rules.h"
#include "../src/race.h"
static Events e;
static void setup(unsigned tier,unsigned tick,unsigned x,int16_t speed,int16_t vx) {
    world_new();world.phase=PHASE_PLAY;world.stage=(uint8_t)(tier+1);world.tier=(uint8_t)tier;
    world.tick=tick;world.course_distance=1000UL*FP;world.gull_timer=world.ring_timer=60000;
    world.beaver.x=(int16_t)(x*FP);world.beaver.lane=world.beaver.lane_to=LANE_BOTTOM;
    world.speed=speed;world.beaver.vx=vx;sea_build(tick,(uint8_t)tier);
    world.beaver.y=front_at(world_beaver_x());
}
int main(void) {
    unsigned tier,t,x,i,attempts,wins,early=0;long previous=0;
    for(tier=0;tier<9;tier++) {
        long span=0;attempts=wins=0;
        for(t=0;t<1024;t+=32) {
            int16_t lo=32767,hi=-32767;
            setup(tier,t,96,SCROLL_SPEED,0);
            for(x=0;x<1024;x+=4) {
                int16_t h=front_at((int32_t)x*FP);
                assert(h<=PX(150) && h>PX(90));
                if(h<lo)lo=h;if(h>hi)hi=h;
            }
            span+=hi-lo;
            for(x=24;x<212;x+=24) {
                setup(tier,t,x,SCROLL_SPEED,0);
                world_step(INPUT_JUMP|INPUT_RIGHT,&e);
                if(!e.jumped)continue;
                attempts++;early+=!world.beaver.perfect;
                world_step(INPUT_UP|INPUT_RIGHT,&e);
                for(i=0;i<160 && world.beaver.airborne;i++)world_step(INPUT_RIGHT,&e);
                assert(!world.beaver.airborne && !e.wipeout);
                if(world.beaver.lane==LANE_TOP)wins++;
            }
        }
        assert(span>previous);previous=span;
        assert(attempts>100 && wins==attempts);
        printf("Stage %u: lower wave swing %.1f px, upward transfers %u/%u\n",tier+1,span/2048.,wins,attempts);
    }
    assert(early>500);
    /* At the exact same wave position, speed and rightward momentum add lift.
     * Every early launch retains its earned surf speed. */
    {
        unsigned checked=0,held=0;int16_t slow,fast,moving;
        for(t=0;t<256;t+=8)for(x=48;x<180;x+=16) {
            setup(0,t,x,SCROLL_SPEED,0);world_step(INPUT_JUMP,&e);
            if(!e.jumped || world.beaver.perfect)continue;
            slow=world.beaver.vy;assert(slow<=EARLY_JUMP_VY);
            setup(0,t,x,VEL*2,0);world_step(INPUT_JUMP,&e);
            if(!e.jumped || world.beaver.perfect)continue;
            fast=world.beaver.vy;assert(world.speed>VEL*2-VEL/8);
            assert(fast<slow);
            setup(0,t,x,VEL*2,VEL);world_step(INPUT_JUMP,&e);
            if(!e.jumped || world.beaver.perfect)continue;
            moving=world.beaver.vy;assert(moving<fast);checked++;
        }
        assert(checked>50);
        /* Holding Up before A and A+Up both request one physical climb. */
        for(t=0;t<256;t+=16) {
            setup(0,t,96,SCROLL_SPEED,0);world_step(INPUT_UP,&e);
            assert(!world.beaver.airborne && world.beaver.lane==LANE_BOTTOM);
            for(i=0;i<120 && !world.beaver.airborne;i++)world_step(INPUT_UP|INPUT_JUMP,&e);
            assert(world.beaver.airborne && world.beaver.lane_to==LANE_TOP);
            for(i=0;i<160 && world.beaver.airborne;i++)world_step(INPUT_RIGHT,&e);
            assert(!world.beaver.airborne && world.beaver.lane==LANE_TOP && !e.wipeout);held++;
        }
        printf("Speed/momentum lift: %u matched early launches; %u held-Up climbs\n",checked,held);
    }
    /* A long stage never silently grows to the hardest tier then resets. */
    setup(0,0,96,SCROLL_SPEED,0);course_length=100000UL*FP;
    for(i=0;i<9000;i++)world_step(0,&e);
    assert(world.phase==PHASE_PLAY && world.tier==0);
    world.phase=PHASE_BEACH_END;world_next_stage_beach();assert(world.tier==1);
    puts("Stage wave growth, visible troughs, real upward landings, retained speed and momentum pass");
}
