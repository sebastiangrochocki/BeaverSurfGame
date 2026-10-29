"""Check bottom HUD start/middle/finish positions in the linked GBC ROM."""

from pathlib import Path
from PIL import Image
from pyboy import PyBoy
from test_gbc_frames import MAP, ROM, ROOT, code_symbol, symbol


def head_position(mem):
    tiles=list(mem[0x9A02:0x9A12])
    assert tiles.count(8)==1,f'missing or duplicate beaver head: {tiles}'
    assert all(t in (8,253) for t in tiles),f'old filling bar or arrow remains: {tiles}'
    return tiles.index(8)


def assert_landmarks(pyboy):
    mem=pyboy.memory
    for row in range(2):
        assert list(mem[0x9A00+row*32:0x9A02+row*32])==[row*2,row*2+1]
        assert list(mem[0x9A12+row*32:0x9A14+row*32])==[4+row*2,5+row*2]
    image=pyboy.screen.image
    for name,origin in [('progress_start',0),('progress_finish',144)]:
        art=Image.open(ROOT/'gbc/art'/f'{name}.png').convert('RGB')
        mapping={}
        for y in range(16):
            for x in range(16):
                source=art.getpixel((x,y));actual=image.getpixel((origin+x,128+y))
                if source in mapping:assert actual==mapping[source],f'{name}: corrupted source pixels at {x},{y}'
                else:mapping[source]=actual
        assert len(set(mapping.values()))==len(mapping),f'{name}: colors collapsed'
        assert mapping[(173,214,239)]==image.getpixel((24,136)),f'{name}: backdrop does not blend into strip'


