# Surf polish and optional rival duels

GBC-only. The GBA game, editor workspace, shop ownership save (BVS1), best-score
save (BVR1), packed World offsets and 126-byte trace record are unchanged.
The old Rust gameplay traces describe different controls; `tools/check_gbc.sh`
now exercises the GBC mechanics directly without modifying shared fixtures.

## Controls and pickups

- A jumps; holding repeats grounded jumps. Ground taps buffer for six ticks.
- A pressed again in the air flips on either lane at any jump-bar position,
  including low/early jumps. An unfinished flip on landing causes a wipeout.
  The NPC alone uses a conservative landing forecast to choose its attempts.
- To climb: jump from the lower lane, then hold Up while rising, or use A+Up
  together. Holding Up before jumping works too. Up on the water does not jump.
  A directed climb adds a bounded upward impulse to clear the moving upper
  surface, then uses the usual gravity and landing rules.
  To drop: jump then press Down, or use A+Down together. Down/B
  alone holds the tuck on the current lane (or fast-falls in the air). Holding
  Down before pressing A also requests the lower lane on takeoff. Directional
  lane requests take priority over flips, so A+Down cannot accidentally flip.
- Rings are 16x16, worth 25, and collect through the body or board with a small
  margin. Every simultaneous collection counts. Three-ring trails alternate
  lanes; stage 3 introduces four-ring groups and bobbing groups, stage 6 increases
  their bob amplitude. Occasional higher groups reward airborne collection.
- Collection pops last 12 simulation ticks at the ring's world position. Healing
  keeps its old time cadence rather than increasing with the denser ring trails.
- Surf speed, rightward rider momentum and rising ramp slope all add takeoff
  lift. Early jumps use a stronger base impulse and retain earned speed; the
  top fifth of the ramp still rewards better timing with a higher jump.
- Gulls cost 50 points, floor zero, and break the combo. Hazard collision remains
  based on visible body pixels: no board hits or hits through the tuck's blank
  top eight rows. Damage flashes keep the character visible.

## Rival races

The teal-board rival waits on each finish beach and on start beaches from stage 2. Approach
and press A. Left selects Yes, Right selects No, A confirms, B backs out; No is
the default. A 1000-point entry is charged once. Countdown and run-in lock input.
The duel replaces the current crossing and scrolls at two course units per tick.
Both riders share waves, clock, pause and finish; score, not arrival, decides it.
A paid challenge on a finish beach starts the upcoming stage's crossing.

Contest ledgers start at zero and count rings, flips, airtime and clean landings.
Races have no gulls or gull penalties. No passive income or existing wealth counts. The first rider touching
a ring gets it; simultaneous contacts use nearest centre, with exact ties going
to the player. Riders do not block each other while surfing. The waiting beach
rival is solid, using stable 32x24 visible-art bounds, axis-separated sliding,
and talk reach outside those bounds. The shop door keeps interaction priority.
Damage never knocks either rider
out; entry health is restored afterward and no race hearts spawn.

After entry, the wallet is held separately. Settlement adds the player's earned
contest points plus 2000 for a win, 1000 for a draw, or zero for a loss. Results
lock movement and settle once. A shows the statistics (player then NPC order),
and a second fresh A resumes beach movement. One accepted duel per beach visit.
Race state is temporary and does not change persistent save formats.

The NPC emits ordinary inputs into the same rider physics. It reacts after at
least 12 ticks, plans every eight ticks, intercepts visible targets, changes
lanes and attempts at most one flip per jump.
Rival aim starts at 80% for the first accepted challenge in a run, increases
to 90% for the second, then caps at 100%. It measures precise planning turns:
a missed eight-tick turn coasts instead of correcting toward a pickup or starting
a jump/flip. Offers, cancellations and insufficient funds do not advance it.
The count survives beach changes and resets with a new run; it is not saved.
No hidden-point bonuses, teleports or catch-up multipliers are used.

## Wave progression and finish beaches

Wave difficulty is min(stage - 1, 8), with no elapsed-time ramp that could make
stage 1 harder than stage 2. The original top wave grows at each of the first
nine stages. Lower crests use 12/16 of its height initially, increasing to 16/16;
troughs use a quarter swing and cap at y150 to keep the rider above the footer.
Its wavelength remains twice the upper lane's. Rendering preserves the full
1024px lower period, fixing the old visible/physical mismatch at 512px.

Walk past the house and palm across 64 additional native pixels of sand (eight
tile columns), then enter the far waves to start the next stage directly.
The pole flag shows the upcoming stage: 2 after the first crossing, then 3,
and so on, capped at 255. The initial start beach has no flag. The shop and
rival remain available before continuing; the water exit preserves your lane.

## Hardware boundaries

Normal play uses a 12-slot ring pool. Races deliberately admit one ring at a time:
two full-size riders use four objects per row each, and
the target uses two, reaching the hardware's ten-object scanline limit. Cosmetic
effects are lowest priority. Unrendered pickups cannot collect; an unrendered
gull cannot damage. The solo HUD pointer stays within the top 16 pixels.

The small ring occupies tiles 160-163. Tiles 164-171 hold an independent
rival lower-body/deck composite, staged in D880-D8FF; the player's D800-D87F
cache is unchanged.
Sprite drawing lives in bank 6; race/pickup logic lives in bank 7. Ordinary WRAM
must end below C500. Both caches and tilemap/OAM updates commit during VBlank;
bulk race/scene pattern changes blank the LCD safely. The footer palette fix is
retained, with no writes during pixel fetch.
The bounded simulation queue retains four owed ticks. Race drawing gets a turn
after two ticks once the remaining debt is down to one; ticks also run while
the prepared frame waits for VBlank. Solo retains full catch-up. Physics still
advances at 60 Hz.
The exact periodic wave refreshes are baked into ROM banks 8-25; the MBC5 ROM
is now 512 KB. See [hardware performance notes](HARDWARE_PERFORMANCE.md).

## Verification

```
make -j4 -C gbc GBDK=/private/tmp/gbdk
sh tools/check_gbc.sh
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_frames.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_race.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_actors.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_progress.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_hardware.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_flips.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_finish_beach.py
/private/tmp/beaver-wave-venv/bin/python tools/test_gbc_lower_wave.py
python3 tools/test_gbc_wave_runtime.py
```

The race ROM test skips only to an affordable stage-2 beach fixture, then uses
real joypad input for the offer, countdown, run-in, shared pause and results.
Scripted AI comparisons are reproducible diagnostics, not human playtest ratings.
