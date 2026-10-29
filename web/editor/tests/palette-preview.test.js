import test from "node:test";
import assert from "node:assert/strict";
import { paletteAnimation, palettePreviewPixels } from "../palette-preview.js";
import { fixture } from "./fixtures.js";

test("legacy tiles get a stable preview without mutating artwork or project",()=>{
  const project=fixture(),tile=project.tiles[0],before=JSON.stringify(project);
  assert.deepEqual(paletteAnimation(tile,project),{fps:2,frames:[{id:"tile-palette-frame-0",paletteId:"red"}]});
  assert.equal(JSON.stringify(project),before);
});
test("preview validates old/missing palette references, frames and rate",()=>{
  const project=fixture(),tile=project.tiles[0];
  tile.paletteAnimation={fps:100,frames:[{id:"first",paletteId:"deleted"},{id:"second",paletteId:"blue"}]};
  const before=JSON.stringify(project);
  assert.deepEqual(paletteAnimation(tile,project),{fps:30,frames:[{id:"first",paletteId:"red"},{id:"second",paletteId:"blue"}]});
  assert.equal(JSON.stringify(project),before);
  tile.paletteAnimation={fps:0,frames:[]};
  assert.equal(paletteAnimation(tile,project).frames.length,1);
  assert.equal(paletteAnimation(tile,project).fps,2);
  tile.palettes=["missing"];
  assert.equal(paletteAnimation(tile,project).frames[0].paletteId,"red");
});
test("preview remaps indices to live palette colors, independent of artwork palette slots",()=>{
  const project=fixture(),tile=project.tiles[0],before=JSON.stringify(tile);
  const palette=project.palettes[1];
  const pixels=palettePreviewPixels(tile,palette);
  assert.deepEqual([...pixels.slice(0,4)],[0,0,255,255]);
  assert.deepEqual([...pixels.slice(8*4,8*4+4)],[255,255,0,255]);
  assert.equal(JSON.stringify(tile),before);
  palette.colors[1]=[0,31,31];
  assert.deepEqual([...palettePreviewPixels(tile,palette).slice(0,4)],[0,255,255,255]);
  tile.image.pixels[0]=3;
  assert.deepEqual([...palettePreviewPixels(tile,palette).slice(0,4)],[255,255,255,255]);
  tile.image.pixels[0]=-1;
  assert.deepEqual([...palettePreviewPixels(tile,palette).slice(0,4)],[0,0,0,0]);
});
