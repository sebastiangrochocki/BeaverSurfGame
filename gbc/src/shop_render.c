#pragma bank 6
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "shop.h"
#include "beach.h"
#include "tiles.h"
#include "shop_data.h"

/* The 64 ramp patterns are unused on foot. Restore their exact bytes for surfing.
 * D800-D87F is the actor cache; the stack is at E000, above this D900-DCFF slot. */
static uint8_t __at(0xD900) pattern_backup[1024];
static uint8_t __at(0xC630) top_backup[192];
static uint16_t palette_backup[20];
static uint8_t loaded_scene, loaded_equipped=255;
uint8_t shop_scene,shop_scroll,shop_loading;

static const uint16_t room_palettes[20]={
    RGB(8,4,3),RGB(3,2,2),RGB(19,11,7),RGB(29,24,17),
    RGB(8,4,3),RGB(3,2,2),RGB(19,0,6),RGB(29,24,17),
    RGB(8,4,3),RGB(3,2,2),RGB(2,23,25),RGB(29,30,23),
    RGB(8,4,3),RGB(3,2,2),RGB(8,23,10),RGB(29,30,23),
    RGB(8,4,3),RGB(3,2,2),RGB(29,19,4),RGB(29,27,20)
};
static const uint16_t rider_palettes[16]={
    RGB(0,0,0),RGB(0,0,0),RGB(19,11,7),RGB(19,0,6),
    RGB(0,0,0),RGB(0,2,4),RGB(21,14,8),RGB(2,23,25),
    RGB(0,0,0),RGB(1,3,1),RGB(18,12,6),RGB(8,23,10),
    RGB(0,0,0),RGB(4,2,0),RGB(23,14,6),RGB(29,19,4)
};

void shop_init(void) BANKED {
    uint16_t i;
    uint8_t index=BCPS_REG;
    VBK_REG=1;
    for(i=0;i<1024;i++)pattern_backup[i]=((volatile uint8_t *)0x8460u)[i];
    for(i=0;i<96;i++)top_backup[96+i]=((volatile uint8_t *)0x9840u)[i];
    VBK_REG=0;
    for(i=0;i<96;i++)top_backup[i]=((volatile uint8_t *)0x9840u)[i];
    for(i=0;i<40;i++) {
        BCPS_REG=(uint8_t)(24+i);((uint8_t *)palette_backup)[i]=BCPD_REG;
    }
    BCPS_REG=index;
}

static void cell(uint8_t row,uint8_t col,uint8_t pattern,uint8_t palette) {
    uint16_t offset=row<9?0x160u+(uint16_t)(row-5)*32u+col:0x40u+(uint16_t)(row-9)*32u+col;
    ((volatile uint8_t *)0xCC00u)[offset]=(uint8_t)(70+pattern);
    ((volatile uint8_t *)0xCE00u)[offset]=(uint8_t)(0x08|palette);
}

void shop_exterior(uint8_t scx) BANKED {
    int16_t left=(world.phase==PHASE_BEACH_END?80:24)-(int16_t)((world.beach_pan>>6)*2/3);
    int16_t col=(left+scx)>>3;
    uint8_t x,y,tile;
    for(y=0;y<4;y++)for(x=0;x<10;x++) {
        int16_t c=col+x;
        if(c<0 || c>20)continue;
        tile=y==0?(x==0?4:x==9?5:3):1;
        if(y==1 && (x==2||x==7))tile=6;
        if(y==2 && x==4)tile=7;
        if(y==2 && x==5)tile=8;
        if(y==3 && x==4)tile=9;
        if(y==3 && x==5)tile=10;
        cell((uint8_t)(6+y),(uint8_t)c,tile,PAL_ISLAND);
    }
    /* Small enamel SURF sign, using the existing bank-0 HUD font. */
    for(x=0;x<4;x++) {
        static const uint8_t letters[4]={32,34,31,19};
        int16_t c=col+3+x;
        if(c>=0 && c<=20) {
            uint16_t offset=0x160u+2u*32u+(uint8_t)c;
            ((volatile uint8_t *)0xCC00u)[offset]=(uint8_t)(213+letters[x]);
            ((volatile uint8_t *)0xCE00u)[offset]=3;
        }
    }
}

void shop_prepare(void) BANKED {
    uint16_t camera;
    uint8_t row;
    shop_scene=world.phase==PHASE_SHOP?2:
        (world.phase==PHASE_BEACH_START || world.phase==PHASE_BEACH_END)?1:0;
    shop_loading=(uint8_t)(shop_scene!=loaded_scene);
    shop_scroll=0;
    if(shop_scene!=2)return;
    camera=shop_mode==SHOP_BROWSE?(uint16_t)(SHOP_BOARD_CENTER+SHOP_BOARD_STEP*shop_selected):shop_x/FP;
    camera=camera>80?camera-80:0;
    if(camera>SHOP_CAMERA_MAX)camera=SHOP_CAMERA_MAX;
    shop_scroll=(uint8_t)(camera&7);
    for(row=5;row<16;row++) {
        uint16_t source=(uint16_t)(row-5)*SHOP_ROOM_COLUMNS+(camera>>3);
        uint16_t offset=row<9?0x160u+(uint16_t)(row-5)*32u:0x40u+(uint16_t)(row-9)*32u;
        memcpy((void *)(0xCC00u+offset),shop_room+source,21);
        memcpy((void *)(0xCE00u+offset),shop_attrs+source,21);
    }
    if(shop_mode==SHOP_BROWSE) {
        int16_t marker=(int16_t)(SHOP_BOARD_LEFT/8+(SHOP_BOARD_STEP/8)*shop_selected)-(int16_t)(camera>>3);
        if(marker>=0 && marker<21)cell(5,(uint8_t)marker,13,7);
    }
}

void shop_present(void) BANKED {
    uint16_t i;
    if(shop_scene!=loaded_scene) {
        VBK_REG=1;
        if((shop_scene==0)!=(loaded_scene==0))
            memcpy((void *)0x8460u,shop_scene?shop_patterns:pattern_backup,1024);
        if(shop_scene==2) {
            for(i=0;i<96;i++)((volatile uint8_t *)0x9840u)[i]=0x08|3;
        } else if(loaded_scene==2) {
            for(i=0;i<96;i++)((volatile uint8_t *)0x9840u)[i]=top_backup[96+i];
        }
        VBK_REG=0;
        if(shop_scene==2) {
            for(i=0;i<96;i++)((volatile uint8_t *)0x9840u)[i]=70+1;
            set_bkg_palette(3,5,room_palettes);
        } else if(loaded_scene==2) {
            for(i=0;i<96;i++)((volatile uint8_t *)0x9840u)[i]=top_backup[i];
            set_bkg_palette(3,5,palette_backup);
            beach_refresh_palettes();
        }
        loaded_scene=shop_scene;
    }
    if(shop_equipped!=loaded_equipped) {
        set_sprite_palette(0,1,rider_palettes+shop_equipped*4);
        loaded_equipped=shop_equipped;
    }
    shop_loading=shop_palette_pending=0;
    VBK_REG=0;
}
