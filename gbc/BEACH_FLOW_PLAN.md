# GBC beach-to-beach presentation

## Approved direction and supplied art

Use the supplied sand (8×8), water (16×16), shoreline (16×16), and assembled
160×144 layout. Split each 16×16 metatile into four hardware 8×8 patterns.
The reference's beach area at y=56..127 contains nine unique 8×8 RGB patterns;
none exceeds three colors per cell. Its blue upper area is a layout placeholder,
not replacement sky art. The bottom 16 pixels belong to the existing fixed HUD.

Preserve the reference's sand texture. Recolor the water portion to match the
surfing water palette; shoreline water pixels use that same water color so the
boundary has no rectangular halo. Allocate a shoreline palette combining sand,
sand shadow, foam, and the matching sea color. Validate every resulting 8×8 cell
before generating indexed PNGs/2bpp data; do not silently quantize or drop pixels.

## Screen and movement

- Keep the current sky/cloud/sun band on both screens, with shared camera offsets.
- Assemble sand on the left, the repeated shoreline around native x=120, and water
  on the right. Use the supplied 160×144 scene as the placement reference.
- Keep the walking feet at the same native screen height as the initial flat sea.
- Keep progress at y=128..143, fixed to the screen through every transition.
- Start with a visible standing beaver holding the board. No input means no walk.
- Left/Right control beach walking. Clamp the left boundary; do not let the player
  disappear off-screen. Use the actual shoreline coordinate for entering water.
- On entering water, change to the surfing pose and start the camera following the
  rider. Scroll the beach off the left, retaining shallow flat water beside it.
- Show waves only farther into the course. No large waves on the beach screen.

The rules currently use 240-wide fixed-point coordinates while the GBC screen is
160 wide. Introduce explicit conversions for native scene boundaries; do not reuse
the existing `BEACH_WATER_X=64` trigger for the new x=120 shoreline.

## Course and ending

Keep a separate camera/world position and a stage distance counter. Camera motion
must remain continuous across phase changes; resetting stage progress must not
reset cloud/parallax offsets. The current reset of `world.scroll` at the end is
part of the discontinuity to remove.

Each stage covers 2,000 distance points, using the already corrected Q6 conversion.
The 2,000-point target is internal only. Show the progress arrow and stage label;
do not display the numeric distance counter to the player. Current courses start
at 4,000 distance units and grow by 500 each stage; the HUD uses a moving head.
Count progress during surfing, not while standing or walking on sand. Score and
trick bonuses remain separate from distance.

Use a short flat entry section, then blend wave amplitude in. During the last
section blend BOTH wave lanes toward calm exit water, resolving the player onto
the beach approach height. Apply the same blend in collision/ride sampling and
rendering. Do not flatten only the displayed waves.

Scroll the destination beach in from the right, using the shoreline in reverse.
Switch to walking with the board when feet cross onto sand. Require player input
to walk to the stage-complete marker. Preserve lives and score, increment the
stage difficulty, then load the next beach without automatic walking.

## Assets and implementation order

1. Keep canonical supplied assets in the repository and generate indexed beach
   patterns/maps with explicit palettes. Use phase-specific pattern slots for
   beach art: current surf banks are already allocated, so do not append tiles
   over ramps, scenery, or the progress font.
2. Replace `prepare_beach()`'s repeated island/land placeholders with the real map.
3. Add actual standing/walking carry-board poses, reusing hardware sprite slots
   between beach and surf phases. Merely moving the existing surf pose is not
   sufficient. Keep the combined beaver/board within scanline limits.
4. Add shared camera coordinates, shoreline triggers, and player-driven walking.
5. Blend flat entry/exit water in both rules and renderer; stream the destination
   shoreline into view. Keep sky and HUD anchored throughout.
6. Test and rebuild the GBC ROM, then update the browser ROM cache key.

## Acceptance checks

- Idle on the start beach for 600 ticks: beaver stays visible, no progress accrues.
- Walk left/right; reach water at the visible shoreline, never at a magic offset.
- Capture beach, shoreline, first surf frame, and exit: matching sky/parallax and
  foot height; unchanged HUD position; no waves painted over sand.
- Compare first/last wave heights against physics; both lanes become calm.
- Exercise course boundaries at 1,999 and 2,000 points and next-stage progress reset.
- Verify carrying/walking and surfing sprite poses, palettes, and scanline budget.
- Decode generated map/patterns against the intended tile pixels; run ROM header,
  frame-budget, and sprite-integrity tests. Inspect actual emulator screenshots.

## Implementation

The GBC ROM now assembles the supplied patterns with `tools/gen_beach_assets.py`
and the banked `src/beach.c` renderer. Beach backgrounds and carrying poses reuse
phase-specific VRAM slots; the original surf assets are restored on departure.
Walking is player-controlled, with a shared sky camera and a fixed bottom HUD.
Both beach and surf use one horizon row at y=40..47. Beach terrain starts at
y=48, and its raster scroll boundary follows that row so the shoreline stays aligned.
Map rows and their raster offsets are committed together during VBlank; preparing
the next frame must not change the current screen's SCX, especially across 7→0 wraps.
Only shoreline cells use a separate palette with three states, each lasting 32
rules ticks: (1) the default colors; (2) dark sand becomes the existing light
foam color, and foam becomes deep water; (3) dark sand becomes deep water and
foam returns to its original light color. Repeat 1→2→3→1 without changing the
other two entries. Open beach water has a separate three-state palette cycle:
dark/mid/light/pale → dark/mid/mid/light → mid/dark/light/light, using the existing
game colors and the same 32-tick cadence. Only open-water cells use that palette;
inland sand, sky, HUD and sprites keep theirs. Palette writes happen during VBlank,
and the original surfing palette is restored before wave tiles become visible.
The shoreline's pattern data and geometry are not animated or rewritten for this effect.
Both wave lanes fade through their shared collision-height table. The ending
merges the rider onto calm water before switching back to the carrying pose.

`tools/test_gbc_progress.py` exercises idle beaches, shoreline entry, the hidden
2,000-point engine distance, gradual exit, carrying sprites, and stage reset.
`tools/test_gbc_frames.py` checks 3,000 gameplay frames for timing and sprite limits.
`tools/test_gbc_shore.py` checks actual displayed map/SCX alignment while walking
both directions across tile boundaries and when arriving at the destination beach.
No GBA changes or new palm-tree artwork are included in this GBC implementation.
