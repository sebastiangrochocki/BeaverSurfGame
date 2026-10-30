"""Real-DMA-budget regression: beach motion, shop, two riders and hard stages.

Run after make -C gbc. This deliberately catches the overrun that unmodified
PyBoy misses, and counts presented ROM frames rather than host emulator FPS.
"""
from pyboy import PyBoy
from test_gbc_frames import ROM, code_symbol, scanline_peak, oam
from gbc_hardware import HardwareTiming

def main():
    p=PyBoy(str(ROM),window='null',sound_emulated=False)
    p.set_emulation_speed(0)
    timing=HardwareTiming(p)
    m=p.memory;s=timing.symbols;w=s['world'];r=s['race']
    frame=0;shown=[]
    fixture=[]
    p.hook_register(0,code_symbol('render_present'),lambda _:shown.append(frame),None)
    def on_prepare(_):
        if fixture:fixture.pop()()
    p.hook_register(0,code_symbol('render_prepare'),on_prepare,None)
    def get(a,n=2):return sum(m[a+i]<<(8*i) for i in range(n))
    def put(a,v,n=2):
        for i in range(n):m[a+i]=(v>>(8*i))&255
    def tick(n):
        nonlocal frame
        for _ in range(n):
            p.tick();frame+=1;timing.assert_safe()
    def tap(key):p.button_press(key);tick(4);p.button_release(key);tick(4)
    def hold(key,n):p.button_press(key);tick(n);p.button_release(key)
    try:
        tick(240);tap('start');tick(120)
        assert m[w+109]==3
        shown.clear();tick(240)
        assert len(shown)>=235,('idle beach FPS',len(shown)/4)
        # Vertical pose DMA + shoreline palettes + a moving tiled camera.
        for _ in range(3):hold('down',32);hold('up',32)
        hold('right',45);hold('down',32);hold('up',32)
        # Put the walker at the door, then use ordinary inputs through the shop.
        put(w+10,96*64);put(w+12,108*64);put(w+268,0);tick(8)
        tap('a');assert m[w+109]==5
        hold('right',160);tap('a');tap('right');tap('a');tap('b')
        hold('left',320);assert m[w+109]==3
        for challenge,stage in enumerate((2,3,8,255)):
            # A fresh stage-2+ beach fixture; all race interactions are real input.
            # Wait for a completed simulation step before replacing rider data:
            # an emulator frame can stop halfway through the rider exchange.
            def setup():
                put(w+263,stage,1);put(w+254,min(stage-1,8),1)
                put(s['course_length'],(4000+(stage-1)*500)*64,4)
                put(w+101,3000,4);put(w+10,96*64);put(w+12,136*64)
                put(w+257,0,4);put(w+268,0);put(w+109,3,1)
                for byte in range(81):m[r+byte]=0
            fixture.append(setup)
            while fixture:tick(1)
            tick(12)
            tap('a');tap('left');tap('a');assert m[r+67]==2
            assert m[r+79]==min(80+challenge*10,100),'rival aim did not increase per accepted challenge'
            assert m[s['race_challenges']]==challenge+1
            for _ in range(600):
                tick(1)
                if m[r+67]==4:break
            assert m[r+67]==4
            shown.clear();dropped=get(s['frames_dropped']);clock=get(w,4)
            p.button_press('right')
            for i in range(1200):
                if i%90==0:p.button_press('a')
                if i%90==4:p.button_release('a')
                # Exercise both lanes and airborne flip requests.
                if i%240==40:p.button_press('down')
                if i%240==48:p.button_release('down')
                if i%240==160:p.button_press('up')
                if i%240==168:p.button_release('up')
                tick(1)
                assert not any(m[w+28+j] for j in range(4)),'race spawned a gull'
                assert scanline_peak(oam(m))[0]<=10,'sprite scanline overflow'
            for key in ('right','a','down','up'):p.button_release(key)
            assert m[r+67]==4
            assert get(s['frames_dropped'])==dropped,'simulation dropped ticks'
            assert 1196<=get(w,4)-clock<=1204,'physics clock slowed'
            fps=len(shown)/20
            gaps=[b-a for a,b in zip(shown,shown[1:])]
            assert fps>=20,(stage,fps)
            assert max(gaps)<=6,(stage,max(gaps))
            print(f'Hardware budget stage {stage}: {fps:.1f} presented FPS, longest gap {max(gaps)} VBlanks, 60 Hz rules, no gulls/dropped ticks/VRAM overruns',flush=True)
            if stage==2:
                for _ in range(1500):
                    tick(1)
                    if m[r+67]==5:break
                assert m[r+67]==5 and m[w+109]==4,'timed race did not settle at the destination beach'
                tap('a');tap('a');assert m[r+67]==6
        assert timing.transfers>1000 and timing.palette_writes>1000
        print(f'Beach 60 FPS; walking/shop/race: {timing.transfers} timed DMA uploads and {timing.palette_writes} safe raster palette writes')
    finally:p.stop(save=False)

if __name__=='__main__':main()
