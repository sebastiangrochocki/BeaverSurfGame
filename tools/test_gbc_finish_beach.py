"""Extended island, palm/numbered flag and automatic water exit on the real ROM."""
from pyboy import PyBoy
from test_gbc_frames import ROM,code_symbol,scanline_peak,oam
from gbc_hardware import HardwareTiming

EXIT_PAN=264*64

def main():
    p=PyBoy(str(ROM),window='null',sound_emulated=False);p.set_emulation_speed(0)
    timing=HardwareTiming(p);m=p.memory;s=timing.symbols;w=s['world'];r=s['race']
    fixture=[];shown=[];frame=0
    def get(a,n=2):return sum(m[a+i]<<(i*8) for i in range(n))
    def put(a,value,n=2):
        for i in range(n):m[a+i]=(value>>(i*8))&255
    def prepare(_):
        if fixture:fixture.pop()()
    p.hook_register(0,code_symbol('render_prepare'),prepare,None)
    p.hook_register(0,code_symbol('render_present'),lambda _:shown.append(frame),None)
    def tick(n=1):
        nonlocal frame
        for _ in range(n):
            p.tick();frame+=1;timing.assert_safe()
            assert scanline_peak(oam(m))[0]<=10
    def tap(key):p.button_press(key);tick(4);p.button_release(key);tick(4)
    def bank(bank,base,n):
        old=m[0xff4f];m[0xff4f]=bank;data=list(m[base:base+n]);m[0xff4f]=old;return data
    def map_cells():
        return [(m[0x9800+y*32+x],bank(1,0x9800+y*32+x,1)[0])
                for y in range(6,16) for x in range(20)]
    try:
        tick(240);tap('start');tick(30)
        assert m[w+109]==3
        initial=map_cells()
        assert any(108<=tile<126 and attr==15 for tile,attr in initial),'missing green palm beside first house'
        assert not any(tile in (126,127) and attr&8 for tile,attr in initial),'first beach has a flag'
        p.screen.image.save('/private/tmp/beaver-palm-start.png')
        for stage in (1,9,99,255):
            length=(4000+(stage-1)*500)*64
            def finish():
                put(w+263,stage,1);put(w+254,min(stage-1,8),1);put(s['course_length'],length,4)
                put(w+257,length-1,4);put(w+268,0);put(w+109,1,1)
                put(w+10,96*64);put(w+12,112*64);put(w+18,0,1);put(w+25,0,1);put(w+26,0,1)
                put(w+101,3000,4)
                for i in range(81):m[r+i]=0
            fixture.append(finish)
            while fixture:tick()
            tick(30);assert m[w+109]==4
            p.button_press('right');tick(120);p.button_release('right');tick(20)
            assert m[w+109]==4 and m[w+263]==stage,'old short beach exit still active'
            assert get(w+268)==120*64,'beach camera did not follow walking'
            # House has scrolled to x0: palm x88, flagpole x120, numbers x128.
            assert m[0x9800+6*32+11]==108 and bank(1,0x9800+6*32+11,1)[0]==15
            number=min(stage+1,255)
            digits=[217+int(c) for c in str(number)]
            assert list(m[0x9800+9*32+16:0x9800+9*32+16+len(digits)])==digits,(stage,digits)
            assert all(a==3 for a in bank(1,0x9800+9*32+16,len(digits)))
            assert m[0x9800+9*32+15]==126 and m[0x9800+9*32+16+len(digits)]==127
            p.screen.image.save(f'/private/tmp/beaver-finish-flag-{number}.png')
            before=get(s['frames_dropped']);shown.clear()
            # The outgoing sea is visible alongside the flag, not 512px away.
            p.button_press('right');tick(24)
            assert any(t in (51,52,53,54) and a&7==4 for t,a in map_cells()),'sea missing beside numbered flag'
            tick(84);p.button_release('right');tick(20)
            assert m[w+109]==4 and get(w+268)==228*64
            cells=map_cells()
            assert any(t in (55,56,57,62) and a&7==6 and not a&32 for t,a in cells),'missing outgoing unmirrored shoreline'
            assert any(t in (51,52,53,54) and a&7==4 for t,a in cells),'missing water beyond sand'
            p.screen.image.save(f'/private/tmp/beaver-finish-outgoing-{number}.png')
            fps=len(shown)*60/128
            p.button_press('right')
            for _ in range(60):
                tick()
                if m[w+109]==1:break
            p.button_release('right');tick(20)
            assert m[w+109]==1 and m[w+263]==number,'water exit did not directly start upcoming stage'
            assert get(w+268)==0 and get(w+257,4)<128*64,'next stage started at wrong camera/distance'
            assert get(s['course_length'],4)==(4000+(number-1)*500)*64
            assert get(s['frames_dropped'])==before,'extended beach dropped simulation ticks'
            assert fps>=45,('slow beach scrolling',fps)
            print(f'Finish {stage} -> flag {number}: 64px extra sand, sea visible beside flag, green palm, direct stage transition, {fps:.1f} walking FPS, no hardware overruns',flush=True)
        print(f'Extended beaches: {timing.transfers} timed DMA uploads, fixed tilemap/VRAM allocation and <=10 objects per scanline')
    finally:p.stop(save=False)

if __name__=='__main__':main()
