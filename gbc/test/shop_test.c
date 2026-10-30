/* Host-side real world_step coverage; no renderer or emulator state injection. */
#include <assert.h>
#include <stdio.h>
#include "../src/rules.h"
#include "../src/shop.h"
static Events events;
static void step(uint8_t input,unsigned ticks) {while(ticks--)world_step(input,&events);}
static void tap(uint8_t input) {step(0,1);step(input,1);step(0,1);}
int main(void) {
    uint8_t raw[8],i;
    shop_load((uint8_t[8]){0});
    world_new();tap(INPUT_START);
    assert(world.phase==PHASE_BEACH_START);
    assert(course_length==4000UL*FP);
    step(INPUT_UP,40);assert(world.beaver.y==PX(104) && shop_direction==2);
    step(INPUT_DOWN,80);assert(world.beaver.y==PX(144) && shop_direction==1);
    step(INPUT_UP,40);step(INPUT_RIGHT,48);
    assert(shop_near_door());
    step(INPUT_UP,2);assert(world.phase==PHASE_BEACH_START); /* Up is not A. */
    tap(INPUT_ACTION);assert(world.phase==PHASE_SHOP);
    assert(shop_mode==SHOP_WALK && shop_x==24u*FP);
    tap(INPUT_ACTION);assert(shop_mode==SHOP_WALK); /* No remote counter interaction. */
    step(INPUT_RIGHT,156);assert(shop_x==180u*FP);
    tap(INPUT_ACTION);assert(shop_mode==SHOP_BROWSE);
    tap(INPUT_RIGHT);assert(shop_selected==1);
    world.score=3999;tap(INPUT_ACTION);
    assert(shop_message==SHOP_SHORT && world.score==3999 && shop_owned==1);
    world.score=4000;tap(INPUT_ACTION);
    assert(shop_equipped==1 && shop_owned==3 && world.score==0 && world.best==4000);
    world.score=123;step(INPUT_ACTION,120);step(0,1);
    assert(world.score==123); /* Held A cannot repeatedly purchase. */
    tap(INPUT_LEFT);tap(INPUT_ACTION);assert(shop_equipped==0 && world.score==123);
    tap(INPUT_RIGHT);tap(INPUT_ACTION);assert(shop_equipped==1 && world.score==123);
    shop_save(raw);shop_equipped=0;shop_owned=1;shop_load(raw);
    assert(shop_owned==3 && shop_equipped==1);
    for(i=0;i<8;i++) {uint8_t damaged[8],j;for(j=0;j<8;j++)damaged[j]=raw[j];damaged[i]^=1;shop_load(damaged);assert(shop_owned==1 && shop_equipped==0);}
    shop_load(raw);
    tap(INPUT_DIVE);assert(shop_mode==SHOP_WALK);
    for(i=0;i<250 && world.phase==PHASE_SHOP;i++)step(INPUT_LEFT,1);
    assert(world.phase==PHASE_BEACH_START && world.beaver.y==PX(104));
    step(INPUT_RIGHT,120);assert(world.phase==PHASE_PLAY && shop_equipped==1);
    world.course_distance=course_length-1;step(0,2);
    assert(world.phase==PHASE_BEACH_END);
    step(INPUT_RIGHT,84);assert(shop_near_door());
    tap(INPUT_ACTION);assert(world.phase==PHASE_SHOP);
    step(INPUT_LEFT,20);assert(world.phase==PHASE_BEACH_END);
    step(INPUT_RIGHT,50);assert(world.phase==PHASE_BEACH_END && world.stage==1);
    step(INPUT_RIGHT,(unsigned)(BEACH_EXIT_X/FP-world.beach_pan/FP));
    assert(world.phase==PHASE_PLAY && world.stage==2);
    assert(course_length==4500UL*FP);
    assert(shop_equipped==1 && shop_owned==3);
    puts("Shop rules: 2D bounds, A-only doors, counter range, exact price, insufficient funds, no repeat charge, equip, corrupt-save rejection, both beaches and stage continuity pass");
}
