/* Actual rules transitions, including courses beyond the old 16-bit boundary. */
#include <assert.h>
#include <stdio.h>
#include "../src/rules.h"
#include "../src/shop.h"

int main(void) {
    Events events;
    unsigned stage;
    world_new();
    assert(course_length==4000UL*FP);
    world_step(INPUT_START,&events);
    assert(world.phase==PHASE_BEACH_START);
    for(stage=1;stage<=255;stage++) {
        unsigned ticks;
        uint32_t expected=(4000UL+(stage-1UL)*500UL)*FP;
        assert(world.stage==stage && course_length==expected);
        world.phase=PHASE_PLAY;
        world.speed=MIN_SPEED;
        world.invuln=255;
        world.course_distance=(stage>=128?65536UL:2000UL)*FP;
        world_step(0,&events);
        assert(world.phase==PHASE_PLAY && world.wave_scale==16);
        world.course_distance=expected-1;
        world_step(0,&events);
        assert(world.phase==PHASE_BEACH_END);
        assert(world.course_distance==expected && world.wave_scale==0);
        for(ticks=0;ticks<BEACH_EXIT_X/FP;ticks++) {
            assert(world.phase==PHASE_BEACH_END && world.stage==stage);
            world_step(INPUT_RIGHT,&events);
        }
        assert(world.phase==PHASE_PLAY && world.course_distance==0);
        assert(world.beaver.x==BEACH_CAMERA_X && !world.beach_pan && !world.wave_scale);
        assert(world.stage==(stage==255?255:stage+1));
    }
    assert(course_length==131000UL*FP);
    puts("All 255 course lengths: 4000 + 500/stage, no early finish or 65536 wrap, exact beach transitions and stage-255 cap pass");
}
