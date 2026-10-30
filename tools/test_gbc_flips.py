"""Real joypad A must start low-hop flips at every bar position, solo and race."""
from pyboy import PyBoy
from test_gbc_frames import ROM, code_symbol
from gbc_hardware import HardwareTiming

def main():
    p=PyBoy(str(ROM),window='null',sound_emulated=False);p.set_emulation_speed(0)
    timing=HardwareTiming(p);m=p.memory;s=timing.symbols
    w=s['world'];r=s['race'];request=[];result=[];waiting=False
    def put(address,value,size=2):
        for i in range(size):m[address+i]=(value>>(8*i))&255
    def before_step(_):
        nonlocal waiting
        if request and m[s['last_input']]&4:
            lane,racing,meter=request.pop()
            put(w,1,4);put(w+4,0,4);put(w+8,256)
            put(w+10,96*64);put(w+12,(140 if lane else 112)*64-8*64)
            put(w+14,0);put(w+16,0);m[w+18]=1
            for offset in (19,20,21,22,23,24):m[w+offset]=0
            m[w+25]=m[w+26]=lane
            for offset in (93,94,110,246,247,248,249):m[w+offset]=0
            m[w+92]=meter;m[w+100]=6;m[w+109]=1
            # The calm run-in keeps the surface fixed while testing a low hop;
            # a newly refreshed moving wave could otherwise land this fixture.
            put(w+257,100*64,4);put(s['course_length'],4500*64,4)
            for i in range(64):put(w+117+i*2,112*64)
            for i in range(81):m[r+i]=0
            m[r+67]=4 if racing else 0;m[r+79]=80
            put(r,96*64);put(r+2,140*64);m[r+15]=m[r+16]=1;put(r+22,256)
            m[s['surf_up_held']]=m[s['surf_down_held']]=0
            waiting=True
        elif waiting:
            result.append((m[w+19],m[w+94]));waiting=False
    bank,pc=divmod(code_symbol('world_step'),65536)
    p.hook_register(bank,pc,before_step,None)
    try:
        p.tick(240)
        for racing in (0,1):
            for lane in (0,1):
                for meter in (0,19,95):
                    result.clear();request.append((lane,racing,meter));p.button_press('a')
                    for _ in range(30):
                        p.tick();timing.assert_safe()
                        if result:break
                    assert result and result[0][0]==25,(lane,racing,meter,result)
                    assert result[0][1]!=4,'low flip was refused'
                    p.button_release('a');p.tick(4);timing.assert_safe()
        print('ROM joypad: low-hop flips start at 0/20/100% on both lanes in solo/race; hardware upload timing remains safe')
    finally:p.stop(save=False)

if __name__=='__main__':main()
