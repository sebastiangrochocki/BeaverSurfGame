import { test } from 'node:test';
import assert from 'node:assert/strict';
import { rasterLine } from '../paint-gesture.js';
test('continuous horizontal, vertical, reversed and diagonal strokes',()=>{
  assert.equal([...rasterLine({x:1,y:2},{x:14,y:2},16,16)].length,14);
  for(const end of [{x:15,y:0},{x:0,y:15},{x:15,y:15},{x:3,y:12}]) {
    const points=[...rasterLine({x:0,y:0},end,16,16)];
    assert.deepEqual(points.at(-1),end);
    for(let i=1;i<points.length;i++)assert.equal(Math.max(Math.abs(points[i].x-points[i-1].x),Math.abs(points[i].y-points[i-1].y)),1);
    assert.deepEqual([...rasterLine(end,{x:0,y:0},16,16)].reverse(),points);
  }
});
test('capture outside art clips to finite pixels without wrapping or invalid writes',()=>{
  assert.equal([...rasterLine({x:-10000,y:3},{x:10000,y:3},16,16)].length,16);
  assert.deepEqual([...rasterLine({x:-20,y:-1},{x:20,y:-1},16,16)],[]);
  assert.deepEqual([...rasterLine({x:0,y:0},{x:0,y:0},16,16)],[{x:0,y:0}]);
});
