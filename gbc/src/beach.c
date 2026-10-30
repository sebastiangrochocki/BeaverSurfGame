#pragma bank 4
#include <gb/cgb.h>
#include <gb/gb.h>
#include <string.h>
#include "beach.h"
#include "beach_data.h"
#include "tiles.h"
#include "shop.h"

/* Aligned WRAM staging, outside the renderer's C900-CFFF shadow maps. */
static uint8_t __at(0xD000) actor_backup[1024];
static uint8_t __at(0xC500) background_backup[144];
static uint8_t __at(0xD400) actor_stage[1024];
static uint8_t __at(0xC590) background_stage[144];
static uint8_t loaded_background, loaded_actors, want_background, want_actors;
static uint8_t loaded_shore, want_shore;
static uint8_t staged_back, staged_front, walk_pending;
static uint16_t water_palette_backup[4];
static uint16_t palm_palette_backup[4];
static const uint16_t palm_palette[4]={RGB(28,24,17),RGB(9,6,3),RGB(7,19,5),RGB(17,27,9)};
uint8_t beach_active, beach_scx, beach_pending, beach_loading;
uint8_t beach_map_dirty;
static uint8_t map_valid,map_phase,map_ending,map_stage;
static int16_t map_column;
void beach_refresh_palettes(void) BANKED {
    loaded_background=loaded_shore=255;beach_pending=1;
}
static const uint8_t slots[9] = {50,51,52,53,54,55,56,57,62};
static const uint16_t coast_palettes[8] = {
    RGB(28,24,17),RGB(2,9,14),RGB(26,20,13),RGB(28,30,29),
    RGB(2,9,14),RGB(4,15,20),RGB(12,23,25),RGB(28,30,29)
};
static const uint16_t sea_palettes[8] = {
    RGB(2,9,14),RGB(7,21,8),RGB(27,23,14),RGB(9,20,26),
    RGB(2,9,14),RGB(31,31,31),RGB(16,9,4),RGB(26,5,6)
};
/* Palette 6 is free while the beach replaces the surfing waves. Only shoreline
 * cells use it here: inland sand stays on palette 3, open water on palette 4.
 * The screenshot swatches are ordered shadow/sand/water/foam; native tile
 * indices are sand/water/shadow/foam. Cycle the three requested mappings:
 * default -> shadow becomes foam, foam becomes water -> shadow becomes water.
 * The last state restores foam. Sand and the existing water stay unchanged. */
static const uint16_t shore_palettes[12] = {
    RGB(28,24,17),RGB(2,9,14),RGB(26,20,13),RGB(28,30,29),
    RGB(28,24,17),RGB(2,9,14),RGB(28,30,29),RGB(2,9,14),
    RGB(28,24,17),RGB(2,9,14),RGB(2,9,14),RGB(28,30,29)
};
/* Beach water: D/M/L/P -> D/M/M/L -> M/D/L/L, using existing sea colors.
 * Palette 4 is open-water-only here; inland/shop/surf palettes stay separate. */
static const uint16_t beach_water_palettes[12] = {
    RGB(2,9,14),RGB(4,15,20),RGB(12,23,25),RGB(28,30,29),
    RGB(2,9,14),RGB(4,15,20),RGB(4,15,20),RGB(12,23,25),
    RGB(4,15,20),RGB(2,9,14),RGB(12,23,25),RGB(12,23,25)
};

uint8_t beach_is_visible(void) BANKED {
    return world.phase == PHASE_BEACH_START || world.phase == PHASE_BEACH_END ||
        (world.phase == PHASE_PLAY &&
        (world.course_distance < 128UL*FP || world.course_distance > course_length-192UL*FP));
}

