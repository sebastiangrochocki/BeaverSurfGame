"""Real-joypad regression: Down tucks; A then Down (or A+Down) changes lanes."""
from pyboy import PyBoy
from test_gbc_frames import ROM,MAP,symbol,u16

def main():
    p=PyBoy(str(ROM),window='null',sound_emulated=False);p.set_emulation_speed(0)
    m=p.memory;w=symbol(MAP.read_text(),'world');d=symbol(MAP.read_text(),'frames_dropped')
    def tap(key):p.button_press(key);p.tick(4);p.button_release(key);p.tick(4)
    try:
        p.tick(240);tap('start');p.button_press('right')
        for _ in range(300):
            p.tick()
            if m[w+109]==1:break
        p.button_release('right');assert m[w+109]==1
        baseline=u16(m,d)
        p.button_press('down')
        for _ in range(180):
            p.tick();assert not m[w+18] and m[w+25]==m[w+26]==0,'ground tug changed lane'
        p.screen.image.save('/private/tmp/beaver-down-tug-stays-lane.png')
        p.button_press('a')
        for _ in range(360):
            p.tick()
            if m[w+18]:break
        assert m[w+18] and m[w+26]==1 and not m[w+19],'A+held Down did not jump/drop cleanly'
        p.button_release('a')
        for _ in range(200):
            p.tick()
            if not m[w+18] and m[w+25]==1:break
        assert not m[w+18] and m[w+25]==m[w+26]==1
        for _ in range(120):
            p.tick();assert not m[w+18] and m[w+25]==m[w+26]==1,'lower-lane tug changed lanes'
        p.button_release('down');tap('up')
        assert not m[w+18] and m[w+26]==1,'Up alone jumped/climbed'
        p.button_press('a')
        for _ in range(360):
            p.tick()
            if m[w+18]:break
        assert m[w+18]
        p.button_release('a');tap('up')
        assert m[w+26]==0,'jump then Up did not request upper lane'
        assert u16(m,d)==baseline,'controls regression dropped ticks'
        print('ROM joypad: sustained tuck stays on both lanes; A+held Down jumps/drops without a flip; Up alone stays grounded; jump then Up climbs; no dropped ticks')
    finally:p.stop(save=False)

if __name__=='__main__':main()
