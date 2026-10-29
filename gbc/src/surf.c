#ifdef __SDCC
#pragma bank 7
#endif
#include <string.h>
#include "surf.h"
#include "actors.h"
#include "race.h"
Pickup pickups[PICKUP_SLOTS];
PickupPop pickup_pops[POP_SLOTS];
uint8_t surf_jump_buffer,surf_up_held,surf_down_held,surf_gull_shown;
static uint32_t next_trail;
static uint8_t trail;

void surf_clear(void) BANKED {
    memset(pickups,0,sizeof pickups);
    memset(world.ring_present,0,sizeof world.ring_present);
}
void surf_reset(void) BANKED {
    surf_clear(); memset(pickup_pops,0,sizeof pickup_pops);
    surf_jump_buffer=surf_up_held=surf_down_held=0;
    surf_gull_shown=15;next_trail=256UL*FP;trail=0;
}
static void pop_at(const Pickup *p) {
    uint8_t i;
    for(i=0;i<POP_SLOTS;i++) if(!pickup_pops[i].life) {
        pickup_pops[i].wx=world.scroll+p->x;
        pickup_pops[i].y=p->y;pickup_pops[i].life=12;return;
    }
}
/* Triangular movement is deterministic, bounded and inexpensive on the SM83. */
static int16_t pickup_y(uint8_t i) {
    const Pickup *p=&pickups[i];
    int16_t lift=PX(10),bob=0;
    if(p->flags&PICKUP_BOB)lift=PX(34);
    if(p->flags&PICKUP_HIGH)lift=PX(52);
    if(p->flags&PICKUP_BOB) {
        uint8_t phase=(uint8_t)((world.tick+(uint16_t)i*13u)&127u);
        uint8_t t=phase<64?phase:(uint8_t)(127-phase);
        bob=(int16_t)((int16_t)t*(world.stage>=6?40:24)-((world.stage>=6?40:24)*32));
    }
    return (int16_t)(lane_surface(world.scroll+p->x,(p->flags&PICKUP_BOTTOM)?1:0)-lift+bob);
}
void surf_tick(int16_t step,uint8_t racing) BANKED {
    uint8_t i,j,count,flags,slot;
    for(i=0;i<POP_SLOTS;i++)if(pickup_pops[i].life)pickup_pops[i].life--;
    if(world.course_distance<=256UL*FP || world.course_distance>=course_length-384UL*FP) {
        surf_clear();return;
    }
    for(i=0;i<PICKUP_SLOTS;i++)if(pickups[i].flags&PICKUP_PRESENT) {
        pickups[i].x-=step;
        if(pickups[i].x<PX(-24))pickups[i].flags=0;
        else if(((world.tick+i)&3)==0)pickups[i].y=pickup_y(i);
    }
    if(!racing && world.course_distance>=next_trail) {
        next_trail+=256UL*FP;
        count=world.stage>=3?4:3;
        flags=PICKUP_PRESENT|((trail&1)?PICKUP_BOTTOM:0);
        if((trail&7)>=6)flags|=PICKUP_HIGH;
        if(world.stage>=3 && (trail&2))flags|=PICKUP_BOB;
        /* Reserve a complete group: never produce a partial, lane-biased trail. */
        slot=0;for(i=0;i<PICKUP_SLOTS;i++)if(!pickups[i].flags)slot++;
        if(slot>=count)for(j=0;j<count;j++)for(i=0;i<PICKUP_SLOTS;i++)if(!pickups[i].flags) {
            pickups[i].x=(int16_t)(PX(264)+(int16_t)j*PX(36));
            pickups[i].flags=flags;pickups[i].y=pickup_y(i);break;
        }
        trail++;
    }
    /* Keep the legacy three-slot trace meaningful without changing its wire layout. */
    memset(world.ring_present,0,sizeof world.ring_present);
    for(i=0,j=0;i<PICKUP_SLOTS && j<RING_SLOTS;i++)if(pickups[i].flags&PICKUP_PRESENT) {
        world.ring_present[j]=1;world.rings[j].x=pickups[i].x;world.rings[j].y=pickups[i].y;
        world.ring_lane[j++]=(pickups[i].flags&PICKUP_BOTTOM)?1:0;
    }
}
uint8_t surf_collect(const Beaver *rival,uint8_t input,uint8_t rival_input,uint8_t *rival_count) BANKED {
    uint8_t i,n=0;
    *rival_count=0;
    for(i=0;i<PICKUP_SLOTS;i++)if((pickups[i].flags&(PICKUP_PRESENT|PICKUP_SHOWN))==(PICKUP_PRESENT|PICKUP_SHOWN)) {
        int16_t dx=(int16_t)(pickups[i].x-world.beaver.x),dy=(int16_t)(pickups[i].y-world.beaver.y);
        uint8_t a=(uint8_t)(dx>-PX(42) && dx<PX(42) && dy>-PX(48) && dy<PX(48) && actor_pickup_touches(&world.beaver,input,pickups[i].x,pickups[i].y));
        uint8_t b=0;
        if(rival) {
            dx=(int16_t)(pickups[i].x-rival->x);dy=(int16_t)(pickups[i].y-rival->y);
            b=(uint8_t)(dx>-PX(42) && dx<PX(42) && dy>-PX(48) && dy<PX(48) && actor_pickup_touches(rival,rival_input,pickups[i].x,pickups[i].y));
        }
        if(a && b) {
            int16_t ax=(int16_t)((world.beaver.x-pickups[i].x)>>6),ay=(int16_t)((world.beaver.y-pickups[i].y)>>6);
            int16_t bx=(int16_t)((rival->x-pickups[i].x)>>6),by=(int16_t)((rival->y-pickups[i].y)>>6);
            /* Both intersect the same 16px pickup: squared distances stay small. */
            if((int32_t)ax*ax+(int32_t)ay*ay <= (int32_t)bx*bx+(int32_t)by*by)b=0;else a=0;
        }
        if(a||b) {pop_at(&pickups[i]);pickups[i].flags=0;if(a)n++;else (*rival_count)++;}
    }
    return n;
}
uint8_t surf_flip_safe(void) BANKED {
    uint8_t t;
    int16_t v=world.beaver.vy,y=world.beaver.y;
    uint16_t x=(uint16_t)(world.scroll+world.beaver.x)&0x7fff;
    int16_t dx=(int16_t)((RACING?PX(2):(world.speed>>2))+(world.beaver.vx>>2));
    /* Five-tick ballistic recurrence avoids costly 32-bit multiplies/divides.
     * 51*(1+2+3+4+5)=765; retain six samples through the full 30-tick guard. */
    for(t=0;t<6;t++) {
        y=(int16_t)(y+((v*5+765)>>2));v+=255;
        x=(uint16_t)(x+dx*5)&0x7fff;
        if(y>=lane_surface((int32_t)x,world.beaver.lane_to)-PX(2))return 0;
    }
    return 1;
}
