"""Author small, editable 2bpp shop tiles; no external art dependencies.

Tile pixels and compositions deliberately live here so the placeholder shop can
be reworked without changing its mechanics. Outputs are build artifacts.
"""
from pathlib import Path
import re
from PIL import Image, ImageDraw
def encode(pixels,width,x,y):
    out=[]
    for py in range(8):
        low=high=0
        for px in range(8):
            value=pixels[(y+py)*width+x+px]
            low|=(value&1)<<(7-px);high|=((value>>1)&1)<<(7-px)
        out.extend((low,high))
    return out

ROOT = Path(__file__).resolve().parents[1]
settings=(ROOT/'gbc/src/shop.h').read_text()
def setting(name):return int(re.search(r'^#define '+name+r' (\d+)\b',settings,re.M)[1])
room_columns=setting('SHOP_ROOM_WIDTH')//8+1
board_left=setting('SHOP_BOARD_LEFT')//8
board_step=setting('SHOP_BOARD_STEP')//8
counter_left=setting('SHOP_COUNTER_LEFT')//8
keeper_left=setting('SHOP_KEEPER_LEFT')//8
tiles = []
def tile(draw):
    image=Image.new('L',(8,8),0)
    draw(ImageDraw.Draw(image))
    tiles.append(image)
    return len(tiles)-1

tile(lambda d: None)                                       # 0 wall fill
tile(lambda d: (d.line((0,7,7,7),fill=1),d.line((0,0,0,6),fill=2),d.point((6,2),fill=2))) # 1 siding
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.line((0,7,7,7),fill=1),d.line((3,0,3,6),fill=0))) # 2 floor
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.line((0,6,7,6),fill=1),d.line((0,0,7,0),fill=3))) # 3 roof
tile(lambda d: d.polygon(((0,7),(7,0),(7,7)),fill=2)) # 4 roof corner
tile(lambda d: d.polygon(((0,0),(7,7),(0,7)),fill=2)) # 5 roof corner
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.rectangle((1,1,6,6),fill=1),d.rectangle((2,2,5,5),fill=3),d.line((4,1,4,6),fill=2))) # 6 window
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.rectangle((1,0,7,7),fill=1),d.line((2,0,2,7),fill=3))) # 7 door left top
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.rectangle((0,0,6,7),fill=1),d.line((5,0,5,7),fill=3))) # 8 door right top
tile(lambda d: (d.rectangle((0,0,7,7),fill=1),d.line((1,0,1,7),fill=2),d.line((0,7,7,7),fill=3))) # 9 door left lower
tile(lambda d: (d.rectangle((0,0,7,7),fill=1),d.line((6,0,6,7),fill=2),d.rectangle((3,2,4,3),fill=3),d.line((0,7,7,7),fill=3))) # 10 door right lower
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.line((0,0,7,0),fill=3),d.line((0,2,7,2),fill=1))) # 11 counter top
tile(lambda d: (d.rectangle((0,0,7,7),fill=2),d.rectangle((1,1,6,6),outline=0),d.line((0,7,7,7),fill=1))) # 12 counter front
tile(lambda d: d.polygon(((1,1),(6,1),(3,5)),fill=3)) # 13 selection marker
tile(lambda d: (d.rectangle((0,0,7,7),fill=1),d.line((0,0,0,7),fill=2),d.line((7,0,7,7),fill=2))) # 14 exit
tile(lambda d: (d.line((0,1,7,1),fill=3),d.line((0,3,7,3),fill=1))) # 15 wall rail

board=Image.new('L',(16,40),0)
d=ImageDraw.Draw(board)
d.polygon(((7,0),(11,4),(14,12),(14,28),(11,36),(7,39),(3,35),(1,27),(1,12),(4,4)),fill=1)
d.polygon(((7,2),(10,6),(12,13),(12,28),(9,35),(7,37),(4,33),(3,26),(3,13),(5,6)),fill=2)
d.line((7,5,7,33),fill=3,width=2)
d.line((4,23,11,23),fill=3)
for y in range(0,40,8):
    for x in range(0,16,8): tiles.append(board.crop((x,y,x+8,y+8))) #16..25

# Use the actual beaver head behind the counter, with a light shop apron.
npc=Image.open(ROOT/'gbc/art/surf.png').convert('RGBA').crop((0,0,32,24))
indexed=Image.new('L',npc.size)
indexed.putdata([0 if a==0 else 1 if (r,g,b)==(0,0,0) else 2 if (r,g,b)==(156,90,58) else 3
                 for r,g,b,a in npc.getdata()])
