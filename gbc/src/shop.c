#ifdef __SDCC
#pragma bank 6
#endif
#include "shop.h"

uint8_t shop_mode,shop_selected,shop_owned=1,shop_equipped;
uint8_t shop_message,shop_direction,shop_save_pending;
uint8_t shop_palette_pending=1;
uint16_t shop_x;
static uint8_t held, return_phase;
static int16_t return_x,return_y;

uint16_t shop_price(uint8_t board) BANKED {
    return board==1?4000u:board==2?6000u:board==3?10000u:0;
}

uint8_t shop_near_door(void) BANKED {
    if(world.beach_pan>384u*FP)return 0; /* Beyond the house; avoid signed-Q6 wrap. */
    int16_t x=(int16_t)(world.beaver.x+world.beach_pan);
    int16_t door=world.phase==PHASE_BEACH_END?PX(180):PX(96);
    return (uint8_t)(x>=door-PX(14) && x<=door+PX(14) && world.beaver.y<=PX(112));
}

uint8_t shop_beach(uint8_t input) BANKED {
    uint8_t edge=(uint8_t)((input&INPUT_ACTION) && !(held&INPUT_ACTION));
    held=input;
    if(!edge || !shop_near_door()) return 0;
    return_phase=world.phase;
    return_x=world.beaver.x; return_y=world.beaver.y;
    world.phase=PHASE_SHOP;
    world.beaver.vx=world.beaver.vy=0;
    world.beaver.x=PX(36);world.beaver.y=PX(136);
    shop_x=24u*FP;
    shop_mode=SHOP_WALK;shop_selected=shop_equipped;
    shop_message=SHOP_WELCOME;shop_direction=0;
    return 1;
}

void shop_step(uint8_t input) BANKED {
    uint8_t edge=(uint8_t)(input & (uint8_t)~held);
    uint16_t camera;
    held=input;
    world.tick++;
    world.beaver.vx=world.beaver.vy=0;
    if(shop_mode==SHOP_BROWSE) {
        if((edge&INPUT_DIVE) && !(input&INPUT_DOWN)) {
            shop_mode=SHOP_WALK;
        } else {
            if((edge&INPUT_LEFT) && shop_selected) {shop_selected--;shop_message=SHOP_WELCOME;}
            if((edge&INPUT_RIGHT) && shop_selected<SHOP_BOARDS-1) {shop_selected++;shop_message=SHOP_WELCOME;}
            if(edge&INPUT_ACTION) {
                uint8_t bit=(uint8_t)(1u<<shop_selected);
                if(shop_owned&bit) {
                    shop_equipped=shop_selected;shop_message=SHOP_EQUIPPED;shop_save_pending=shop_palette_pending=1;
                } else if(world.score>=shop_price(shop_selected)) {
                    if(world.score>world.best) world.best=world.score;
                    world.score-=shop_price(shop_selected);
                    shop_owned|=bit;shop_equipped=shop_selected;
                    shop_message=SHOP_BOUGHT;shop_save_pending=shop_palette_pending=1;
                } else shop_message=SHOP_SHORT;
            }
        }
    } else {
        if((input&INPUT_RIGHT) && !(input&INPUT_LEFT) && shop_x<SHOP_PLAYER_MAX*FP) {
            shop_x+=FP;world.beaver.vx=VEL;world.facing_left=0;
        }
        if((input&INPUT_LEFT) && !(input&INPUT_RIGHT)) {
            if(shop_x>8u*FP) {shop_x-=FP;world.beaver.vx=-VEL;world.facing_left=1;}
            else {
                world.phase=return_phase;
                world.beaver.x=return_x;world.beaver.y=return_y;
                world.beaver.vx=0;shop_direction=0;
                return;
            }
        }
        if((edge&INPUT_ACTION) && shop_x>=SHOP_COUNTER_TALK_X*FP) {
            shop_mode=SHOP_BROWSE;shop_selected=shop_equipped;shop_message=SHOP_WELCOME;
        }
    }
    /* Scroll the room to the chosen display while talking; never teleport the player. */
    camera=shop_mode==SHOP_BROWSE?(uint16_t)(SHOP_BOARD_CENTER+SHOP_BOARD_STEP*shop_selected):shop_x/FP;
    camera=camera>80?camera-80:0;
    if(camera>SHOP_CAMERA_MAX)camera=SHOP_CAMERA_MAX;
    world.beaver.x=(int16_t)(((int32_t)shop_x-(int32_t)camera*FP)*3/2);
}

void shop_save(uint8_t *raw) BANKED {
    uint8_t i,sum=0;
    raw[0]='B';raw[1]='V';raw[2]='S';raw[3]='1';
    raw[4]=shop_owned;raw[5]=shop_equipped;raw[6]=0;
    for(i=0;i<7;i++)sum=(uint8_t)(sum+raw[i]);raw[7]=sum;
}

void shop_load(const uint8_t *raw) BANKED {
    uint8_t i,sum=0;
    shop_owned=1;shop_equipped=0;shop_save_pending=0;
    shop_palette_pending=1;
    for(i=0;i<7;i++)sum=(uint8_t)(sum+raw[i]);
    if(raw[0]!='B'||raw[1]!='V'||raw[2]!='S'||raw[3]!='1'||raw[7]!=sum) return;
    if((raw[4]&0xf0) || !(raw[4]&1) || raw[5]>=SHOP_BOARDS || !(raw[4]&(1u<<raw[5]))) return;
    shop_owned=raw[4];shop_equipped=raw[5];
}
