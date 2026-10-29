import {test} from 'node:test';
import assert from 'node:assert/strict';
import {indexedPng} from '../png-import.js';

const rgba=(width,height,pixel)=>({width,height,data:Uint8ClampedArray.from(Array.from({length:width*height},(_,i)=>pixel(i%width,Math.floor(i/width))).flat())});
test('PNG strips preserve transparent pixels, black ink, RGB555 colors and shared per-cell palettes',()=>{
  const art=indexedPng(rgba(16,8,(x,y)=>x===0&&y===0?[0,0,0,0]:x===1?[0,0,0,255]:x<8?[255,0,0,255]:[0,255,0,255]),{sprite:true,frameWidth:8,frameHeight:8});
  assert.equal(art.images.length,2);assert.equal(art.images[0].pixels[0],-1);
  assert.equal(art.images[0].pixels[1],1);
  assert.deepEqual(art.palettes,[[[0,0,0],[0,0,0],[31,0,0],[0,31,0]]]);
  assert.equal(art.images[1].pixels[0],3);
  assert.deepEqual(art.images.map(im=>im.tilePal),[[0],[0]]);
});
test('PNG import uses separate hardware palettes for different cells and rejects unsupported colors/dimensions',()=>{
  const art=indexedPng(rgba(16,8,(x)=>[x<8?255:0,x<8?0:255,(x%4)*80,255]));
  assert.equal(art.palettes.length,2);assert.deepEqual(art.images[0].tilePal,[0,1]);
  assert.throws(()=>indexedPng(rgba(7,8,()=>[0,0,0,0])),/multiples of 8/);
  assert.throws(()=>indexedPng(rgba(8,8,x=>[x*30,0,0,255])),/at most 4/);
  assert.throws(()=>indexedPng(rgba(8,8,x=>[(x%4)*80,0,0,255]),{sprite:true}),/at most 3/);
  assert.equal(indexedPng(rgba(8,8,()=>[0,0,0,0]),{sprite:true}).palettes.length,1);
});
