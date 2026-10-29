# GBC rider artwork

Canonical user-supplied PNGs live in `art/`. Color 0 is transparent; opaque
black is index 1, fur `#9C5A3A` is index 2, and board/tail `#9C0031` is index 3.
The runtime palette uses their RGB555 equivalents (19,11,7) and (19,0,6).

`surf.png`, `right.png`, `left.png`, and `jump.png` are 32×32, including eight
empty bottom rows. `board.png`, `board_up.png`, and `board_down.png` are 32×16.
The latest board artwork is taller: the tilted noses reach the top row. Preserve
the entire supplied canvas without cropping or moving its pixels.

The body canvas starts at `(center_x−16, waterline_y−31)`. The board canvas
starts at `(center_x−16, waterline_y−15)`: 16 pixels above a stacked board,
overlapping the body's bottom half. The bottom of the board canvas finishes at
the waterline. The body is drawn in front of the board.

The overlapping lower body and deck are precomposited into four hardware objects
in a small VRAM cache. This preserves both source PNGs and the intended overlap,
but avoids counting eight objects on those scanlines and hiding a nearby gull.
Variant changes are copied during VBlank with the corresponding sprite frame.

Backflips combine the jump pose with that overlapping board at build time.
An upright and a counterclockwise quarter-turn frame plus GBC XY flips supply
all four orientations, without dropping the board during rotation. `tug.png`
uses the supplied Down pose, with eight transparent top rows.

`walk_0.png` and `walk_1.png` are complete 32×32 carrying poses. Do not add
another board in beach mode. Their last ink row is aligned to the ground;
horizontal OAM flipping changes walking direction. Idle holds frame 0.

`walk_back.png` / `walk_back_1.png` are the two supplied up-walk frames;
`walk_front.png` / `walk_front_1.png` are the supplied down-walk frames.
They alternate every eight ticks while moving vertically on either beach;
stopping holds the first pose. Their last ink row aligns with side-walk feet.
Second frames stream through WRAM D600 (front) / D700 (back) into existing
VRAM tiles 168–183 / 184–199 during VBlank. Optimized memcpy staging keeps
the frame budget intact without increasing the 64-tile walking allocation.
`gen_shop_assets.py` must not regenerate any canonical walking PNGs.

Regenerate only actor sheets, leaving background/ramp assets untouched:

```sh
cargo +stable run --manifest-path tools/gen_assets/Cargo.toml -- --gbc-actors-only
make -C gbc GBDK=/path/to/gbdk
```

`make` also regenerates the beach carrying-pose data when either walk PNG
changes. Sprite VRAM allocations remain unchanged from the previous build.
`tools/test_gbc_actors.py` independently decodes the ROM's 2bpp data against
the source PNGs and checks pose selection, rotations, facing, and overlap.
`tools/test_gbc_frames.py` checks the scanline budget including the deck and
effects; optional effects must fit rather than slicing the rider or board.

Collision masks are generated from the source PNG alpha, not the 32×32 canvas.
They use the same pose, quarter-turns, and native coordinate projection as rendering.
Empty top/bottom rows and transparent corners cannot damage the rider or collect
pickups. Hearts use their visible alpha; hoops include their opening as well as
the rim and still require the rider to be airborne. The surfboard itself is not a
damageable part of the beaver. `tools/test_gbc_collisions.py` compares these masks
against the PNGs and exercises actual damage events through the host rules.
