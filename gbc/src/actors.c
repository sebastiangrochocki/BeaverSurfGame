#ifdef __SDCC
#pragma bank 5
#include <gb/cgb.h>
#include <gb/gb.h>
#endif
#include "actors.h"
#include "actor_data.h"
#include "race.h"
#include <string.h>

uint8_t actor_pending;
#ifdef __SDCC
static uint8_t __at(0xD800) lower_stage[128];
static uint8_t __at(0xD880) rival_stage[128];
static uint8_t lower_committed=255, lower_wanted;
static uint8_t rival_committed=255,rival_wanted,race_loaded,race_wanted;
#endif

uint8_t actor_pose_for(const Beaver *rider,uint8_t input, uint8_t *mirror) BANKED {
    *mirror=0;
    if(world.phase!=PHASE_PLAY) return 0;
    if(rider->airborne && rider->flip) {
        uint8_t turn=(uint8_t)(((uint16_t)(FLIP_FRAMES-rider->flip)*4)/FLIP_FRAMES);
        if(turn>3) turn=3;
        if(turn>=2) *mirror=0x60;
        return (turn&1)?4:3;
    }
    if(input&INPUT_DIVE) return 1;
    if(rider->airborne) return 2;
    if((input&INPUT_LEFT) && !(input&INPUT_RIGHT)) return 5;
    if((input&INPUT_RIGHT) && !(input&INPUT_LEFT)) return 6;
    return 0;
}
uint8_t actor_pose(uint8_t input,uint8_t *mirror) BANKED {
    return actor_pose_for(&world.beaver,input,mirror);
}

void actor_prepare(uint8_t frame, uint8_t deck) BANKED {
#ifdef __SDCC
    const uint8_t *source;
    lower_wanted=(uint8_t)((frame==255?21:frame*3)+(deck>>3));
    race_wanted=RACING;
    if(race_wanted!=race_loaded)rival_committed=255;
    actor_pending=(uint8_t)((lower_wanted!=lower_committed?1:0)|(race_wanted!=race_loaded?4:0));
    if(!(actor_pending&1)) return;
    source=actor_lower+(uint16_t)lower_wanted*128;
    memcpy(lower_stage,source,128);
#else
    (void)frame;(void)deck;
#endif
}
void actor_prepare_rival(uint8_t frame,uint8_t deck) BANKED {
#ifdef __SDCC
    const uint8_t *source;
    rival_wanted=(uint8_t)((frame==255?21:frame*3)+(deck>>3));
    if(rival_wanted==rival_committed)return;
    actor_pending|=2;source=actor_lower+(uint16_t)rival_wanted*128;
    memcpy(rival_stage,source,128);
#else
    (void)frame;(void)deck;
#endif
}

void actor_present(void) BANKED {
#ifdef __SDCC
    VBK_REG=0;
    if(actor_pending&1) {
        HDMA1_REG=0xD8;HDMA2_REG=0;
        HDMA3_REG=0x87;HDMA4_REG=0;HDMA5_REG=7;
        lower_committed=lower_wanted;
    }
    if(actor_pending&2) {
        HDMA1_REG=0xD8;HDMA2_REG=0x80;
        HDMA3_REG=0x8A;HDMA4_REG=0x40;HDMA5_REG=7;
        rival_committed=rival_wanted;
    }
    if(actor_pending&4) {
        if(race_wanted)set_sprite_data(136,12,actor_race_gulls);
        else {HDMA1_REG=0xD0;HDMA2_REG=0;HDMA3_REG=0x88;HDMA4_REG=0x80;HDMA5_REG=23;}
        race_loaded=race_wanted;
    }
    actor_pending=0;
#endif
}

/* Same floor-to-native conversion as render.c; the vertical VIEW_LIFT cancels. */
static int16_t native_x(int16_t q) {
    int16_t x=(int16_t)(q>>6);
    return x>=0?(int16_t)(x*2/3):(int16_t)-((-x*2+2)/3);
}
static uint8_t ink(const uint8_t *mask,uint8_t x,uint8_t y) {
    return (uint8_t)(mask[(uint16_t)y*4+(x>>3)]&(1u<<(x&7)));
}

