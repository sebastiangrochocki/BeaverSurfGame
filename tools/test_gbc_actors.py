"""Decode ROM sprite bytes independently and check pose selection/overlap in OAM."""
from pathlib import Path
from PIL import Image
from pyboy import PyBoy
from test_gbc_frames import ROOT, ROM, MAP, symbol, code_symbol, scanline_peak, oam

ART = ROOT / "gbc/art"
COLORS = {(0, 0, 0): 1, (156, 90, 58): 2, (156, 0, 49): 3}


def source(name):
    im = Image.open(ART / name).convert("RGBA")
    return [0 if p[3] == 0 else COLORS[p[:3]] for p in im.getdata()]


def decode(mem, base, w, h):
    pixels = [0] * (w*h)
    tile = base
    for top in range(0, h, 16):
        for left in range(0, w, 8):
            for y in range(16):
                lo, hi = mem[0x8000 + tile*16 + y*2], mem[0x8001 + tile*16 + y*2]
                for x in range(8):
                    pixels[(top+y)*w+left+x] = ((lo >> (7-x)) & 1) | (((hi >> (7-x)) & 1) << 1)
            tile += 2
    return pixels


def main():
    pyboy = PyBoy(str(ROM), window="null", sound_emulated=False)
    pyboy.set_emulation_speed(0)
    mem = pyboy.memory
    world = symbol(MAP.read_text(), "world")
    last_input = symbol(MAP.read_text(), "last_input")
    direction = code_symbol("shop_direction")
    try:
        pyboy.tick(240)
        mem[0xFF4F] = 0
        original_surf_slots=bytes(mem[0x8880:0x8c80])
        for frame, name in ((0, "surf.png"), (1, "tug.png"), (2, "jump.png"), (5, "left.png"), (6, "right.png")):
            assert decode(mem, frame*16, 32, 32) == source(name), f"ROM pixels differ: {name}"
        # Runtime streams composites over the first deck slot. Decode the original
        # ROM source independently rather than mistaking that cache for source art.
        addr=code_symbol("board_tiles")
        offset=(addr>>16)*0x4000+(addr&0x3fff)
        rom=ROM.read_bytes()
        class BoardSource:
            def __getitem__(self, address): return rom[offset+address-0x8000]
        for frame, name in enumerate(("board.png", "board_up.png", "board_down.png")):
            assert decode(BoardSource(), frame*8, 32, 16) == source(name), f"ROM pixels differ: {name}"
        flip = source("jump.png")
        board = source("board.png")
        for i, value in enumerate(board):
            if not flip[512+i]: flip[512+i] = value
        quarter = [flip[x*32+31-y] for y in range(32) for x in range(32)]
        assert decode(mem, 48, 32, 32) == flip, "flip must use the jump with attached overlapping board"
        assert decode(mem, 64, 32, 32) == quarter, "quarter-turn pixel conversion differs"

        scenario = {}
        def word(offset, value):
            mem[world+offset] = value & 255
            mem[world+offset+1] = (value >> 8) & 255
        def prepare(_context):
            s = scenario
            mem[world+109] = s.get("phase", 1)
            word(10, 96*64); word(12, 112*64)
            word(14, s.get("vx", 0)); word(16, s.get("vy", 0))
            mem[world+18] = s.get("air", 0); mem[world+19] = s.get("flip", 0)
            mem[world+25] = mem[world+26] = 0
            mem[world+93] = mem[world+94] = mem[world+110] = mem[world+82] = 0
            for i in (28,29,30,31,60,61,62,75): mem[world+i] = 0
            for i in range(64): word(117+i*2, 112*64)
            for i in range(4): mem[world+257+i] = ((400*64) >> (i*8)) & 255
            mem[world+270] = 16
            mem[world+271] = s.get("left", 0)
            mem[world] = s.get("tick", 0)
            mem[direction] = s.get("direction", 0)
            mem[last_input] = s.get("input", 0)

        pyboy.hook_register(0, code_symbol("render_prepare"), prepare, None)
        cases = [
            ("surf", {}, 0, 112, 0),
            ("right", {"input": 2}, 96, 112, 0),
            ("left", {"input": 1}, 80, 112, 0),
            ("tug", {"input": 8}, 16, 112, 0),
            ("jump-up", {"air": 1, "vy": -256}, 32, 120, 0),
            ("jump-down", {"air": 1, "vy": 256}, 32, 128, 0),
            ("flip-0", {"air": 1, "flip": 26}, 48, None, 0),
            ("flip-90", {"air": 1, "flip": 19}, 64, None, 0),
            ("flip-180", {"air": 1, "flip": 13}, 48, None, 0x60),
            ("flip-270", {"air": 1, "flip": 6}, 64, None, 0x60),
            ("walk-0", {"phase": 3, "vx": 256, "tick": 0}, 136, None, 0),
            ("walk-1", {"phase": 3, "vx": 256, "tick": 8}, 152, None, 0),
            ("walk-left", {"phase": 3, "vx": -256, "tick": 8, "left": 1}, 152, None, 0x20),
            ("walk-up-0", {"phase": 3, "vy": -256, "direction": 2, "tick": 0}, 184, None, 0),
            ("walk-up-1", {"phase": 3, "vy": -256, "direction": 2, "tick": 8}, 184, None, 0),
            ("walk-up-idle", {"phase": 3, "direction": 2, "tick": 8}, 184, None, 0),
            ("walk-up-end", {"phase": 4, "vy": -256, "direction": 2, "tick": 8}, 184, None, 0),
            ("walk-down-0", {"phase": 3, "vy": 256, "direction": 1, "tick": 0}, 168, None, 0),
            ("walk-down-1", {"phase": 3, "vy": 256, "direction": 1, "tick": 8}, 168, None, 0),
            ("walk-down-idle", {"phase": 3, "direction": 1, "tick": 8}, 168, None, 0),
            ("walk-down-end", {"phase": 4, "vy": 256, "direction": 1, "tick": 8}, 168, None, 0),
            ("walk-up-return", {"phase": 3, "vy": -256, "direction": 2, "tick": 0}, 184, None, 0),
            ("surf-restored", {}, 0, 112, 0),
        ]
        for name, config, base, deck, mirror in cases:
            scenario = config
            pyboy.tick(24)
            entries = [tuple(mem[0xFE00+i*4+j] for j in range(4)) for i in range(40) if mem[0xFE00+i*4]]
            body = [s for s in entries if base <= s[2] < base+16]
            assert len(body) == (8 if deck is None else 4), f"{name}: wrong body object allocation: {body}"
            assert all(s[3] & 0x60 == mirror for s in body), f"{name}: wrong facing/rotation"
            boards = [s for s in entries if 112 <= s[2] < 136]
            if deck is None:
                assert not boards, f"{name}: board is already baked into this pose"
            else:
                assert sorted(s[2] for s in boards) == list(range(112,120,2)), f"{name}: incomplete composed lower row"
                frame=base//16
                raw=source("board_up.png" if deck==120 else "board_down.png" if deck==128 else "board.png")
                body_source=source({0:"surf.png",1:"tug.png",2:"jump.png",5:"left.png",6:"right.png"}[frame])[512:]
                assert decode(mem,112,32,16)==[a or b for a,b in zip(body_source,raw)], f"{name}: overlap pixels differ"
                assert min(s[0] for s in boards) - min(s[0] for s in body) == 16, f"{name}: deck must overlap by 16px"
                assert max(s[0] for s in boards) == 89, f"{name}: deck anchor moved away from waterline"
            assert scanline_peak(oam(mem))[0] <= 10, f"{name}: OAM scanline overflow"
            if name == "walk-0":
                assert decode(mem, 136, 32, 32) == source("walk_0.png")
                assert decode(mem, 152, 32, 32) == source("walk_1.png")
                walk_anchor = min(s[0] for s in body)
            if name.startswith("walk-up"):
                expected="walk_back_1.png" if name in ("walk-up-1","walk-up-end") else "walk_back.png"
                assert decode(mem,184,32,32)==source(expected), f"{name}: supplied up-walk pixels differ"
                assert min(s[0] for s in body)==walk_anchor, f"{name}: feet alignment changed"
            if name.startswith("walk-down"):
                expected="walk_front_1.png" if name in ("walk-down-1","walk-down-end") else "walk_front.png"
                assert decode(mem,168,32,32)==source(expected), f"{name}: supplied down-walk pixels differ"
                assert min(s[0] for s in body)==walk_anchor, f"{name}: feet alignment changed"
            if name=="surf-restored":
                assert bytes(mem[0x8880:0x8c80])==original_surf_slots, "walking leaked into surf actor/scenery slots"
            pyboy.screen.image.save(Path("/private/tmp") / f"beaver-new-{name}.png")
        print("Supplied sprite/board pixels, all poses and quarter-turns, walking, facing, 16px overlap and OAM limits pass")
    finally:
        pyboy.stop(save=False)


if __name__ == "__main__":
    main()