for y in range(0,24,8):
    for x in range(0,32,8): tiles.append(indexed.crop((x,y,x+8,y+8))) #26..37

# Exterior decorations fit unused patterns in the existing 64-tile scene slot.
# Sand / dark brown / leaf green / leaf highlight; no sprites or extra VRAM.
palm_base=len(tiles)
palm=Image.new('L',(24,48),0)
d=ImageDraw.Draw(palm)
d.line(((9,46),(10,37),(12,28),(14,19),(13,11)),fill=1,width=3)
for y,x in ((23,13),(29,11),(35,10),(41,9)):
    d.line((x,y,x+2,y+1),fill=0)
d.line((6,47,10,45,13,47),fill=1)
fronds=[((13,11),(7,3),(1,0),(2,5),(8,8)),
        ((13,11),(12,2),(15,0),(17,3),(15,9)),
        ((13,11),(20,3),(23,2),(22,7),(17,10)),
        ((13,11),(4,8),(0,10),(1,14),(7,12)),
        ((13,11),(5,13),(0,19),(4,18),(10,14)),
        ((13,11),(19,10),(23,14),(23,20),(20,16),(15,14))]
for points in fronds:d.polygon(points,fill=2)
for points in (((2,2),(8,5),(13,11)),((15,2),(14,8),(13,11)),
               ((22,4),(17,8),(13,11)),((1,11),(8,10),(13,11)),
               ((2,17),(7,13),(13,11)),((13,11),(19,13),(22,17))):
    d.line(points,fill=3)
d.ellipse((10,12,13,15),fill=1);d.ellipse((14,12,17,15),fill=1)
d.point((11,13),fill=0);d.point((15,13),fill=0)
for y in range(0,48,8):
    for x in range(0,24,8):tiles.append(palm.crop((x,y,x+8,y+8))) #38..55
flag_pole=tile(lambda d:(d.line((3,0,3,7),fill=1),d.line((4,0,4,7),fill=2)))
flag_edge=tile(lambda d:(d.polygon(((0,0),(7,3),(0,7)),fill=3),
                         d.line((0,0,7,3,0,7),fill=1)))
assert len(tiles)<=64
while len(tiles)<64:tiles.append(Image.new('L',(8,8),0))
data=[v for t in tiles for v in encode(list(t.getdata()),8,0,0)]
rows=[','.join(f'0x{x:02x}' for x in data[i:i+16]) for i in range(0,len(data),16)]
source=('/* Generated by tools/gen_shop_assets.py. */\n'
        f'#define SHOP_PALM_PATTERN {palm_base}\n#define SHOP_FLAG_POLE {flag_pole}\n#define SHOP_FLAG_EDGE {flag_edge}\n'
        'static const uint8_t shop_patterns[1024]={\n'+',\n'.join(rows)+'\n};\n')
# Bake the room composition too: scrolling must not repeatedly assemble every
# board/counter pixel on the SM83's frame budget.
room=[];attrs=[]
for row in range(5,16):
    for wx in range(room_columns):
        pattern=2 if row>=12 else 1
        palette=3
        if row==6:pattern=15
        if 7<=row<12:
            for n in range(4):
                if board_left+board_step*n<=wx<board_left+board_step*n+2:
                    pattern=16+(row-7)*2+wx-(board_left+board_step*n);palette=4+n
        if wx==1 and row>=8:pattern=14
        if 8<=row<11 and keeper_left<=wx<keeper_left+4:pattern=26+(row-8)*4+wx-keeper_left
        if counter_left<=wx<counter_left+8 and row==11:pattern=11
        if counter_left<=wx<counter_left+8 and row==12:pattern=12
        room.append(70+pattern);attrs.append(8|palette)
for name,values in [('shop_room',room),('shop_attrs',attrs)]:
    source+=f'static const uint8_t {name}[{len(values)}]={{\n'+',\n'.join(','.join(str(v) for v in values[i:i+room_columns]) for i in range(0,len(values),room_columns))+'\n};\n'
(ROOT/'gbc/src/shop_data.h').write_text(source)

# All walking PNGs are canonical supplied artwork, never generated here.
print('Shop: 38 room + 18 palm + 2 flag patterns in the existing 64-tile scene slot')
