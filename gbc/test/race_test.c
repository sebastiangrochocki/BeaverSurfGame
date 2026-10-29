#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/race.h"
#include "../src/surf.h"
static Events e;
static void step(uint8_t input) {world_step(input,&e);}
static void beach(uint32_t money) {
    world_new();world.phase=PHASE_BEACH_START;world.stage=2;
    course_length=4500UL*FP;world.beaver.x=PX(144);world.beaver.y=PX(136);world.score=money;
}
static void accept_race(uint32_t money) {
    beach(money);assert(race_near());
    step(INPUT_ACTION);assert(race.state==RACE_OFFER && !race.yes);
    step(INPUT_LEFT);step(INPUT_ACTION);
}
static void until_surf(void) {
    unsigned n=0;
    while(race.state!=RACE_ACTIVE && n++<500)step(INPUT_ACTION|INPUT_LEFT|INPUT_DOWN);
    assert(RACING && world.phase==PHASE_PLAY);
    assert(world.beaver.lane==0 && race.rival.beaver.lane==1);
}
static void settlement(uint32_t player,uint32_t npc,uint16_t payout) {
    uint32_t wallet;
    accept_race(3000);until_surf();wallet=race.wallet;
    world.score=player;race.rival.score=npc;
    world.phase=PHASE_BEACH_END;race_finish();
    assert(race.state==RACE_RESULTS && race.payout==payout);
    assert(world.score==wallet+player+payout && world.hearts==START_HEARTS);
    race_finish();assert(world.score==wallet+player+payout);
    step(0);step(INPUT_ACTION);assert(race.page==1);
    step(INPUT_ACTION);assert(race.state==RACE_RESULTS);
    step(0);step(INPUT_ACTION);assert(race.state==RACE_DONE);
}
int main(void) {
    uint32_t start,distance;
    uint8_t other,n;
    unsigned t,peaks=0;
    char top[32],bottom[32];
    /* Beach solid bounds and camera safety: the 32x24 walking art must not overlap. */
    beach(3000);world.beaver.x=PX(90);world.beaver.y=PX(136);
    for(t=0;t<100;t++)step(INPUT_RIGHT);
    assert(world.beaver.x==BEACH_CAMERA_X && !world.beach_pan && !world.beaver.vx);
    assert(race_near());step(INPUT_ACTION);assert(race.state==RACE_OFFER);
    step(0);step(INPUT_DIVE);assert(race.state==RACE_IDLE);
    for(t=0;t<24;t++)step(INPUT_UP);
    assert(world.beaver.y==PX(112));
    for(t=0;t<48;t++)step(INPUT_RIGHT);
    assert(world.beaver.x+world.beach_pan==PX(144));
    for(t=0;t<50;t++)step(INPUT_DOWN);
    assert(world.beaver.y==PX(112) && !world.beaver.vy); /* Top edge blocks. */
    assert(race_near()); /* Can still talk from above. */
    world.beaver.x=PX(96);world.beach_pan=PX(96);world.beaver.y=PX(136);
    {uint32_t visual=world.visual_scroll;step(INPUT_LEFT);
        assert(world.beach_pan==PX(96) && world.visual_scroll==visual && !world.beaver.vx);}
    world.beaver.x=PX(96);world.beach_pan=0;world.beaver.y=PX(108);
    assert(!race_near()); /* Shop doorway wins over the expanded talk reach. */
    beach(3000);step(INPUT_UP);assert(world.beaver.y<PX(136)); /* Escape old overlap safely. */
    beach(1000);step(INPUT_ACTION);step(INPUT_DIVE);
    assert(race.state==RACE_IDLE && world.score==1000);
    accept_race(999);assert(race.state==RACE_OFFER && race.short_funds && world.score==999);
    accept_race(1000);assert(race.state==RACE_COUNTDOWN && !world.score && !race.wallet);
    start=world.beaver.x;for(t=0;t<179;t++)step(INPUT_RIGHT|INPUT_ACTION);
    assert(world.beaver.x==start && race.state==RACE_COUNTDOWN);
    until_surf();distance=world.course_distance;
    step(0);assert(world.course_distance==distance+2UL*FP);
    assert(world.tick>0 && world.score==0); /* No passive contest income. */
    /* Exactly simultaneous pickup contacts choose the player, then disappear once. */
    surf_clear();world.beaver.x=PX(96);world.beaver.y=PX(112);
    race.rival.beaver=world.beaver;
    pickups[0].x=PX(96);pickups[0].y=PX(107);pickups[0].flags=PICKUP_PRESENT|PICKUP_SHOWN;
    n=surf_collect(&race.rival.beaver,0,0,&other);assert(n==1 && !other);
    assert(!surf_collect(&race.rival.beaver,0,0,&other));
    /* Run a full duel with normal world_step: no KO, no hidden second target. */
    world.hearts=1;race.saved_health=1;
    for(t=0;t<2600 && RACING;t++) {
        uint8_t i,active=0;
        for(i=0;i<PICKUP_SLOTS;i++)if(pickups[i].flags&PICKUP_PRESENT) {pickups[i].flags|=PICKUP_SHOWN;active++;}
        for(i=0;i<GULL_SLOTS;i++)if(world.gull_present[i])active++;
        assert(active<=1);if(active)peaks++;
        surf_gull_shown=15;
        step(t%40==0?INPUT_JUMP:0);
        assert(world.phase!=PHASE_OVER);
    }
    assert(peaks && race.state==RACE_RESULTS && world.hearts==1);
    assert(race_hud(top,bottom,0) && strlen(top)<=20 && strlen(bottom)<32);
    settlement(400,200,2000);settlement(200,400,0);settlement(300,300,1000);
    puts("Race: offers/cancel/funds, single escrow, countdown lock, aligned run-in, shared clock, no passive income, contested ties, sequential targets, full duel/no KO, health restore, all payouts and held-A/idempotent result flow pass");
}
