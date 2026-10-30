# GBC hardware performance repair

The reproduced race slowdown came from CPU saturation and an unbounded tick-drain
loop: the previous ROM presented 40 frames over 1,200 display refreshes (2 FPS),
while simulation work repeatedly postponed drawing. Tile allocation alone could
not protect the display. The beach palette/VRAM commit could also overrun VBlank,
writing into the next frame's first rows.

## Changes

- Bake the exact 1,024-tick wave cycle for all nine wave tiers into banks 8-25.
  Only the sixteen samples refreshed on each tick are stored. The original
  calculation remains the host reference and handles full table construction.
- Use a fixed-bank reader to switch ROM banks safely. The MBC5 cartridge is now
  512 KB, with the existing 8 KB battery RAM and save formats.
- Skip the undisplayed race jump meter; exchange compact rider state in assembly;
  avoid scanning unused race pickup slots and use fast sprite-cache copies.
- Give race drawing a turn after two owed ticks once debt is down to one tick.
  Service remaining debt while waiting for VBlank. Keep full catch-up for solo
  play, preserving the 60 Hz rules/audio clock in both modes.
- Cache beach map rows by camera tile column. Build its two distinct terrain rows
  once and copy them down the beach instead of calculating every cell repeatedly.
- Upload only dirty HUD, wave, scenery and footer bands. HUD attributes are fixed
  bank-1/palette-0 bytes initialized with the LCD off. Scrolling scenery normally
  updates one column instead of two whole map planes.
- Copy OAM once per presentation and disable GBDK's duplicate automatic transfer.
  Reset the top raster state every VBlank. Disable the LCD only during VBlank.
- Remove race gull spawning and avoidance. Contests reward rings, flips, airtime
  and clean landings; solo gulls retain their original behavior.

## Hardware timing regression

`tools/test_gbc_hardware.py` executes the linked ROM through normal joypad input,
including beach walking, streamed front/back poses, palette cycling, shop entry
and exit, races, both lanes, flips, and race settlement. Fresh stage fixtures are
installed between simulation calls, so the test cannot overwrite a half-exchanged
rider state.

PyBoy's general DMA implementation currently copies data without advancing CPU
time (`core/mb.py`, `HDMA.set_hdma5`). The test helper executes a conservative delay
of 64 double-speed CPU clocks per DMA block, plus call overhead, in unused WRAM
at C700. It checks DMA starts/ends, direct scenery writes, and raster palette
writes against LCD timing. This timing follows the
[Pan Docs VRAM DMA specification](https://gbdev.io/pandocs/CGB_Registers.html#lcd-vram-dma-transfers).
It is an emulator regression model; it does not replace a physical cartridge test.

Measured over 1,200 refreshes per race scenario:

| Scenario | Presented FPS | Longest gap | Dropped simulation ticks |
| --- | ---: | ---: | ---: |
| Idle beach | 60 | 1 refresh | 0 |
| Finish beach, walking | 55.8 | — | 0 |
| Lower-lane pumping, stages 1 / 3 / 9 | 23.1 / 26.6 / 24.9 | ≤6 refreshes | 0 |
| Race, stage 2 | 22.6 | 5 refreshes | 0 |
| Race, stage 3 | 23.5 | 4 refreshes | 0 |
| Race, stage 8 | 22.4 | 5 refreshes | 0 |
| Race, stage 255 | 23.1 | 5 refreshes | 0 |

The test requires at least 20 presented FPS and at most six refreshes (100 ms) between
presentations. It also checks the shared 60 Hz clock, no gulls, the ten-object
scanline ceiling, and no VRAM or raster-palette timing violations.

`python3 tools/test_gbc_wave_runtime.py` compares every one of the 147,456 cached
samples against the original C engine and the actual linked ROM bank bytes.
Existing native sanitized rules and ROM controls, sprites, shop, race, shoreline
palette and progress-HUD checks remain applicable.

Build with `make -j4 -C gbc GBDK=/private/tmp/gbdk` (or your GBDK path), then run
the verification commands in [SURF_RACE.md](SURF_RACE.md). The shipping ROM is
`web/beaver.gbc`; the browser cache key is version 69.

The finish beach reuses the existing scrolling tilemap and adds 64 pixels of
sand (eight columns) after the house, so sea is visible beside the flag.
Palm patterns use 18 of the shop's existing
64 pattern slots; two more patterns form the flag, with existing font digits.
Neither decoration consumes sprite objects. Ordinary WRAM ends at C49C, below
the fixed buffers at C500. `tools/test_gbc_finish_beach.py` checks flags 2, 10,
100 and 255, both shorelines, stage transitions, scanline limits and DMA timing.

The lower-lane physics update keeps the same wave ROM, sprite allocation and
60Hz simulation. Crest scaling uses bounded shift/add arithmetic with reduced
fractional precision before multiplication to avoid signed 16-bit overflow.
Crest searches compare unscaled samples and shape only their final result.
The lower display meter refreshes its target every four ticks and retains its
existing per-tick glide; physical jump timing and lane contacts stay exact at
60Hz. This raised the targeted lower-lane stage-1 pumping test from 15.9 to
23.1 presented FPS, with no dropped ticks.
`tools/test_gbc_lower_wave.py` compares the physical contact height and rendered
line against the actual wave table across both halves of its 1024px period.
The native wave-physics sweep covers stages 1-9, speed/momentum takeoff lift,
early jumps, held-Up transfers and real landings on the upper surface.
