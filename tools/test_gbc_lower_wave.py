"""Compare the ROM's lower-lane physics and drawing through both 512px halves.

The old renderer wrapped at 512px although front_at repeats at 1024px. Verify
prepared line samples against the wave table actually used by rider physics,
including negative deltas and the shallow trough shape. Use normal joypad input
for upward transfers and measure presented frames with real DMA timing.
"""
from pyboy import PyBoy
from test_gbc_frames import ROM,code_symbol
from gbc_hardware import HardwareTiming
import re
from pathlib import Path

def main():
    p=PyBoy(str(ROM),window='null',sound_emulated=False);p.set_emulation_speed(0)
    timing=HardwareTiming(p);m=p.memory;s=timing.symbols;w=s['world']
    noi=(Path(__file__).resolve().parents[1]/'gbc/build/beaver.noi').read_text()
    def address(name):return int(re.search(r'DEF _'+name+r' 0x([0-9A-Fa-f]+)',noi)[1],16)&65535
    # Static sampler scratch shares render.o's DATA section with hud_icons.
    # Resolve local offsets from the assembler instead of exporting test symbols.
    symbols=(Path(__file__).resolve().parents[1]/'gbc/build/obj/src/render.sym').read_text()
    def offset(name):return int(re.search(r'\b_'+name+r'\s+([0-9A-Fa-f]+) G?R',symbols)[1],16)
    fy=address('hud_icons')+offset('fy_half')-offset('hud_icons');num=address('front_num')
    fixtures=[];prepared=None;shown=[];frame=0;checks=0;grounded=0;halves=set()
    def get(a,n=2):return sum(m[a+i]<<(i*8) for i in range(n))
    def signed(v):return v-65536 if v&32768 else v
    def put(a,value,n=2):
        for i in range(n):m[a+i]=(value>>(i*8))&255
    def lerp(heights,px):
        i=(px&511)>>3;f=px&7
        return heights[i]+(((heights[(i+1)&63]-heights[i])*f)>>3)
    def prepare(_):
        nonlocal prepared
        if fixtures:fixtures.pop()()
        prepared=(get(w+4,4),[signed(get(w+117+i*2)) for i in range(64)],m[num],
                  m[w+109],m[w+18],m[w+25],m[w+26],get(w+10),signed(get(w+12)))
    def present(_):
        nonlocal checks,grounded
        shown.append(frame)
        if prepared is None:return
        scroll,heights,n,phase,air,lane,target,x,y=prepared
        if phase!=1 or get(w+257,4)<300*64:return
        base=(scroll>>6)&1023;halves.add(base>>9)
        for k in range(11):
            delta=(lerp(heights,((base>>1)+128+k*12)&511)>>6)-112
            if delta>0:delta>>=2
            expected=116+min(10,(delta*n)>>4)
            assert m[fy+k]==expected,(base,k,m[fy+k],expected)
            checks+=1
        if not air and lane==target==1:
            delta=lerp(heights,(((scroll+x)>>7)+128)&511)-112*64
            if delta>0:delta>>=2
            expected=140*64+min(640,((delta>>2)*n)>>2)
            assert abs(y-expected)<=1,('lower physical surface mismatch',base,y,expected)
            grounded+=1
    p.hook_register(0,code_symbol('render_prepare'),prepare,None)
    p.hook_register(0,code_symbol('render_present'),present,None)
    def tick(n=1):
        nonlocal frame
        for _ in range(n):p.tick();frame+=1;timing.assert_safe()
    def tap(key):p.button_press(key);tick(4);p.button_release(key);tick(4)
    try:
        tick(240);tap('start');tick(30)
        for stage in (1,3,9):
            def setup():
                put(w+263,stage,1);put(w+254,min(stage-1,8),1)
                put(s['course_length'],(4000+(stage-1)*500)*64,4)
                put(w+257,1000*64,4);put(w+109,1,1);put(w+268,0)
                put(w+4,480*64,4);put(w+10,96*64);put(w+12,140*64)
                put(w+14,0);put(w+16,0);put(w+18,0,1);put(w+19,0,1)
                put(w+25,1,1);put(w+26,1,1);put(w+93,0,1)
                put(w+8,256);put(w+246,0,1);put(w+247,0,1)
                for i in range(81):m[s['race']+i]=0
                for i in range(64):put(w+117+i*2,112*64)
                put(num,12+min(stage-1,8)//2,1)
            fixtures.append(setup)
            while fixtures:tick()
            tick(30);shown.clear();before=get(s['frames_dropped'])
            # Grounded lower-lane pumping covers both halves and the wrap.
            p.button_press('right');tick(280);p.button_release('right')
            p.button_press('left');tick(120);p.button_release('left')
            p.screen.image.save(f'/private/tmp/beaver-lower-wave-stage-{stage}.png')
            fps=len(shown)*60/400
            gaps=[b-a for a,b in zip(shown,shown[1:])]
            assert fps>=20 and max(gaps)<=6,(stage,fps,max(gaps))
            p.button_press('a');p.button_press('up')
            for _ in range(240):
                tick()
                if m[w+18] and m[w+26]==0:break
            assert m[w+18] and m[w+26]==0,'A+Up did not start upward transfer'
            p.button_release('a');p.button_release('up')
            for _ in range(160):
                tick()
                if not m[w+18]:break
            assert not m[w+18] and m[w+25]==0,'upward transfer did not land on the top wave'
            assert get(s['frames_dropped'])==before
            print(f'Stage {stage}: lower wave drawn on physical surface through both period halves, {fps:.1f} FPS pumping, real A+Up lands on upper lane, safe hardware timing',flush=True)
        assert checks>3000 and grounded>100 and halves=={0,1},(checks,grounded,halves)
        print(f'Lower wave: {checks} exact drawing samples, {grounded} physical contacts, no 512px reset')
    finally:p.stop(save=False)
if __name__=='__main__':main()
