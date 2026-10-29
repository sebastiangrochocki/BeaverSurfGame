"""Exercise the actual GBC shop with joypad input and inspect saved/VRAM state."""
from pyboy import PyBoy
from io import BytesIO
import re
from test_gbc_frames import ROM,MAP,symbol,code_symbol,scanline_peak,oam

def main():
    heap=re.search(r'([0-9A-Fa-f]{8})\s+s__HEAP\b',MAP.read_text())
    assert heap and int(heap[1],16)<0xc500,'ordinary data overlaps reserved scene WRAM'
    p=PyBoy(str(ROM),ram_file=BytesIO(bytes(8192)),window='null',sound_emulated=False)
    p.set_emulation_speed(0)
    m=p.memory;w=symbol(MAP.read_text(),'world')
    address={name:code_symbol(name) for name in ['shop_mode','shop_owned','shop_equipped','shop_selected','shop_message','shop_x','shop_direction','frames_dropped']}
    sprite_palette={}
    scroll={'enabled':False,'checks':0,'pixels':0,'hud':None,'offsets':set(),'errors':[]}
    playfield_scroll=code_symbol('scx_playfield')
    def palette(_context):
        boundary=m[0xff45]
        if scroll['enabled']:
            # Inspect the offset from the band just drawn, before this ISR
            # changes SCX. All interior bands must share the committed camera.
            expected=0 if boundary==16 else m[playfield_scroll]
            actual=m[0xff43]
            if actual!=expected:
                scroll['errors'].append((boundary,actual,expected))
            if boundary==40:
                scroll['checks']+=1;scroll['offsets'].add(expected)
        if m[0xff45]!=72:return
        old=m[0xff6a];colors=[]
        for i in range(8):m[0xff6a]=i;colors.append(m[0xff6b])
        m[0xff6a]=old;sprite_palette['current']=bytes(colors)
    p.hook_register(0,code_symbol('lcd_isr'),palette,None)
    def value(name):return m[address[name]]
    def word(addr):return m[addr]|m[addr+1]<<8
    def phase():return m[w+109]
    def bankbytes(bank,addr,size):
        old=m[0xff4f];m[0xff4f]=bank
        result=bytes(m[addr:addr+size]);m[0xff4f]=old
        return result
    def tap(button):
        p.button_press(button);p.tick(5);p.button_release(button);p.tick(6)
    def until(button,predicate,limit=800):
        p.button_press(button)
        for _ in range(limit):
            p.tick()
            if scroll['enabled'] and phase()==5 and value('shop_mode')==0:
                # Both rows contain the same repeated plank artwork. Compare
                # rendered pixels across the old fixed/scrolled band boundary.
                image=p.screen.image
                assert image.crop((0,20,160,21)).tobytes()==image.crop((0,44,160,45)).tobytes(), 'visible wooden wall seam during camera movement'
                if word(address['shop_x'])<144*64 and scroll['hud'] is not None:
                    assert image.crop((0,128,160,144)).tobytes()==scroll['hud'], 'dialogue pixels moved or letters broke while room scrolled'
                scroll['pixels']+=1
            if predicate():break
        else:raise AssertionError(f'{button} timeout, phase={phase()}')
        p.button_release(button);p.tick(6)
    def score(amount):
        for i in range(4):m[w+101+i]=(amount>>(i*8))&255
    def screenshot(name):
        p.screen.image.save('/private/tmp/beaver-shop-'+name+'.png')
        print(name,'dropped ticks',word(address['frames_dropped']))
    try:
        p.tick(240)
        original_patterns=bankbytes(1,0x8460,1024)
        original_sky=(bankbytes(0,0x9840,96),bankbytes(1,0x9840,96))
        tap('start');p.tick(24);assert phase()==3
        screenshot('beach')
        # Repeated real joypad transitions exercise both streamed frame caches
        # and catch staging work that exceeds the frame budget.
        for _ in range(4):
            for button in ('down','up'):
                p.button_press(button);p.tick(24);p.button_release(button);p.tick(2)
        until('up',lambda:word(w+12)==104*64,60)
        assert value('shop_direction')==2
        screenshot('back')
        until('down',lambda:word(w+12)==144*64,70)
        assert value('shop_direction')==1
        screenshot('front')
        until('up',lambda:word(w+12)==104*64,70)
        until('right',lambda:word(w+10)>=90*64,100)
        assert phase()==3
        tap('a');p.tick(24);assert phase()==5
        screenshot('interior-left')
        board_columns=[x for x in range(21) if m[0x98e0+x]==86]
        assert board_columns==[7,11,15,19],f'wall-board spacing is not 16px: {board_columns}'
        scroll['enabled']=True
        scroll['hud']=p.screen.image.crop((0,128,160,144)).tobytes()
        assert bankbytes(1,0x8460,1024)!=original_patterns
        tap('a');assert value('shop_mode')==0
        until('right',lambda:word(address['shop_x'])>=180*64,240)
        assert scroll['checks']>100 and scroll['offsets']==set(range(8)),scroll
        assert not scroll['errors'],f"shop bands/HUD scroll mismatch: {scroll['errors'][:8]}"
        screenshot('counter')
        # The counter is visible, player and carrying board stay whole.
        assert scanline_peak(oam(m))[0]<=10
        tap('a');assert value('shop_mode')==1
        tap('right');assert value('shop_selected')==1
        score(3999);tap('a')
        assert value('shop_message')==1 and value('shop_owned')==1
        screenshot('insufficient')
        score(4000);tap('a')
        assert value('shop_equipped')==1 and value('shop_owned')==3
        assert bytes(m[w+101:w+105])==b'\0'*4
        screenshot('purchased')
        score(177);p.button_press('a');p.tick(90);p.button_release('a');p.tick(8)
        assert sum(m[w+101+i]<<(8*i) for i in range(4))==177
        tap('left');tap('a');assert value('shop_equipped')==0
        tap('right');tap('a');assert value('shop_equipped')==1
        # Exercise the other price tiers and boundary selection.
        tap('right');assert value('shop_selected')==2
        score(6000);tap('a');assert value('shop_equipped')==2 and value('shop_owned')==7
        tap('right');tap('right');assert value('shop_selected')==3
        score(9999);tap('a');assert value('shop_message')==1
        score(10000);tap('a');assert value('shop_equipped')==3 and value('shop_owned')==15
        sunset=b''.join((r|(g<<5)|(b<<10)).to_bytes(2,'little') for r,g,b in [(0,0,0),(4,2,0),(23,14,6),(29,19,4)])
        assert sprite_palette['current']==sunset,'purchase did not apply the rider/board palette'
        screenshot('sunset')
        m[0]=0x0a;saved=bytes(m[0xa008:0xa010]);ram=bytes(m[0xa000:0xc000]);m[0]=0
        assert saved[:6]==b'BVS1\x0f\x03' and saved[7]==sum(saved[:7])%256
        tap('b');assert value('shop_mode')==0
        until('left',lambda:word(address['shop_x'])<=156*64,60)
        assert not scroll['errors'],f"reverse shop scroll mismatch: {scroll['errors'][:8]}"
        scroll['enabled']=False
        until('left',lambda:phase()==3,400)
        p.tick(24)
        screenshot('exit')
        assert (bankbytes(0,0x9840,96),bankbytes(1,0x9840,96))==original_sky
        assert value('shop_equipped')==3
        assert sprite_palette['current']==sunset,'equipped palette changed on leaving shop'
        until('right',lambda:phase()==1,250);p.tick(200)
        assert bankbytes(1,0x8460,1024)==original_patterns,'surf ramp patterns not restored'
        assert sprite_palette['current']==sunset,'surfing lost the purchased colors'
        screenshot('surfing')
        assert word(address['frames_dropped'])==0,word(address['frames_dropped'])
        reboot=PyBoy(str(ROM),ram_file=BytesIO(ram),window='null',sound_emulated=False)
        try:
            reboot.set_emulation_speed(0);reboot.tick(240)
            assert reboot.memory[address['shop_owned']]==15 and reboot.memory[address['shop_equipped']]==3,'battery ownership/equipment did not survive reboot'
        finally:reboot.stop(save=False)
        print(f"Shop upper/lower wall synchronized across all 8 offsets, both directions; {scroll['checks']} raster / {scroll['pixels']} rendered-pixel checks, HUD fixed")
        print('GBC joypad shop: 2D beach, door, scrolling, counter, all prices, funds, equip, SRAM, exit, sky/ramp restoration and no dropped ticks pass')
    finally:p.stop(save=False)

if __name__=='__main__':main()
