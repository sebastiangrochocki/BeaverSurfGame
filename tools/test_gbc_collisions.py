"""Compare C collision mechanics against the canonical PNG alpha, not mask tables."""
import ctypes
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]

def points(name,frame=0,w=32,h=32,art=True,filled=False):
    im=Image.open(ROOT/('gbc/art' if art else 'gbc/res')/name).convert('RGBA')
    out=set()
    for y in range(h):
        xs=[x for x in range(w) if im.getpixel((frame*w+x,y))[3]]
        if filled and xs: xs=range(min(xs),max(xs)+1)
        out.update((x,y) for x in xs)
    return out

def check(lib):
    targets=[(0,points('gull.png',frame=i,h=16,art=False),1,23,i*8) for i in range(3)]
    targets += [(1,points('heart.png',w=16,h=16,art=False),8,23,0)]
    cases=[('surf.png',0,0,0),('right.png',2,0,0),('left.png',1,0,0),('tug.png',8,0,0),('jump.png',0,1,0)]
    jump=points('jump.png')
    checks=0
    for name,input_,air,flip in cases:
        body=points(name)
        for kind,target,ox,oy,tick in targets:
            lib.actor_test_state(air,flip,tick)
            for dx in range(-45,46,3):
                for dy in range(-42,25,2):
                    nx=(96+dx)*2//3-64+ox
                    expected=bool(body & {(x+nx,y+dy+oy) for x,y in target})
                    actual=bool(lib.actor_touches(input_,kind,(96+dx)*64,(112+dy)*64))
                    assert actual==expected,(name,kind,dx,dy,actual,expected)
                    checks+=1
    for turn,flip in enumerate((26,19,13,6)):
        body=jump
        for _ in range(turn): body={(y,31-x) for x,y in body}
        target=targets[0][1]
        lib.actor_test_state(1,flip,0)
        for dx in range(-36,37,6):
            for dy in range(-38,25,3):
                nx=(96+dx)*2//3-64+1
                expected=bool(body & {(x+nx,y+dy+23) for x,y in target})
                assert bool(lib.actor_touches(0,0,(96+dx)*64,(112+dy)*64))==expected,('flip',turn,dx,dy)
                checks+=1
    lib.actor_test_state(0,0,0)
    assert not lib.actor_touches(8,0,96*64,81*64),'tug empty top eight rows caused a hit'
    assert lib.actor_touches(0,0,96*64,81*64),'standing body should intersect that same gull'
    assert not lib.actor_test_step_gull(8,17),'actual Down mechanics damaged through empty top band'
    assert lib.actor_test_step_gull(8,24),'actual visible tug body contact should cause damage'
    assert lib.actor_test_step_gull(0,17),'same head-level gull must damage the standing pose'
    lib.actor_test_state(0,0,0)
    assert lib.actor_touches(0,2,96*64,112*64),'grounded board must collect small rings'
    assert not lib.actor_touches(0,2,160*64,112*64),'far ring must not collect'
    print(f'{checks} independent PNG overlap checks; gull/heart precision, all flips, tug top-band safety, real damage and grounded ring collection pass')

def main():
    with tempfile.TemporaryDirectory(prefix='beaver-collisions-') as work:
        library=Path(work)/'actors.so'
        sources=['gbc/src/'+name+'.c' for name in ('rules','wave','actors','save','shop','surf','race')]
        subprocess.run(['cc','-shared','-fPIC','-std=c99','-O1','-o',str(library),*sources,'gbc/test/actor_harness.c'],cwd=ROOT,check=True)
        lib=ctypes.CDLL(str(library))
        lib.actor_test_state.argtypes=[ctypes.c_uint8,ctypes.c_uint8,ctypes.c_uint32]
        lib.actor_touches.argtypes=[ctypes.c_uint8,ctypes.c_uint8,ctypes.c_int16,ctypes.c_int16]
        lib.actor_touches.restype=ctypes.c_uint8
        lib.actor_test_step_gull.argtypes=[ctypes.c_uint8,ctypes.c_int16]
        lib.actor_test_step_gull.restype=ctypes.c_uint8
        check(lib)

if __name__=='__main__': main()
