/* Host reference for every entry of the compact hardware wave cache. */
#include <stdio.h>
#include "../src/rules.h"
World world;
int main(void) {
    unsigned tier,tick,i;
    world.wave_scale=16;
    for(tier=0;tier<9;tier++)for(tick=0;tick<1024;tick++) {
        sea_build(tick,tier);
        for(i=tick&3;i<TABLE_LEN;i+=4) {
            unsigned value=(unsigned short)world.heights[i];
            putchar(value&255);putchar(value>>8);
        }
    }
    return 0;
}