void beach_init(void) BANKED {
    uint16_t i;
    uint8_t palette_index=BCPS_REG;
    /* Preserve the actual loaded surf palette, not a separately approximated copy. */
    for(i=0;i<8;i++) {
        BCPS_REG=(uint8_t)(PAL_WATER*8+i);
        ((uint8_t *)water_palette_backup)[i]=BCPD_REG;
        BCPS_REG=(uint8_t)(7*8+i);
        ((uint8_t *)palm_palette_backup)[i]=BCPD_REG;
    }
    BCPS_REG=palette_index;
    VBK_REG=0;
    for(i=0;i<1024;i++) actor_backup[i]=((volatile uint8_t *)0x8880u)[i];
    VBK_REG=1;
    for(i=0;i<144;i++) background_backup[i]=((volatile uint8_t *)(0x8000u+(uint16_t)slots[i>>4]*16u))[i&15];
    VBK_REG=0;
    for(i=0;i<1024;i++) actor_stage[i]=beach_actors[i];
    for(i=0;i<144;i++) background_stage[i]=beach_patterns[i];
    loaded_background=loaded_actors=beach_pending=0;
    loaded_shore=255;
    staged_back=staged_front=walk_pending=0;
}

static void cell(uint8_t row,uint8_t col,uint8_t tile,uint8_t attr) {
    uint16_t offset=row<9 ? 0x160u+(uint16_t)(row-5)*32u+col : 0x40u+(uint16_t)(row-9)*32u+col;
    ((volatile uint8_t *)0xCC00u)[offset]=tile;
    ((volatile uint8_t *)0xCE00u)[offset]=attr;
}

void beach_prepare(void) BANKED {
    int16_t coast;
    int16_t shore_col;
    uint8_t row,col,ending;
    uint16_t pan;
    beach_active=beach_is_visible();
    beach_map_dirty=0;
    want_background=beach_active;
    want_actors=(uint8_t)(world.phase==PHASE_BEACH_START || world.phase==PHASE_BEACH_END || world.phase==PHASE_SHOP);
    want_shore=beach_active?(uint8_t)((world.tick>>5)%3UL):0;
    /* Stream the supplied vertical poses into their existing slots. Expanding
     * the walking allocation would overwrite scenery and HUD glyphs. */
    walk_pending=0;
    if(want_actors && shop_direction && world.phase!=PHASE_SHOP) {
        uint8_t frame=(uint8_t)(world.beaver.vy?((world.tick>>3)&1):0);
        uint8_t back=(uint8_t)(shop_direction==2);
        if(frame!=(back?staged_back:staged_front)) {
            uint16_t offset=back?768:512;
            const uint8_t *pixels=frame?(back?beach_back_second:beach_front_second):beach_actors+offset;
            memcpy(actor_stage+offset,pixels,256);
            if(back)staged_back=frame;else staged_front=frame;
            walk_pending=shop_direction;
        }
    }
    beach_pending=(uint8_t)(want_background!=loaded_background || want_actors!=loaded_actors ||
        (want_background && want_shore!=loaded_shore) || walk_pending);
    beach_loading=(uint8_t)(want_background!=loaded_background || want_actors!=loaded_actors);
    beach_scx=0;
    if(!beach_active) {map_valid=0;return;}
    ending=(uint8_t)(world.phase==PHASE_BEACH_END ||
        (world.phase==PHASE_PLAY && world.course_distance>course_length/2));
    pan=(uint16_t)(((world.beach_pan>>6)*2u)/3u);
    if(world.phase==PHASE_BEACH_START) coast=(int16_t)(112-pan);
    else if(world.phase==PHASE_BEACH_END) coast=(int16_t)(48-pan);
    else if(ending) coast=(int16_t)(48+((course_length-world.course_distance)>>6)*2/3);
    else coast=(int16_t)(48-(world.course_distance>>6)*2/3);
    beach_scx=(uint8_t)(-coast)&7;
    shore_col=(int16_t)((coast+beach_scx)>>3);
    if(map_valid && map_phase==world.phase && map_ending==ending && map_column==shore_col && map_stage==world.stage)return;
    map_valid=beach_map_dirty=1;
    map_phase=world.phase;map_ending=ending;map_column=shore_col;map_stage=world.stage;
    memset((void *)0xCD60u,TILE_SKY0+1,32);
    memset((void *)0xCF60u,0x08|PAL_SKY,32);
    /* Match the surf horizon: one haze row at y=40, terrain from y=48. */
    for(row=6;row<8;row++) for(col=0;col<21;col++) {
        int16_t relative=(int16_t)col-shore_col;
        uint8_t sy=(row-6)&1,pattern,attr=0x08|PAL_ISLAND;
        /* Finished beaches are finite islands: mirrored arrival shore on the
         * left, 64 extra sand pixels after the house, then outgoing surf. */
        if(ending && relative>=(int16_t)BEACH_END_SHORE_COLUMNS) {
            relative-=(int16_t)BEACH_END_SHORE_COLUMNS;
            if(relative<2) {pattern=(uint8_t)(5+(sy<<1)+relative);attr=0x08|PAL_WATER;}
            else {pattern=(uint8_t)(1+(sy<<1)+((uint8_t)relative&1));attr=0x08|PAL_SHIP;}
        } else if((!ending && relative<0) || (ending && relative>=2)) pattern=0;
        else if(relative>=0 && relative<2) {
            pattern=(uint8_t)(5+(sy<<1)+(ending?1-relative:relative));
            attr=0x08|PAL_WATER;
            if(ending) attr|=0x20;
        } else {
            pattern=(uint8_t)(1+(sy<<1)+((uint8_t)relative&1));
            attr=0x08|PAL_SHIP;
        }
        cell(row,col,slots[pattern],attr);
    }
    /* Only two terrain rows differ. Copy their cached patterns/attributes to
     * the remaining rows instead of redoing 210 expensive SM83 cell calls. */
    for(row=8;row<16;row++) {
        uint16_t offset=row<9?0x1C0u:0x40u+(uint16_t)(row-9)*32u;
        uint16_t source=0x180u+(uint16_t)(row&1)*32u;
        memcpy((void *)(0xCC00u+offset),(const void *)(0xCC00u+source),21);
        memcpy((void *)(0xCE00u+offset),(const void *)(0xCE00u+source),21);
    }
    if(want_actors) shop_exterior(beach_scx);
}