uint8_t actor_touches_for(const Beaver *rider,uint8_t input,uint8_t kind,int16_t x,int16_t y) BANKED {
    int16_t qdx=(int16_t)(x-rider->x);
    int16_t qdy=(int16_t)(y-rider->y);
    if(qdx<=-PX(72) || qdx>=PX(72) || qdy<=-PX(64) || qdy>=PX(64)) return 0;
    uint8_t mirror,pose=actor_pose_for(rider,input,&mirror),target;
    const uint8_t *body=actor_body_masks+(uint16_t)pose*128;
    const uint8_t *b=actor_body_bounds+pose*4;
    const uint8_t *other,*t;
    int16_t dx=(int16_t)(native_x(x)-native_x(rider->x));
    int16_t dy=(int16_t)((y>>6)-(rider->y>>6));
    int16_t left,top,right,bottom,px,py;
    uint8_t bl=b[0],bt=b[1],br=b[2],bb=b[3];
    if(mirror&0x20) {bl=(uint8_t)(32-b[2]);br=(uint8_t)(32-b[0]);}
    if(mirror&0x40) {bt=(uint8_t)(32-b[3]);bb=(uint8_t)(32-b[1]);}
    if(kind==ACTOR_GULL || kind==ACTOR_RACE_GULL) {
        target=(uint8_t)((world.tick>>3)&3);if(target==3) target=1;
        dx+=kind==ACTOR_RACE_GULL?8:1;dy+=23;
        if(kind==ACTOR_RACE_GULL)target+=5;
    } else if(kind==ACTOR_HEART) {target=3;dx+=8;dy+=23;}
    else {target=4;dx+=8;dy+=23;}
    other=actor_target_masks+(uint16_t)target*128;
    t=actor_target_bounds+target*4;
    left=dx+t[0];if(left<bl) left=bl;
    top=dy+t[1];if(top<bt) top=bt;
    right=dx+t[2];if(right>br) right=br;
    bottom=dy+t[3];if(bottom>bb) bottom=bb;
    if(left>=right || top>=bottom) return 0;
    for(py=top;py<bottom;py++) for(px=left;px<right;px++) {
        uint8_t bx=(uint8_t)((mirror&0x20)?31-px:px);
        uint8_t by=(uint8_t)((mirror&0x40)?31-py:py);
        if(ink(body,bx,by) && ink(other,(uint8_t)(px-dx),(uint8_t)(py-dy))) return 1;
    }
    return 0;
}
uint8_t actor_touches(uint8_t input,uint8_t kind,int16_t x,int16_t y) BANKED {
    return kind==ACTOR_RING?actor_pickup_touches(&world.beaver,input,x,y):actor_touches_for(&world.beaver,input,kind,x,y);
}
/* Collect with the visible body OR deck; unlike hazards, the ring's hole counts.
 * Bounds come from source alpha, including the tucked pose's empty top band. */
uint8_t actor_pickup_touches(const Beaver *rider,uint8_t input,int16_t x,int16_t y) BANKED {
    uint8_t mirror,pose=actor_pose_for(rider,input,&mirror);
    const uint8_t *b=actor_body_bounds+(uint16_t)pose*4;
    int16_t dx=(int16_t)(native_x(x)-native_x(rider->x));
    int16_t dy=(int16_t)((y>>6)-(rider->y>>6));
    int16_t l=(mirror&0x20)?32-b[2]:b[0],r=(mirror&0x20)?32-b[0]:b[2];
    int16_t t=(mirror&0x40)?32-b[3]:b[1],bt=(mirror&0x40)?32-b[1]:b[3];
    if(dx+10>l-16 && dx-10<r-16 && dy+10>t-31 && dy-10<bt-31)return 1;
    return (uint8_t)(!rider->flip && dx>-26 && dx<26 && dy>-25 && dy<11);
}
