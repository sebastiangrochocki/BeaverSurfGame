import { test } from "node:test";
import assert from "node:assert/strict";
import { editSelectedPixels, editSelectedCells, selectionDelta, reflectSelectedPixels, reflectSelectedCells, reflectedPaletteCells } from "../selection-model.js";
import { scenePixels } from "../scene-model.js";
import { fixture } from "./fixtures.js";

test("overlapping selections move once, from a snapshot, without smearing", () => {
  const im = { w: 8, h: 1, pixels: [1, 2, 3, 0, 0, 0, 0, 0] };
  const rects = [{ x: 0, y: 0, w: 2, h: 1 }, { x: 1, y: 0, w: 2, h: 1 }];
  assert.deepEqual(editSelectedPixels(im, rects, 0, { x: 1, y: 0 }), [0, 1, 2, 3, 0, 0, 0, 0]);
  assert.deepEqual(im.pixels, [1, 2, 3, 0, 0, 0, 0, 0]);
});

test("delete uses transparent sprite/detail pixels, opaque tile color zero", () => {
  const im = { w: 4, h: 1, pixels: [3, 2, 1, 3] };
  const rects = [{ x: 1, y: 0, w: 2, h: 1 }];
  assert.deepEqual(editSelectedPixels(im, rects, -1), [3, -1, -1, 3]);
  assert.deepEqual(editSelectedPixels(im, rects, 0), [3, 0, 0, 3]);
});

test("moves preserve holes and indexed colors; rectangles clip at edges", () => {
  const im = { w: 4, h: 2, pixels: [1, -1, 2, 3, 0, 0, 0, 0] };
  assert.deepEqual(editSelectedPixels(im, [{ x: -1, y: 0, w: 3, h: 1 }], -1, { x: 0, y: 1 }),
    [-1, -1, 2, 3, 1, -1, 0, 0]);
});

test("drag delta keeps all selected pixels inside the artboard", () => {
  const rects = [{ x: 2, y: 3, w: 2, h: 4 }, { x: 6, y: 1, w: 2, h: 1 }];
  assert.deepEqual(selectionDelta(rects, 16, 16, 100, -100), { x: 8, y: -1 });
  assert.deepEqual(selectionDelta([{ x: 8, y: 8, w: 8, h: 8 }], 32, 32, 11, -7, 8), { x: 8, y: -8 });
});

test("scene tile-layer moves and deletes preserve the remaining links and palette IDs", () => {
  const a = { tileId: "cloud", sx: 1, sy: 0, rotation: 90, paletteId: "sky" };
  const b = { tileId: "sand", sx: 0, sy: 0, rotation: 0, paletteId: "beach" };
  const scene = { w: 32, h: 16, tiles: { "0,0": a, "1,0": b, "3,0": b } };
  const rects = [{ x: 0, y: 0, w: 16, h: 8 }];
  assert.deepEqual(editSelectedCells(scene, rects, { x: 8, y: 8 }), { "1,1": a, "2,1": b, "3,0": b });
  assert.deepEqual(editSelectedCells(scene, rects), { "3,0": b });
  assert.equal(scene.tiles["0,0"], a);
});

test("whole-selection flips swap both halves and preserve outside pixels", () => {
  const im={w:6,h:4,pixels:Array.from({length:24},(_,i)=>i)};
  const r=[{x:1,y:1,w:4,h:2}];
  for (const direction of ["horizontal","vertical"]) {
    const actual=reflectSelectedPixels(im,r,direction);
    const expected=im.pixels.slice();
    if(direction==="horizontal") for(let y=1;y<=2;y++)for(let x=1;x<=4;x++)expected[y*6+x]=im.pixels[y*6+5-x];
    else for(let x=1;x<=4;x++){expected[12+x]=im.pixels[6+x];expected[6+x]=im.pixels[12+x];}
    assert.deepEqual(actual,expected,direction);
    assert.deepEqual(reflectSelectedPixels({...im,pixels:actual},r,direction),im.pixels,"two flips restore the source");
  }
  assert.deepEqual(im.pixels,Array.from({length:24},(_,i)=>i));
});

