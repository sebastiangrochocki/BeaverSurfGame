#ifndef BEAVER_SHOP_H
#define BEAVER_SHOP_H
#include "rules.h"
#define SHOP_BOARDS 4
/* Native pixels: 16-wide wall boards with a 16-pixel clear gap. */
#define SHOP_ROOM_WIDTH 256
#define SHOP_BOARD_LEFT 56
#define SHOP_BOARD_WIDTH 16
#define SHOP_BOARD_STEP 32
#define SHOP_COUNTER_LEFT 184
#define SHOP_KEEPER_LEFT 200
#define SHOP_COUNTER_TALK_X 164
#define SHOP_ROOM_COLUMNS (SHOP_ROOM_WIDTH / 8 + 1)
#define SHOP_BOARD_CENTER (SHOP_BOARD_LEFT + SHOP_BOARD_WIDTH / 2)
#define SHOP_CAMERA_MAX (SHOP_ROOM_WIDTH - 160)
#define SHOP_PLAYER_MAX (SHOP_ROOM_WIDTH - 16)
enum { SHOP_WALK, SHOP_BROWSE };
enum { SHOP_WELCOME, SHOP_SHORT, SHOP_BOUGHT, SHOP_EQUIPPED };
extern uint8_t shop_mode, shop_selected, shop_owned, shop_equipped;
extern uint8_t shop_message, shop_direction, shop_save_pending;
extern uint8_t shop_palette_pending;
extern uint16_t shop_x;
uint16_t shop_price(uint8_t board) BANKED;
uint8_t shop_near_door(void) BANKED;
uint8_t shop_beach(uint8_t input) BANKED;
void shop_step(uint8_t input) BANKED;
void shop_load(const uint8_t *raw) BANKED;
void shop_save(uint8_t *raw) BANKED;
/* The renderer owns all VRAM writes; these are called with LCD off / in VBlank. */
extern uint8_t shop_scene, shop_scroll, shop_loading;
void shop_init(void) BANKED;
void shop_prepare(void) BANKED;
void shop_exterior(uint8_t scx) BANKED;
void shop_present(void) BANKED;
#endif
