"""Verify palette-only surf at the shoreline, isolated from sand/water/sky/HUD."""
from pathlib import Path
from PIL import Image
from pyboy import PyBoy
from test_gbc_frames import ROM, MAP, ROOT, symbol, code_symbol


def main():
    pyboy=PyBoy(str(ROM),window='null',sound_emulated=False)
    pyboy.set_emulation_speed(0)
    mem=pyboy.memory
    world=symbol(MAP.read_text(),'world')
    visible=symbol(MAP.read_text(),'scx_beach')
    observations={'original':None,'surf':None,'frame':None,'end':set(),'errors':[]}
    def rgb(r,g,b):return (r|(g<<5)|(b<<10)).to_bytes(2,'little')
    sand,water,shadow,foam=rgb(28,24,17),rgb(2,9,14),rgb(26,20,13),rgb(28,30,29)
    mid,light=rgb(4,15,20),rgb(12,23,25)
    expected_palettes=(sand+water+shadow+foam,sand+water+foam+water,sand+water+water+foam)
    expected_water=(water+mid+light+foam,water+mid+mid+light,mid+water+light+light)
    def raster(_context):
        if mem[0xFF45]!=72:return
        old=mem[0xFF68]
        palettes=[]
        for i in range(64):
            mem[0xFF68]=i
            palettes.append(mem[0xFF69])
        mem[0xFF68]=old
        palettes=bytes(palettes)
        phase=mem[world+109]
        if not mem[visible]:
            if phase==0:observations['original']=(palettes[32:40],palettes[48:56])
            if phase==1:observations['surf']=(palettes[32:40],palettes[48:56])
            return
        shore=palettes[48:56]
        assert shore in expected_palettes,f'wrong shoreline palette: {shore.hex()}'
        state=expected_palettes.index(shore)
        assert palettes[32:40]==expected_water[state],f'wrong beach-water palette at phase {state+1}'
        previous_bank=mem[0xFF4F]
        mem[0xFF4F]=1
        attrs=bytes(mem[0x98C0+y*32+x] for y in range(10) for x in range(20))
        patterns=bytes(mem[0x8000+tile*16+i] for tile in (50,51,52,53,54,55,56,57,62) for i in range(16))
        mem[0xFF4F]=0
        cells=bytes(mem[0x98C0+y*32+x] for y in range(10) for x in range(20))
        mem[0xFF4F]=previous_bank
        for tile,attr in zip(cells,attrs):
            expected=3 if tile==50 or tile>=70 else 6 if tile in (55,56,57,62) else 4
            assert attr&7==expected,f'palette animation leaked: tile {tile}, palette {attr&7}, expected {expected}'
        if phase==3:
            observations['frame']=(state,palettes,patterns,cells,attrs)
        if phase==4:
            assert all(attr&0x20 for tile,attr in zip(cells,attrs) if tile in (55,56,57,62)),'end shoreline lost its mirror'
            observations['end'].add(state)
    def checked_raster(context):
        # PyBoy reports and ignores Python hook exceptions; propagate them through
        # the test instead of allowing a false success after a hook assertion.
        if observations['errors']:return
        try:raster(context)
        except Exception as error:observations['errors'].append(str(error))
    pyboy.hook_register(0,code_symbol('lcd_isr'),checked_raster,None)
    try:
        pyboy.tick(240)
        assert observations['original'] is not None
        pyboy.button_press('start');pyboy.tick(20);pyboy.button_release('start')
        snapshots={};sequence=[];previous=None
        for _ in range(180):
            pyboy.tick()
            assert not observations['errors'],observations['errors']
            current=observations['frame']
            if current is None:continue
            state=current[0]
            if not sequence or sequence[-1]!=state:sequence.append(state)
            if state==previous and state not in snapshots:
                snapshots[state]=(current,pyboy.screen.image.copy())
            previous=state
        assert set(snapshots)=={0,1,2},'shoreline did not show all three palettes'
        assert len(sequence)>=5 and all(b==(a+1)%3 for a,b in zip(sequence,sequence[1:])),sequence
        default,di=snapshots[0]
        reference=Image.open(ROOT/'gbc/res/beach/shoreline.png').convert('RGB')
        sea_reference=Image.open(ROOT/'gbc/res/beach/water.png').convert('RGB')
        for state,(current,image) in sorted(snapshots.items()):
            assert default[2:]==current[2:],'palette animation modified tile pixels, map IDs, or attributes'
            assert default[1][:32]==current[1][:32] and default[1][40:48]==current[1][40:48] and default[1][56:]==current[1][56:],'an unrelated palette changed'
            assert default[1][48:52]==current[1][48:52],'base shoreline sand/water changed'
            changed={(x,y) for y in range(48,128) for x in range(160) if di.getpixel((x,y))!=image.getpixel((x,y))}
            colors=() if state==0 else ((214,165,107),(173,214,239)) if state==1 else ((214,165,107),)
            expected={(x,y) for y in range(48,128) for x in range(112,128)
                      if reference.getpixel((x-112,(y-48)%16)) in colors}
            sea_colors=() if state==0 else ((99,173,222),) if state==1 else ((16,49,132),(41,107,189))
            expected.update((x,y) for y in range(48,128) for x in range(128,160)
                            if sea_reference.getpixel(((x-128)%16,(y-48)%16)) in sea_colors)
            assert changed==expected,f'state {state+1}: changed {len(changed)} pixels, expected {len(expected)} shoreline/water pixels'
            image.save(f'/private/tmp/beaver-shore-phase-{state+1}.png')
        pyboy.button_press('right');pyboy.tick(170);pyboy.button_release('right');pyboy.tick(220)
        assert not observations['errors'],observations['errors']
        assert observations['surf']==observations['original'],'surfing palette was not restored exactly'
        value=3999*64
        for i in range(4):mem[world+257+i]=(value>>(8*i))&255
        pyboy.tick(160)
        assert not observations['errors'],observations['errors']
        assert mem[world+109]==4 and observations['end']=={0,1,2},'destination shore did not animate all three palette states'
        print('Three exact shore/water palettes cycle 1->2->3->1; exact shoreline/water pixel masks, unchanged inland/sky/HUD/shop palettes and tile bytes, mirrored coast and both surf palettes restored')
    finally:
        pyboy.stop(save=False)


if __name__=='__main__':main()