test("mirror clone is a separate directional operation that retains the source half", () => {
  const im={w:4,h:2,pixels:[1,-1,2,3,0,1,2,3]},r=[{x:0,y:0,w:4,h:2}];
  assert.deepEqual(reflectSelectedPixels(im,r,"left-right"),[1,-1,-1,1,0,1,1,0]);
  assert.deepEqual(reflectSelectedPixels(im,r,"right-left"),[3,2,2,3,3,2,2,3]);
  assert.deepEqual(reflectSelectedPixels(im,r,"top-bottom"),[1,-1,2,3,1,-1,2,3]);
  assert.deepEqual(reflectSelectedPixels(im,r,"bottom-top"),[0,1,2,3,0,1,2,3]);
  const scene={w:16,h:8,tiles:{"0,0":{tileId:"board",paletteId:"blue",rotation:90}}};
  const cloned=reflectSelectedCells(scene,[{x:0,y:0,w:16,h:8}],"left-right");
  assert.deepEqual(cloned["0,0"],scene.tiles["0,0"]);
  assert.deepEqual(cloned["1,0"],{...scene.tiles["0,0"],flipX:true});
});

test("reflection preserves transparent values, odd center, holes, clipped masks and overlapping unions", () => {
  const im={w:7,h:1,pixels:[1,-1,2,3,9,9,9]};
  const rects=[{x:-1,y:0,w:5,h:1},{x:2,y:0,w:5,h:1}];
  assert.deepEqual(reflectSelectedPixels(im,rects,"horizontal"),[9,9,9,3,2,-1,1]);
  assert.deepEqual(reflectSelectedPixels(im,[{x:0,y:0,w:2,h:1},{x:5,y:0,w:2,h:1}],"horizontal"),[9,9,2,3,9,-1,1]);
  assert.deepEqual(reflectSelectedPixels(im,[],"vertical"),im.pixels);
});

test("reflection carries palettes only for fully mapped hardware cells", () => {
  assert.deepEqual(reflectedPaletteCells(32,16,[{x:0,y:0,w:32,h:16}],"horizontal"),
    [["0,0","3,0"],["1,0","2,0"],["2,0","1,0"],["3,0","0,0"],["0,1","3,1"],["1,1","2,1"],["2,1","1,1"],["3,1","0,1"]]);
  assert.deepEqual(reflectedPaletteCells(32,16,[{x:1,y:0,w:30,h:15}],"horizontal"),[["1,0","2,0"],["2,0","1,0"]]);
  assert.deepEqual(reflectedPaletteCells(32,8,[{x:1,y:0,w:31,h:8}],"horizontal"),[]);
});

test("scene reflection preserves linked tiles, rotation, palette and prior flips; blanks erase", () => {
  const tile={tileId:"tile",sx:1,sy:0,rotation:90,paletteId:"blue",flipY:true};
  const scene={w:32,h:16,tiles:{"0,0":tile,"2,0":{tileId:"old"}}};
  const result=reflectSelectedCells(scene,[{x:0,y:0,w:32,h:16}],"horizontal");
  assert.deepEqual(result,{"3,0":{...tile,flipX:true},"1,0":{tileId:"old",flipX:true}});
  assert.equal(scene.tiles["2,0"].tileId,"old");
  const twice=reflectSelectedCells({...scene,tiles:result},[{x:0,y:0,w:32,h:16}],"horizontal");
  assert.equal(twice["0,0"].flipX,false);
});

test("scene rendering mirrors within rotated source cells and stays live-linked", () => {
  const p=fixture(),s=p.scenes[0];
  p.tiles[0].image.pixels=Array.from({length:128},(_,i)=>(Math.floor(i/16)+i%16)%4);
  const original={...s,tiles:{...s.tiles}};
  s.tiles=reflectSelectedCells(s,[{x:0,y:0,w:16,h:8}],"horizontal");
  const check=()=>{
    const rgba=scenePixels(p,s);
    const before=scenePixels(p,original);
    for(let y=0;y<8;y++) for(let x=0;x<8;x++)
      assert.deepEqual(before.slice((y*s.w+x)*4,(y*s.w+x)*4+4),rgba.slice((y*s.w+15-x)*4,(y*s.w+15-x)*4+4));
  };
  check(); p.tiles[0].image.pixels[0]=2; p.palettes[0].colors[2]=[31,0,31]; check();
});
