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

## Checklist for future changes

Treat the allocations below as project reservations, not as a list of all RAM
or tiles the hardware offers. Check the linked map and generated asset counts
again whenever code, artwork, compiler flags or GBDK versions change.

### Display and CPU budgets

- Count both total OAM objects (40) and objects crossing each scanline (10).
  Our two racers already use eight objects on an overlapping row; a ring uses
  the remaining two. Admit whole actors and keep gameplay collisions consistent
  with which actors were actually displayed.
- Budget CPU work and VRAM transfers separately. A legal tile allocation can
  still glitch if its upload runs beyond VBlank. Include palette work, bank
  switches, DMA stalls and OAM copying in that budget.
- Keep ordinary scene uploads in VBlank; blank the LCD during VBlank before
  bulk scene replacement. Do not write palettes during pixel fetch. Preserve
  the HUD/footer raster boundaries and reset SCX/LYC every VBlank.
- Keep simulation/audio near 60 ticks per second independently of presentation.
  Measure presented frames, longest gaps and `frames_dropped`, not the host
  emulator's speed counter. Current race/lower-lane regression floors are 20
  presented FPS, at most six refreshes between presentations, and zero dropped
  simulation ticks. These are minimum guards, not a claim of 60 FPS rendering.
- Avoid adding a per-tick scan, division, banked sample walk or copied structure
  without timing it. Cache display-only work separately from physical decisions.
  Preserve the bounded four-tick queue and test both riders when changing it.

### Reserved memory and artwork

| Region | Current use / rule |
| --- | --- |
| ROM bank 0 | Fixed code, interrupts and safe wave-bank reader |
| ROM banks 1-7 | Rules, wave reference, assets and scene/actor/race code; inspect source bank pragmas before adding code |
| ROM banks 8-25 | Exact wave refresh cache: nine tiers, two 16KB banks per tier |
| Cartridge | 512KB MBC5 ROM, 8KB battery RAM; preserve BVR1/BVS1 save formats |
| Ordinary WRAM below C500 | Linked data currently ends at C49C; recheck `s__HEAP` after every build |
| C500-C58F / C590-C61F | Original / staged beach background patterns |
| C630-C6EF | Shop top-row backup |
| C700-C717 | Timing-test delay stub only; keep clear or relocate the helper when reserving new memory |
| C900-C9FF / CA00-CAFF | Alternating OAM pages |
| CB00-CBFF | Background attribute lookup |
| CC00-CDFF / CE00-CFFF | Shadow tile / attribute maps |
| D000-D3FF / D400-D7FF | Original / staged beach actor patterns |
| D800-D87F / D880-D8FF | Player / rival lower-body composite caches |
| D900-DCFF | Shop backup of surfing ramp patterns; leave stack headroom above it |
| VRAM bank 0, tiles 160-163 / 164-171 | Small ring / independent rival composite |
| VRAM bank 1, tiles 70-255 | Main ramp allocation; first 64 slots are temporarily reused on foot and restored for surfing |
| VRAM bank 0, tiles 194 onward | Ramp overflow, scenery and font; use generated counts and compile-time fit assertions, not assumed spare space |

The shop's existing 64-pattern scene slot holds 38 room patterns, 18 palm
patterns and two flag patterns. The palm and flag are background stamps and
consume no OAM objects. Background palettes are also scene-dependent: beach
sand uses 3, open water 4, shoreline animation 6 and palm 7. Shop interiors and
surfing reuse palettes, so preserve and restore their actual loaded values.
The HUD borrows palettes 0 and 5 for its footer; retain the raster restoration.

The two VRAM banks are not one interchangeable tile pool. Tile index, bank bit,
palette and signed/unsigned addressing mode must agree. In particular, the
footer changes addressing mode and hides actors; verify the first HUD row and
line 128 when changing it. Keep the 126-byte trace format and packed World
layout intact, or update every assembly offset assertion and test fixture that
reads it.

### Lessons from failed approaches

| Approach / assumption | What went wrong | Keep instead |
| --- | --- | --- |
| Count tiles and assume hardware safety | Legal tile counts did not prevent top-row corruption from late uploads | Time all transfer paths, including beach palette/pose changes |
| Drain every owed race tick before drawing | Two-rider CPU demand repeatedly postponed presentation, reproducing about 2 FPS | Bound catch-up before race drawing and service ticks while a frame waits for VBlank |
| Recalculate periodic waves on the SM83 | Repeated arithmetic consumed the CPU budget | Bake exact samples into ROM and verify them against the C reference |
| Trust emulator DMA timing alone | Unmodified PyBoy copied GDMA data without charging its CPU stall | Run the timing helper and still test a physical cartridge |
| Put the timing stub at D900 | It overlaps the shop's ramp backup | Reserve C700 for the helper and audit absolute addresses |
| Rescale every sample during lower-meter crest searches | Display-only calculations slowed lower-lane play | Search raw samples, shape endpoints and cache the visible target for four ticks |
| Wrap both wave drawings at 512px | The lower lane's 1024px physical period no longer matched its drawing | Retain the full lower-lane phase and test both halves |
| Multiply taller lower waves directly in signed 16-bit math | Larger intermediate products can overflow even if the final height fits | Reduce fractional precision before multiplication; test extreme tiers |
| Increase difficulty by time, then reset by stage | A long first crossing could become harder than the next stage | Use the stage tier consistently, capped at eight |

### Verification before shipping

Build first so the ROM, map, NoICE symbols and assembler listings describe the
same binary. Tests install emulator fixtures at safe simulation/render boundaries;
do not overwrite World while a rider exchange is partly complete.

| Change | Required checks from repository root |
| --- | --- |
| Any GBC rules change | `sh tools/check_gbc.sh` (address/undefined-behavior sanitizers) |
| Any shipping ROM | `python3 tools/check_header.py web/beaver.gbc` |
| Waves, bank layout or memory | `python3 tools/test_gbc_wave_runtime.py`, plus `test_gbc_lower_wave.py` |
| Scheduling, DMA, palettes, scene loading | `test_gbc_hardware.py`, `test_gbc_shore.py`, `test_gbc_shore_palette.py`, `test_gbc_shop.py`, `test_gbc_progress.py` |
| Sprites, collision or controls | `test_gbc_frames.py`, `test_gbc_actors.py`, `test_gbc_collisions.py`, `test_gbc_controls.py`, `test_gbc_flips.py` |
| Rival or destination beach | `test_gbc_race.py`, `test_gbc_finish_beach.py`, and the hardware timing test |

All `test_gbc_*.py` names above are under `tools/`; run them with a Python
environment containing PyBoy and Pillow. Build with the project's GBDK 4.5.0
setup, or re-establish the timing baseline when upgrading the toolchain. Follow
[SURF_RACE.md](SURF_RACE.md#verification) for example commands. Include stages
1, 2, 3, 8/9 and 255, both lanes and scrolling wraps in relevant coverage.
When changing generated assets, regenerate them and check the tile bounds.
Rebuild `web/beaver.gbc`, update its browser cache key when its bytes change,
and confirm the preview serves that ROM. A fresh source build should reproduce
the shipping ROM; do not rely only on an incremental build.

Finally test the resulting ROM on real GBC hardware: start-beach walking and
vertical poses; shop entry/exit and palette restoration; a paid race with both
riders, rings, flips and pause/results; lower-to-upper transfers; later wave
tiers; and the finish flag, far shoreline and next-stage entry. Look for top-row
corruption, footer seams, sprite flicker, missing actors and long visual stalls.
Record the ROM/commit and hardware used. Emulator checks establish a regression
baseline; they cannot certify every cartridge, display or physical timing path.