#define DMA(src,dst,count) do { HDMA1_REG=(uint8_t)((src)>>8); HDMA2_REG=(uint8_t)(src); HDMA3_REG=(uint8_t)((dst)>>8); HDMA4_REG=(uint8_t)(dst); HDMA5_REG=(count)-1; } while(0)
void beach_present(void) BANKED {
    uint16_t source;
    if(want_background!=loaded_background) {
        source=want_background?0xC590u:0xC500u;
        VBK_REG=1;
        DMA(source,0x8320u,8);
        DMA(source+128u,0x83E0u,1);
        VBK_REG=0;
        if(world.phase!=PHASE_SHOP)set_bkg_palette(3,2,want_background?coast_palettes:sea_palettes);
        if(world.phase!=PHASE_SHOP)set_bkg_palette(7,1,want_background?palm_palette:palm_palette_backup);
        if(!want_background) {
            if(world.phase!=PHASE_SHOP)set_bkg_palette(PAL_WATER,1,water_palette_backup);
            loaded_shore=255;
        }
        loaded_background=want_background;
    }
    if(want_background && want_shore!=loaded_shore) {
        set_bkg_palette(PAL_SHIP,1,beach_water_palettes+(uint8_t)(want_shore*4));
        set_bkg_palette(PAL_WATER,1,shore_palettes+(uint8_t)(want_shore*4));
        loaded_shore=want_shore;
    }
    if(want_actors!=loaded_actors) {
        source=want_actors?0xD400u:0xD000u;
        VBK_REG=0;
        /* Scene changes may exceed one VBlank; render_present blanks the LCD. */
        DMA(source,0x8880u,64);
        loaded_actors=want_actors;
    } else if(want_actors && walk_pending) {
        VBK_REG=0;
        source=walk_pending==2?0xD700u:0xD600u;
        DMA(source,walk_pending==2?0x8B80u:0x8A80u,16);
    }
    walk_pending=0;
    beach_pending=0;
    VBK_REG=0;
}
