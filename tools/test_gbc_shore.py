"""Check displayed shore map/SCX alignment at raster time, including 8px wraps."""
from pyboy import PyBoy
from test_gbc_frames import ROM, MAP, symbol, code_symbol

def main():
    pyboy=PyBoy(str(ROM),window='null',sound_emulated=False)
    pyboy.set_emulation_speed(0)
    mem=pyboy.memory
    world=symbol(MAP.read_text(),'world')
    length=symbol(MAP.read_text(),'course_length')
    state={'preparing':None,'shown':None,'checks':0,'wraps':0,'last_scx':None,'errors':[]}
    def word(offset):return mem[world+offset] | mem[world+offset+1]<<8
    def before_prepare(_ctx):
        phase=mem[world+109]
        distance=sum(mem[world+257+i]<<(8*i) for i in range(4))//64
        pan=word(268)//64*2//3
        target=sum(mem[length+i]<<(8*i) for i in range(4))//64
        ending=phase==4 or (phase==1 and distance>target//2)
        coast=112-pan if phase==3 else 48-pan if phase==4 else 48+(target-distance)*2//3 if ending else 48-distance*2//3
        state['preparing']=(phase,distance,coast,ending,target)
    def before_present(_ctx):state['shown']=state['preparing']
    def raster(_ctx):
        shown=state['shown']
        if mem[0xFF45]!=72 or not shown:return
        phase,distance,coast,ending,target=shown
        if not (phase in (3,4) or phase==1 and (distance<128 or distance>target-192)):return
        if not 0<=coast<=144:return
        scx=mem[0xFF43]
        first=56 if ending else 55
        cols=[i for i in range(21) if mem[0x98C0+i]==first]
        actual=cols[0]*8-scx if cols else None
        if actual!=coast:
            state['errors'].append(f'phase={phase}, coast={coast}, map/SCX={actual}, SCX={scx}')
        if state['last_scx'] is not None and abs(scx-state['last_scx'])>=5:state['wraps']+=1
        state['last_scx']=scx
        state['checks']+=1
    pyboy.hook_register(0,code_symbol('render_prepare'),before_prepare,None)
    pyboy.hook_register(0,code_symbol('render_present'),before_present,None)
    pyboy.hook_register(0,code_symbol('lcd_isr'),raster,None)
    try:
        pyboy.tick(240);pyboy.button_press('start');pyboy.tick(20);pyboy.button_release('start');pyboy.tick(20)
        pyboy.button_press('right');pyboy.tick(120);pyboy.button_release('right')
        pyboy.button_press('left');pyboy.tick(50);pyboy.button_release('left')
        pyboy.button_press('right');pyboy.tick(140);pyboy.button_release('right');pyboy.tick(100)
        distance=3999*64
        for i in range(4):mem[world+257+i]=(distance>>(8*i))&255
        pyboy.tick(30)
        assert mem[world+109]==4
        pyboy.button_press('right');pyboy.tick(50);pyboy.button_release('right')
        pyboy.button_press('left');pyboy.tick(20);pyboy.button_release('left')
        pyboy.screen.image.save('/private/tmp/beaver-new-shore.png')
        assert state['checks']>200,state
        assert state['wraps']>=10,state
        assert not state['errors'],f"shore jumped in {len(state['errors'])} frames: {state['errors'][:6]}"
        print(f"{state['checks']} displayed shoreline checks and {state['wraps']} SCX wraps: map and pixel scroll stay synchronized")
    finally:pyboy.stop(save=False)

if __name__=='__main__':main()
