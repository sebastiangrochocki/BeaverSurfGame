# Beach surf shop (GBC)

The shop is a cosmetic upgrade loop. It does not change jump height, speed,
collision masks, ring placement, or difficulty. The classic board is free;
Lagoon costs 4,000 score points, Forest 6,000, and Sunset 10,000. Buying deducts
the displayed score once, unlocks the board, and equips its rider/board palette.
Owned boards can be re-equipped for free. Record scores are preserved before
spending. Currency is the current run's score, not a persistent wallet.

## Controls

- Beach: arrows walk; Up/Down use the supplied two-frame back/front animations.
- Walk near the wooden shop door and press A (browser Z) to enter.
- Interior: Left/Right walk through a 256-pixel scrolling room.
- Approach the counter at the right and press A to talk.
- Left/Right selects a wall board, with an arrow above it and a displayed price.
- A buys/equips; B (browser X) returns to walking. Walk left to leave.

Both start and destination beaches have a shop. Leaving restores the exact
beach position, camera, stage, lives, and course distance. The room does not
advance the surfing course or spawn enemies. Entering water from the lower
part of the beach selects the near lane, rather than snapping to the far lane.

## Save compatibility

The existing `BVR1` best-score record at SRAM A000-A007 is unchanged. A separate
checksummed `BVS1` record at A008-A00F stores an ownership bit mask and equipped
palette. Invalid or missing records fall back to the free classic board. Save
immediately on purchase/equip; the browser's existing SRAM flush handles disk
persistence. Ownership/equipment survive a new run and a cartridge reboot.

## Art and hardware

`tools/gen_shop_assets.py` authors editable 2bpp placeholder wooden siding,
roof, windows, door, wall boards, counter, and a beaver shopkeeper using the
existing character pixels. It bakes a room map to keep scrolling inexpensive.
Front/back PNG pairs live in `gbc/art/walk_front{,_1}.png` and
`walk_back{,_1}.png`; all walking artwork is supplied rather than generated.
Vertical walking switches frames every eight ticks and holds frame 0 at rest.

The room uses a background beaver behind the counter to preserve OAM capacity.
Board choices change sprite palette 0 only, so collision silhouettes stay exact.
Background palette changes inside the shop do not leak into the beach/waves.
Sixty-four ramp patterns in VRAM bank 1 are swapped while walking and restored
byte-for-byte for surfing. On-foot sprite slots expand to 64 tiles, preserving
their original surf/ramp bytes. Bulk scene transitions briefly blank the LCD;
normal scrolling and palette animation remain VBlank-only and synchronized.
The upper wooden wall (y=16..39) uses the same camera offset as the lower
interior (y=40..127), including sub-tile scroll and 7-to-0 wraps. Only the
top title/points and bottom dialogue stay fixed to the screen.
The scroll reset happens in line 127's HBlank, before the dialogue starts at
y=128, so no first-line tile fetch can use a stale room offset. Wall boards are
16×40 native pixels with exactly 16 pixels between their 16-pixel tile bounds.
Board positions, selection-camera targets and the closer counter share the
layout constants in `src/shop.h`.

Fixed WRAM reservations: C500-C58F coast backup; C590-C61F coast staging;
C630-C6EF sky backup; D000-D3FF actor backup; D400-D7FF actor staging;
D800-D87F actor composite cache; D900-DCFF ramp backup. Ordinary data must stay
below C500. OAM/shadow maps remain C900-CFFF and the stack stays above DCFF.

## Verification

Build: `make -C gbc GBDK=/path/to/gbdk`.

Host rules test (run from repository root):

```sh
cc -std=c99 -O1 -fsanitize=undefined,implicit-conversion \
  -fno-sanitize-recover=all gbc/src/rules.c gbc/src/wave.c \
  gbc/src/save.c gbc/src/actors.c gbc/src/shop.c gbc/test/shop_test.c \
  -o /private/tmp/beaver-shop-test
/private/tmp/beaver-shop-test
```

Run `tools/test_gbc_shop.py` with PyBoy to test real joypad interactions,
all prices, insufficient funds, repeated inputs, equip, SRAM reboot, room
scrolling, and exact graphics restoration. Existing actor/collision/frame,
shoreline palette/scroll, and progress tests remain part of the regression pass.

Denser ring challenges and stat-changing board upgrades are intentionally not
part of this first shop implementation.
