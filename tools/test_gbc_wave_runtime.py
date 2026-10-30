"""Compare all baked wave samples to the original C calculation and linked ROM."""
import re
import subprocess
import tempfile
from pathlib import Path
from gen_wave_runtime import ROOT, tables

def main():
    with tempfile.TemporaryDirectory(prefix='beaver-wave-') as folder:
        binary=str(Path(folder)/'reference')
        subprocess.run(['cc','-std=c99','-O2',str(ROOT/'gbc/src/wave.c'),
                        str(ROOT/'gbc/test/wave_runtime_test.c'),'-o',binary],check=True)
        expected=subprocess.check_output([binary])
    actual=b''.join(tables())
    assert len(actual)==9*1024*16*2
    assert actual==expected,'ROM wave generator differs from original physics'
    rom=(ROOT/'web/beaver.gbc').read_bytes()
    assert len(rom)==512*1024 and rom[0x148]==4,'expected 32-bank MBC5 ROM'
    assert rom[8*16384:26*16384]==actual,'linked wave ROM banks differ from reference'
    link_map=(ROOT/'gbc/build/beaver.map').read_text()
    data_end=int(re.search(r'([0-9A-F]{8})\s+s__HEAP\b',link_map)[1],16)
    assert data_end<=0xc500,'ordinary WRAM overlaps the fixed beach buffers'
    print('147456 wave samples: all 9 tiers / 1024 ticks exactly match C physics and linked ROM')

if __name__=='__main__':main()