def main():
    world = symbol(MAP.read_text(), "world")
    pyboy = PyBoy(str(ROM), window="null", sound_emulated=False)
    pyboy.set_emulation_speed(0)
    mem = pyboy.memory
    dropped=symbol(MAP.read_text(),'frames_dropped')
    def drop_count():return mem[dropped]|mem[dropped+1]<<8
    raster={'errors':[],'top':0,'bottom':0}
    def rgb(r,g,b):return (r|(g<<5)|(b<<10)).to_bytes(2,'little')
    pale=rgb(22,27,30)
    expected_bottom=(rgb(19,11,7)+rgb(0,0,0)+rgb(31,31,31)+pale,
                     rgb(26,20,13)+rgb(28,24,17)+rgb(2,6,16)+pale)
    expected_top=(rgb(1,4,9)+rgb(31,31,31)+rgb(27,6,7)+rgb(8,24,11),
                  rgb(12,22,28)+rgb(31,23,3)+rgb(31,28,16)+rgb(31,31,31))
    def checked_raster(_context):
        try:
            boundary=mem[0xFF45]
            if boundary not in (16,127):return
            old=mem[0xFF68]
            palettes=[]
            for p in (0,5):
                data=[]
                for i in range(8):mem[0xFF68]=p*8+i;data.append(mem[0xFF69])
                palettes.append(bytes(data))
            mem[0xFF68]=old
            assert tuple(palettes)==(expected_top if boundary==16 else expected_bottom),f'palette leak at raster {boundary}'
            if boundary==16:
                assert mem[0xFF40]&0x12==0x12,'signed footer mode or hidden actors leaked into next frame'
                raster['top']+=1
            else:raster['bottom']+=1
        except Exception as e:raster['errors'].append(str(e))
    pyboy.hook_register(0,code_symbol('lcd_isr'),checked_raster,None)
    # Inspect every palette *write*, not merely its final RAM contents. PyBoy
    # accepts some Mode-3 writes that real hardware/browser cores reject.
    code=ROM.read_bytes();pc=code_symbol('render_hud_palettes')
    writes=[]
    lengths={0x47:1,0xb7:1,0x21:3,0x28:2,0x3e:2,0xea:3,0x0e:2,
             0x78:1,0xfa:3,0xe6:2,0x20:2,0x2a:1,0xe2:1}
    while code[pc]!=0xc9:
        opcode=code[pc]
        assert opcode in lengths,f'unknown palette-copy opcode {opcode:02x}'
        if opcode==0xe2:writes.append(pc)
        pc+=lengths[opcode]
    assert len(writes)==16
    write_checks={'count':0,'blocked':[]}
    def check_write(_context):
        mode=mem[0xff41]&3
        write_checks['count']+=1
        if mode not in (0,1):write_checks['blocked'].append((mem[0xff44],mode))
    for address in writes:pyboy.hook_register(0,address,check_write,None)
    def assert_single_horizon():
        assert list(mem[0x98A0:0x98B4]) == [43]*20, "expected one horizon row at y=40"
        assert 43 not in list(mem[0x98C0:0x98D5]), "duplicate horizon at y=48"
    try:
        pyboy.tick(240)
        pyboy.button_press("start")
        pyboy.tick(20)
        pyboy.button_release("start")
        pyboy.tick(20)
        assert mem[world + 109] == 3, "expected player waiting on start beach"
        initial_x=list(mem[world+10:world+12])
        pyboy.tick(600)
        assert list(mem[world+10:world+12])==initial_x, "idle beach must not walk automatically"
        assert mem[world+109]==3
        assert drop_count()==0,'idle start beach dropped ticks'
        assert list(mem[world+257:world+261])==[0]*4
        assert_single_horizon()
        assert mem[0x98C0]==50, "expected supplied sand at native y=48"
        assert mem[0x98CE]==55, "expected supplied shoreline top at native x=112, y=48"
        assert mem[0x98EE]==57, "expected supplied shoreline bottom at native y=56"
        assert any(136<=mem[0xFE02+i*4]<168 for i in range(40)), "missing supplied carrying pose"
        assert not any(168<=mem[0xFE02+i*4]<176 and mem[0xFE00+i*4] for i in range(40)), "carried board must not be drawn twice"
        pyboy.screen.image.save('/private/tmp/beaver-beach-start.png')
        for distance, marker in ((0,0),(266,0),(267,1),(2000,7),(3999,14),(4000,15)):
            # Packed World layout; render.c asserts heights at byte 117. The course
            # fields follow the 128-byte height table and existing control fields.
            value = distance * 64
            for byte in range(4):
                mem[world + 257 + byte] = (value >> (byte * 8)) & 255
            pyboy.tick(20)
            assert head_position(mem)==marker,f'{distance}: wrong head position'
            assert_landmarks(pyboy)
            assert 255 not in mem[0x9A00:0x9A14],'old progress arrow remains'
            assert list(mem[0x9A25:0x9A2E])==[245,246,227,233,231,213,217,217,218],'stage label must read STAGE 001'
            mem[0xFF4F] = 1
            attrs = list(mem[0x9A00:0x9A00 + 20])
            mem[0xFF4F] = 0
            expected_attrs=[0x85]*2+[0x81]*16+[0x80]*2
            expected_attrs[marker+2]=0x80
            assert attrs == expected_attrs, "wrong landmark/head/strip attributes"
            pyboy.screen.image.save(Path("/private/tmp") / f"beaver-progress-{distance}.png")
        assert drop_count()==0,'progress redraws dropped ticks'
        for byte in range(4):
            mem[world + 257 + byte] = 0
        pyboy.button_press("right")
        for _ in range(240):
            pyboy.tick()
            if mem[world+109]==1: break
        pyboy.button_release("right")
        assert mem[world+109]==1, "walking did not enter the visible shoreline"
        pyboy.tick(8)
        pyboy.screen.image.save('/private/tmp/beaver-beach-entry.png')
        pyboy.tick(100)
        assert mem[world + 109] == 1, "expected surfing after player enters water"
        pyboy.screen.image.save("/private/tmp/beaver-progress-surfing.png")
        pyboy.tick(240)
        assert mem[world + 109] == 1, "a stage must not finish after a few seconds"
        assert_single_horizon()
        pyboy.screen.image.save('/private/tmp/beaver-horizon-surf.png')
        assert drop_count()==0,'walking/surfing dropped ticks'
        # Approach from the lower lane: the feet must merge gradually rather
        # than teleporting upward when the wave lanes flatten.
        # Sample at rules boundaries: a displayed frame can drain multiple
        # queued ticks, so per-display-frame deltas aren't per-tick movement.
        approach = {"steps": 0, "previous_y": 140 * 64, "deltas": []}
        def approach_step(_context):
            if approach["steps"] == 0:
                value = 3880 * 64
                for byte in range(4):
                    mem[world + 257 + byte] = (value >> (byte * 8)) & 255
                mem[world + 12] = (140 * 64) & 255
                mem[world + 13] = (140 * 64) >> 8
                mem[world + 25] = mem[world + 26] = 1
            else:
                y = mem[world + 12] | mem[world + 13] << 8
                approach["deltas"].append(abs(y - approach["previous_y"]))
                approach["previous_y"] = y
                approach["heights"] = [mem[world + 117 + i*2] | mem[world + 118 + i*2] << 8
                                       for i in range(64)]
            approach["steps"] += 1
        step = code_symbol("world_step")
        pyboy.hook_register(step >> 16, step & 0xffff, approach_step, None)
        pyboy.tick(75)
        pyboy.hook_deregister(step >> 16, step & 0xffff)
        assert approach["steps"] >= 60, "not enough approach ticks sampled"
        assert max(approach["deltas"]) <= 32, "exit lane snapped instead of merging"
        assert approach["previous_y"] == 112 * 64, "exit did not settle at beach height"
        assert mem[world + 270] == 0, "exit waves should be flat"
        assert approach["heights"] == [112*64]*64, f"wave collision heights did not flatten: {approach['heights']}"
        value = 3999 * 64
        for byte in range(4):
            mem[world + 257 + byte] = (value >> (byte * 8)) & 255
        pyboy.tick(30)
        assert mem[world + 109] == 4, "course did not reach end beach"
        assert_single_horizon()
        pyboy.screen.image.save('/private/tmp/beaver-beach-end.png')
        assert drop_count()==0,'finish approach dropped ticks'
        assert head_position(mem)==15, "finish head must stay beside the flag"
        assert_landmarks(pyboy)
        initial_x = list(mem[world+10:world+12])
        pyboy.tick(300)
        assert mem[world+109] == 4 and list(mem[world+10:world+12]) == initial_x, "end beach must wait for input"
        pyboy.button_press("right")
        pyboy.tick(130)
        pyboy.button_release("right")
        pyboy.tick(20)
        assert mem[world + 109] == 3, "expected waiting on next beach"
        assert mem[world + 263] == 2, "stage did not increment"
        assert_single_horizon()
        assert head_position(mem)==0, "next-stage head did not reset"
        assert_landmarks(pyboy)
        assert not raster['errors'],raster['errors'][:5]
        assert write_checks['count']>10000 and not write_checks['blocked'],write_checks
        assert raster['top']>500 and raster['bottom']>500,raster
        assert drop_count()==0,f'new HUD dropped {drop_count()} simulation ticks'
        length=symbol(MAP.read_text(),'course_length')
        assert sum(mem[length+i]<<(i*8) for i in range(4))==4500*64,'stage 2 length is not 4500'
        # Complete stage 2 at its own target, not stage 1's old distance, and
        # verify the top label uses three digits rather than the score format.
        mem[world+109]=1
        for i in range(4):mem[world+257+i]=((4499*64)>>(i*8))&255
        pyboy.tick(30)
        assert mem[world+109]==4
        assert list(mem[0x9825:0x982e])==[32,33,14,20,18,0,4,4,6],'top label must read STAGE 002'
        pyboy.screen.image.save('/private/tmp/beaver-stage-002.png')
        print("Supplied landmarks, 8x8 head, stage label/reset, 4000/4500 lengths, palette isolation, beach flow and zero dropped ticks pass")
    finally:
        pyboy.stop(save=False)


if __name__ == "__main__":
    main()
