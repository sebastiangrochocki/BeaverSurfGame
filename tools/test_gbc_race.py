"""Native-ROM race flow and sprite/frame budget, using the real joypad path.

The fixture only skips to an affordable stage-2 beach; the offer, countdown,
run-in, race, pause and settlement then run through the shipping ROM.
"""
from pathlib import Path
import re
import sys
from pyboy import PyBoy
from test_gbc_frames import ROOT, ROM, MAP, NOI, symbol, scanline_peak, oam

def main():
    stage=int(sys.argv[1]) if len(sys.argv)>1 else 2
    assert 2<=stage<=255
    course_px=4000+(stage-1)*500
    text=MAP.read_text();base=symbol(text,'world');dropped=symbol(text,'frames_dropped')
    def addr(name):
        return int(re.search(r'DEF _'+name+r' 0x([0-9A-Fa-f]+)',NOI.read_text())[1],16)
    r=addr('race');length=addr('course_length');pool=addr('pickups')
    p=PyBoy(str(ROM),window='null',sound_emulated=False);p.set_emulation_speed(0);m=p.memory
    def get(a,n=2):return sum(m[a+i]<<(i*8) for i in range(n))
    def put(a,v,n=2):
        for i in range(n):m[a+i]=(v>>(i*8))&255
    def tap(key):p.button_press(key);p.tick(4);p.button_release(key);p.tick(4)
    try:
        p.tick(240);tap('start');assert m[base+109]==3
        m[base+263]=stage;m[base+254]=min(stage-1,8);put(length,course_px*64,4);put(base+101,3000,4)
        put(base+10,90*64);put(base+12,136*64)
        p.button_press('right');p.tick(100);p.button_release('right');p.tick(12)
        assert get(base+10)==96*64 and get(base+268)==0,'rival collision failed or moved camera'
        assert get(base+14)==0,'blocked beach walk still moving'
        p.screen.image.save('/private/tmp/beaver-rival-collision.png')
        tap('a');assert m[r+67]==1,'offer did not open'
        p.screen.image.save('/private/tmp/beaver-race-offer.png')
        tap('left');tap('a');assert m[r+67]==2,'offer not accepted'
        assert m[r+79]==80 and m[addr('race_challenges')]==1,'first rival challenge must use 80% aim'
        assert get(r+43,4)==2000 and get(base+101,4)==0,'stake charged incorrectly'
        p.screen.image.save('/private/tmp/beaver-race-countdown.png')
        for _ in range(500):
            p.tick()
            if m[r+67]==4:break
        assert m[r+67]==4 and m[base+109]==1,'run-in never reached race'
        before=get(dropped);peak=0;saw_target=False;dual=0;paused=False
        p.button_press('a');p.button_press('right')
        for frame in range(course_px//2+1500):
            p.tick()
            if m[r+67]!=4:break
            count,line=scanline_peak(oam(m));peak=max(peak,count)
            assert count<=10,(frame,line,count)
            entries=[tuple(m[0xfe00+i*4+j] for j in range(4)) for i in range(40) if m[0xfe00+i*4]]
            player=[s for s in entries if s[3]&7 in (0,7) and s[2]<136]
            npc=[s for s in entries if s[3]&7==6 and (s[2]<112 or 164<=s[2]<172)]
            if player and npc:dual+=1
            targets=sum(bool(m[pool+i*5+4]&1) for i in range(12))+sum(bool(m[base+28+i]) for i in range(4))
            assert targets<=1,('crowded race',frame,targets)
            saw_target|=bool(targets)
            if frame==600:
                p.screen.image.save('/private/tmp/beaver-race-surfing.png')
                tap('start');snapshot=bytes(m[base:base+272])+bytes(m[r:r+81]);p.tick(40)
                assert snapshot==bytes(m[base:base+272])+bytes(m[r:r+81]),'pause advanced a racer/course'
                tap('start');paused=True
        assert m[r+67]==5,'race did not show results'
        assert dual>100 and saw_target and paused
        assert get(dropped)==before,('dropped ticks',before,get(dropped))
        p.button_release('a');p.button_release('right');p.tick(12)
        p.screen.image.save('/private/tmp/beaver-race-results.png')
        money=get(base+101,4);position=bytes(m[base+10:base+18]);p.tick(120)
        assert get(base+101,4)==money and bytes(m[base+10:base+18])==position,'results did not lock/settle once'
        tap('a');assert m[r+72]==1 and m[r+67]==5
        p.tick(30);p.screen.image.save('/private/tmp/beaver-race-stats.png')
        tap('a');assert m[r+67]==6 and m[base+109]==4
        print(f'ROM race stage {stage}: solid beach rival/camera, real offer/countdown/run-in, {dual} dual-rider frames, sequential targets, peak {peak}/10 objects per scanline, shared pause, no dropped ticks, locked results and one-time settlement pass')
    finally:p.stop(save=False)

if __name__=='__main__':main()
