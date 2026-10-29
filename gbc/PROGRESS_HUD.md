# Beach-to-beach progress

The fixed 16-pixel footer uses the supplied `art/progress_start.png` island
and `art/progress_finish.png` checkered flag, both 16×16. Between them is a
128-pixel track with a moving 8×8 beaver head; the second row reads `STAGE 001`.
There are no `B` endpoint labels, progress arrows or filling bar. The head moves
through 16 positions and reaches the last one only at the exact finish.

`tools/gen_progress_assets.py` preserves the icon pixels, mapping their pale
backdrop to the existing footer color. It creates eight landmark patterns and
one head pattern at VRAM bank 0, 9000–908F. These are unused by actors and
terrain: only the footer switches to signed BG tile addressing. Existing font
IDs 213–254 address the same patterns in either mode.

After the line-72 interrupt, two separate HBlanks install palettes 0 (flag/head)
and 5 (island). Each unrolled eight-byte copy waits for a fresh Mode-0 edge;
no color-data write is allowed during Mode 3. Palette 0's otherwise-unused
color zero supplies the head's fur without changing the flag. Gameplay colors are derived from `res/bg.png` and
restored every VBlank, even without a newly prepared frame. The line-127 HBlank
resets scroll, selects footer addressing and hides hardware objects. VBlank
restores normal addressing and objects before the next top HUD. The shop keeps
its own palettes, normal addressing, and both fixed dialogue rows.

The raster mode is committed with its prepared tilemap, never read from a newer
world state mid-frame. Landmark/stage cells redraw only after a stage or shop
transition; advancing progress touches only the 16 track cells. Thresholds are
cached, so the per-frame marker update requires no division.

Stage targets are `4000 + (stage−1) × 500` distance units (up to 131,000 for
stage 255). Rules, wave flattening, shoreline and HUD share a cached 32-bit
fixed-point target. The established World/trace layout is unchanged; long
stages cannot wrap at 65,536. Scores keep six digits; stage labels use three.

`gbc/test/course_test.c` checks all 255 lengths through real rules transitions.
`tools/test_gbc_progress.py` checks source shapes, head position, stage reset,
beach flow and the LCD mode at every palette write. The browser emulator test
checks actual flag pixels through idle/walk/surf, not just final palette RAM.
Shop, shoreline palette, actor and 3,000-frame checks cover shared hardware.
