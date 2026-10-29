import { test } from "node:test";
import assert from "node:assert/strict";
import { fixture } from "./fixtures.js";
import { stampCells, recolorCell, scenePixels, tileBrushBounds } from "../scene-model.js";

test("brush footprint and snapping follow the entire rotated asset, not its 8×8 cells",()=>{
  const tile={image:{w:32,h:16}};
  for(const rotation of [0,90,180,270]) {
    const w=rotation%180?16:32,h=rotation%180?32:16;
    assert.deepEqual(tileBrushBounds(tile,rotation,9,9),{x:0,y:0,w,h});
    assert.deepEqual(tileBrushBounds(tile,rotation,w+3,h+3),{x:w,y:h,w,h});
    assert.deepEqual(tileBrushBounds(tile,rotation,-1,-1),{x:-w,y:-h,w,h});
  }
  assert.deepEqual(tileBrushBounds({image:{w:16,h:16}},0,15,15),{x:0,y:0,w:16,h:16});
});

test("different placements retain palettes and links through backup round trip", () => {
  const p = fixture(),
    s = p.scenes[0];
  stampCells(s, p.tiles[0], 16, 0, 0, "blue");
  const pixel = (x) => [...scenePixels(p, s).slice(x * 4, x * 4 + 4)];
  assert.deepEqual(pixel(0), [255, 0, 0, 255]);
  assert.deepEqual(pixel(16), [0, 0, 255, 255]);
  recolorCell(s, 1, 1, "blue");
  assert.equal(s.tiles["0,0"].tileId, "tile");
  assert.equal(s.tiles["2,0"].paletteId, "blue");
  const imported = JSON.parse(JSON.stringify(p));
  assert.deepEqual(
    scenePixels(imported, imported.scenes[0]),
    scenePixels(p, s)
  );
  p.tiles[0].image.pixels[0] = 3;
  assert.deepEqual(pixel(0), [255, 255, 255, 255]);
  assert.deepEqual(pixel(16), [255, 255, 255, 255]);
  assert.equal(recolorCell(s, 100, 100, "blue"), false);
});

test("rectangular rotation, partial overwrite and palette overrides are independent", () => {
  for (const rotation of [0, 90, 180, 270]) {
    const p = fixture(),
      s = p.scenes[0];
    s.tiles = {};
    stampCells(s, p.tiles[0], 0, 0, rotation, "blue");
    assert.equal(Object.keys(s.tiles).length, 2);
    assert.ok(s.tiles[rotation % 180 ? "0,1" : "1,0"]);
    recolorCell(s, 0, 0, "red");
    assert.equal(
      Object.values(s.tiles).filter((c) => c.paletteId === "blue").length,
      1
    );
    assert.ok(Object.values(s.tiles).every((c) => c.rotation === rotation));
    assert.equal(scenePixels(p, s).length, 160 * 144 * 4);
  }
});
