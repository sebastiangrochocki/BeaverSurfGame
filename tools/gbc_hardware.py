"""Conservative hardware DMA timing for PyBoy ROM regression tests.

PyBoy's general DMA currently copies VRAM without advancing CPU clocks (see
core/mb.py HDMA.set_hdma5). Execute the documented 64 double-speed CPU clocks
per 16 bytes in spare WRAM, plus a small conservative call overhead. No game
code or emulated physics state is patched. C700 is outside the game's buffers.
"""
import re
from test_gbc_frames import ROOT, NOI

class HardwareTiming:
    def __init__(self, pyboy):
        self.p=pyboy
        self.mem=pyboy.memory
        self.symbols={n:int(a,16) for n,a in re.findall(r'DEF _(\w+) 0x([\da-fA-F]+)',NOI.read_text())}
        self.errors=[]
        self.transfers=0
        self.palette_writes=0
        self.hooks=set()
        # push AF/BC; B blocks of twelve NOPs + DEC/JR (64 clocks);
        # restore BC/AF and return after the intercepted LDH instruction.
        stub=bytes([0xf5,0xc5,0x06,1]+[0]*12+[0x05,0x20,0xf1,0xc1,0xf1,0xc9])
        self.mem[0xc700:0xc700+len(stub)]=stub
        for path in (ROOT/'gbc/build/obj/src').glob('*.lst'):
            lines=path.read_text().splitlines()
            labels=[(int(mt[1],16),mt[2]) for line in lines
                    if (mt:=re.match(r'\s*([0-9A-F]{8})\s+\d+ _(\w+)::?$',line))]
            matches=[(off,name) for off,name in labels
                     if name in self.symbols and (self.symbols[name]&65535)<0x8000]
            if not matches:continue
            off,name=matches[0]
            bank=self.symbols[name]>>16
            base=(self.symbols[name]&65535)-off
            in_columns=False
            for line in lines:
                label=re.match(r'\s*[0-9A-F]{8}\s+\d+ _(\w+)::?$',line)
                if label:in_columns=label[1]=='present_shore_columns'
                store=re.match(r'\s*([0-9A-F]{8}).*\[.*?\].*\bld\s+\((de|hl\+?)\), a',line)
                if in_columns and store:
                    self._hook(bank,base+int(store[1],16),self._vram_store,store[2])
                mt=re.match(r'\s*([0-9A-F]{8}).*\[.*?\].*\bldh\s+\(_HDMA5_REG \+ 0\), a',line)
                if not mt:continue
                address=base+int(mt[1],16)
                assert 0x200<=address<0x8000
                self._hook(bank,address,self._dma,(address,2))
                self._hook(bank,address+2,self._after_dma,None)
        # These palette writes use an unrolled LDH (C),A in the raster routine.
        # Inspect actual linked opcodes, as in the footer regression test.
        rom=(ROOT/'web/beaver.gbc').read_bytes()
        pc=self.symbols['render_hud_palettes']
        lengths={0x47:1,0xb7:1,0x21:3,0x28:2,0x3e:2,0xea:3,0x0e:2,
                 0x78:1,0xfa:3,0xe6:2,0x20:2,0x2a:1,0xe2:1}
        while rom[pc]!=0xc9:
            opcode=rom[pc]
            if opcode==0xe2:self._hook(0,pc,self._palette,None)
            pc+=lengths[opcode]

    def _hook(self,bank,address,callback,context):
        assert (bank,address) not in self.hooks
        self.p.hook_register(bank,address,callback,context)
        self.hooks.add((bank,address))

    def _safe(self,where):
        if self.mem[0xff40]&128 and self.mem[0xff44]<144:
            self.errors.append(f'{where} escaped VBlank at LY={self.mem[0xff44]}')

    def _dma(self,context):
        pc,size=context
        value=self.p.register_file.A
        self._safe('DMA start')
        assert not value&128,'test models general DMA only'
        self.transfers+=1
        self.mem[0xff55]=value
        self.mem[0xc703]=(value&127)+1
        sp=self.p.register_file.SP-2
        self.mem[sp]=(pc+size)&255
        self.mem[sp+1]=(pc+size)>>8
        self.p.register_file.SP=sp
        self.p.register_file.PC=0xc700

    def _after_dma(self,_):
        self._safe('DMA end')

    def _palette(self,_):
        self.palette_writes+=1
        if self.mem[0xff40]&128 and self.mem[0xff41]&3==3:
            self.errors.append(f'palette write during Mode 3 at LY={self.mem[0xff44]}')

    def _vram_store(self,register):
        r=self.p.register_file
        address=(r.D<<8)|r.E if register=='de' else r.HL
        if 0x8000<=address<0xa000:self._safe('scenery column write')

    def assert_safe(self):
        assert not self.errors,self.errors[:10]
