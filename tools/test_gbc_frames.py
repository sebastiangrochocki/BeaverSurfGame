#!/usr/bin/env python3
"""Headless frame check for the Game Boy Color ROM.

Runs web/beaver.gbc for a few thousand frames of lane hops, jumps and flips.
Every frame it checks that queued actors are whole sprites, that the beaver,
gulls, rings and hearts never put more than 10 sprites on one scanline, that
the game did not drop a tick, and that a grounded beaver's feet sit on the
lane the rules are simulating.

The rules run at 60 Hz while the display takes whatever time is left, so the
sprites on screen belong to a slightly older world state than the one in WRAM.
Hooks on render_prepare and render_present keep a copy of the state each frame
was drawn from, and the sprite checks read that copy.

Needs PyBoy (`python3 -m pip install pyboy`) and a freshly linked ROM so
build/beaver.map and build/beaver.noi match web/beaver.gbc.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "web" / "beaver.gbc"
MAP = ROOT / "gbc" / "build" / "beaver.map"
NOI = ROOT / "gbc" / "build" / "beaver.noi"
# Enough of the World struct for every offset below.
WORLD_BYTES = 128

# Sprite tile ranges from render.c, after the generated sheets.
TILE_BEAVER = range(0, 112)
TILE_GULL = range(136, 160)
TILE_RING = range(160, 172)
TILE_HEART = range(172, 176)

# Packed World layout. heights_at_117 in render.c fails the ROM build if this moves.
BEAVER_Y = 12
BEAVER_AIR = 18
BEAVER_LANE = 25
GULL_PRESENT = 28
GULLS = 32
RING_PRESENT = 60
RINGS = 63
HEART_PRESENT = 75
HEART = 76
STUN = 93
PHASE = 109

PHASE_PLAY = 1
LANE_BOTTOM = 1
VIEW_LIFT = 24
# Both waves are painted into the background map; the window stays off (LCDC bit 5).
LCDC_WINON = 0x20

TWO_THIRDS = [
    0, 0, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 8, 8, 9, 10,
    10, 11, 12, 12, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 20,
    21, 22, 22, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31,
]

FRAMES = 3000


def symbol(text, name):
    """Address of a linker symbol. The map truncates names, so match the prefix."""
    short = name[:8]
    match = re.search(r"([0-9A-Fa-f]{8})\s+_" + re.escape(short) + r"\b", text)
    if not match:
        raise SystemExit(f"{name} is not in {MAP}")
    return int(match.group(1), 16)


def code_symbol(name):
    """Address of a function from the NoICE symbol file, which keeps full names."""
    match = re.search(r"DEF _" + re.escape(name) + r" 0x([0-9A-Fa-f]+)", NOI.read_text(errors="replace"))
    if not match:
        raise SystemExit(f"{name} is not in {NOI}")
    return int(match.group(1), 16)


class Snapshot:
    """A copy of the World bytes, indexed by the same absolute addresses as memory."""

    def __init__(self, mem, base):
        self.base = base
        self.data = bytes(mem[base + i] for i in range(WORLD_BYTES))

    def __getitem__(self, addr):
        return self.data[addr - self.base]


def view_x(px):
    if px < -48 or px > 400:
        return -64
    v = px + 48
    groups, v = divmod(v, 48)
    return groups * 32 + TWO_THIRDS[v] - 32


def on_screen(px):
    # Interior only: a sprite clipped by the screen edge is shorter than a whole actor.
    x = view_x(px)
    return 16 < x < 144


def i16(mem, addr):
    value = mem[addr] | (mem[addr + 1] << 8)
    return value - 0x10000 if value >= 0x8000 else value


def u16(mem, addr):
    return mem[addr] | (mem[addr + 1] << 8)


def oam(mem):
    sprites = []
    for index in range(40):
        addr = 0xFE00 + index * 4
        y, x, tile = mem[addr], mem[addr + 1], mem[addr + 2]
        if y == 0 or y >= 160:
            continue
        sprites.append((y, x, tile))
    return sprites


def kind(tile):
    if tile in TILE_BEAVER:
        return "beaver"
    if tile in TILE_GULL:
        return "gull"
    if tile in TILE_RING:
        return "ring"
    if tile in TILE_HEART:
        return "heart"
    return None


def counted(sprites):
    return [sprite for sprite in sprites if kind(sprite[2])]


def scanline_peak(sprites):
    peak = 0
    worst = 0
    for line in range(144):
        n = 0
        for y, _x, _tile in sprites:
            top = y - 16
            if top <= line < top + 16:
                n += 1
        if n > peak:
            peak = n
            worst = line
    return peak, worst


def buttons(frame):
    """Right most of the time, with lane hops, jumps and a second press in the air."""
    held = {"right"}
    cycle = frame % 150
    # A few frames apart, so a loop that samples twice and then waits still sees the release.
    if cycle in (30, 36, 90, 96):
        held.add("b")
    if cycle in (50, 58, 110, 120):
        held.add("a")
    if cycle == 70:
        held.add("left")
        held.discard("right")
    return held


def press(pyboy, held, previous):
    for name in held - previous:
        pyboy.button_press(name)
    for name in previous - held:
        pyboy.button_release(name)


def main():
    try:
        from pyboy import PyBoy
    except ImportError:
        raise SystemExit("PyBoy is not installed. python3 -m pip install pyboy")

    text = MAP.read_text(errors="replace")
    world = symbol(text, "world")
    dropped_addr = symbol(text, "frames_dropped")

    pyboy = PyBoy(str(ROM), window="null", sound_emulated=False)
    pyboy.set_emulation_speed(0)
    mem = pyboy.memory

    # The state each frame is drawn from: taken as render_prepare starts, current once
    # render_present has copied that frame to VRAM and OAM.
    drawn = {"preparing": None, "shown": None}

    def on_prepare(_ctx):
        drawn["preparing"] = Snapshot(mem, world)

    def on_present(_ctx):
        drawn["shown"] = drawn["preparing"]

    pyboy.hook_register(0, code_symbol("render_prepare"), on_prepare, None)
    pyboy.hook_register(0, code_symbol("render_present"), on_present, None)

    previous = set()
    saw_bottom = False
    saw_top_air = False
    play_drops = None
    problems = []

    for _ in range(240):
        pyboy.tick()
    pyboy.button_press("start")
    for _ in range(30):
        pyboy.tick()
    pyboy.button_release("start")
    pyboy.tick()

    for frame in range(FRAMES):
        held = buttons(frame)
        press(pyboy, held, previous)
        previous = held
        pyboy.tick()

        if mem[world + PHASE] != PHASE_PLAY:
            continue
        if play_drops is None:
            play_drops = u16(mem, dropped_addr)
        if mem[0xFF40] & LCDC_WINON:
            problems.append(f"frame {frame}: the window is on (LCDC {mem[0xFF40]:#x})")
            break

        sprites = counted(oam(mem))
        peak, line = scanline_peak(sprites)
        if peak > 10:
            problems.append(f"frame {frame}: scanline {line} has {peak} beaver/gull/ring/heart sprites")
            break

        if mem[world + BEAVER_LANE] == LANE_BOTTOM:
            saw_bottom = True
        if mem[world + BEAVER_AIR] and mem[world + BEAVER_LANE] == 0:
            saw_top_air = True

        # Sprites on screen come from the state the frame was prepared from.
        seen = drawn["shown"]
        if seen is None or seen[world + PHASE] != PHASE_PLAY:
            continue
        lane = seen[world + BEAVER_LANE]

        def actors(present_at, data_at, slots, stride):
            found = []
            for slot in range(slots):
                if not seen[world + present_at + slot]:
                    continue
                x = i16(seen, world + data_at + slot * stride)
                y = i16(seen, world + data_at + slot * stride + 2)
                if on_screen(x >> 6):
                    found.append((x, y))
            return found

        def boxed(name, left, width):
            return sum(
                1
                for _sy, sx, tile in sprites
                if kind(tile) == name and left <= (sx - 8) < left + width
            )

        # Actors fully inside the screen. A ring or heart that does not fit is omitted
        # whole; fewer sprites than a full actor is a slice. A neighbour can add extras.
        for gx, _gy in actors(GULL_PRESENT, GULLS, 4, 4)[:3]:
            left = view_x(gx >> 6) - 15
            if 0 <= left and left + 32 <= 168 and boxed("gull", left, 32) < 4:
                problems.append(f"frame {frame}: a gull is missing sprites")
                break
        else:
            for rx, _ry in actors(RING_PRESENT, RINGS, 3, 4)[:2]:
                left = view_x(rx >> 6) - 12
                if 0 <= left and left + 24 <= 160 and 0 < boxed("ring", left, 24) < 6:
                    problems.append(f"frame {frame}: a ring is sliced")
                    break
            else:
                if seen[world + HEART_PRESENT]:
                    hx = i16(seen, world + HEART)
                    left = view_x(hx >> 6) - 8
                    if 0 <= left and left + 16 <= 160 and boxed("heart", left, 16) == 1:
                        problems.append(f"frame {frame}: a heart is sliced")
        if problems:
            break

        stun = seen[world + STUN]
        airborne = seen[world + BEAVER_AIR]
        beaver_sprites = [sprite for sprite in sprites if kind(sprite[2]) == "beaver"]
        if stun == 0 and not airborne and beaver_sprites:
            # beaver.y is the lane surface while grounded. The lower 8x16 row is queued
            # at feet - 15, and OAM stores that plus 16.
            feet_oam = max(sprite[0] for sprite in beaver_sprites)
            surface = i16(seen, world + BEAVER_Y)
            want = ((surface >> 6) - VIEW_LIFT) + 1
            if abs(feet_oam - want) > 2:
                problems.append(
                    f"frame {frame}: sprite feet OAM {feet_oam}, lane surface {want} (lane {lane})"
                )
                break

    dropped = u16(mem, dropped_addr) - (play_drops or 0)
    pyboy.stop()

    if dropped != 0:
        problems.append(f"frames_dropped is {dropped}")
    if not saw_bottom:
        problems.append("the beaver never reached the bottom lane")
    if not saw_top_air:
        problems.append("the beaver never jumped on the top lane")
    if problems:
        print("\n".join(problems))
        return 1
    print(f"{FRAMES} frames, both lanes, no dropped ticks, sprites whole")
    return 0


if __name__ == "__main__":
    sys.exit(main())
