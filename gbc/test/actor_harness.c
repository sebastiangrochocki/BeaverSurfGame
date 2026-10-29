/* Host-only adapter for independent PNG/mask and actual rules-event tests. */
#include <string.h>
#include "../src/actors.h"
void actor_test_state(uint8_t air,uint8_t flip,uint32_t tick) {
    memset(&world,0,sizeof world);
    world.phase=PHASE_PLAY;world.beaver.x=PX(96);world.beaver.y=PX(112);
    world.beaver.airborne=air;world.beaver.flip=flip;world.tick=tick;
}
uint8_t actor_test_step_gull(uint8_t input,int16_t y) {
    Events events;
    world_new();
    world.phase=PHASE_PLAY;world.course_distance=400UL*FP;
    world.beaver.x=PX(96);world.beaver.y=PX(48);world.beaver.airborne=1;
    world.speed=MIN_SPEED;world.gull_timer=world.ring_timer=600;
    world.gull_present[0]=1;
    world.gulls[0].x=(int16_t)(PX(96)+(MIN_SPEED>>2)+FP);
    world.gulls[0].y=PX(y);
    world_step(input,&events);
    return events.hit;
}
