// A placement owns its palette ID; source pixels remain linked to the tile asset.
export function tileBrushBounds(tile, rotation, x, y) {
  const sideways = rotation % 180 !== 0;
  const w = sideways ? tile.image.h : tile.image.w;
  const h = sideways ? tile.image.w : tile.image.h;
  return { x: Math.floor(x / w) * w, y: Math.floor(y / h) * h, w, h };
}

export function rotateImage(image, rotation = 0) {
  let out = image;
  for (let n = 0; n < rotation / 90; n++) {
    const next = { w: out.h, h: out.w, pixels: [], tilePal: [] };
    for (let y = 0; y < out.h; y++)
      for (let x = 0; x < out.w; x++) {
        const nx = out.h - 1 - y,
          ny = x;
        next.pixels[ny * next.w + nx] = out.pixels[y * out.w + x];
        next.tilePal[Math.floor(ny / 8) * (next.w / 8) + Math.floor(nx / 8)] =
          out.tilePal?.[Math.floor(y / 8) * (out.w / 8) + Math.floor(x / 8)] ||
          0;
      }
    out = next;
  }
  return out;
}

export function stampCells(scene, tile, x, y, rotation, paletteId) {
  const image = rotateImage(tile.image, rotation);
  for (let sy = 0; sy < image.h / 8; sy++)
    for (let sx = 0; sx < image.w / 8; sx++) {
      const dx = Math.floor(x / 8) + sx,
        dy = Math.floor(y / 8) + sy;
      if (dx < 0 || dy < 0 || dx >= scene.w / 8 || dy >= scene.h / 8) continue;
      scene.tiles[`${dx},${dy}`] = {
        tileId: tile.id,
        sx,
        sy,
        rotation,
        paletteId,
      };
    }
}

export function recolorCell(scene, x, y, paletteId) {
  const cell = scene.tiles[`${Math.floor(x / 8)},${Math.floor(y / 8)}`];
  if (!cell || cell.paletteId === paletteId) return false;
  cell.paletteId = paletteId;
  return true;
}

export function scenePixels(project, scene) {
  const pixels = new Uint8ClampedArray(scene.w * scene.h * 4);
  const byId = new Map(project.palettes.map((p) => [p.id, p]));
  const paint = (x, y, index, id) => {
    if (x < 0 || y < 0 || x >= scene.w || y >= scene.h || index < 0) return;
    const c = (byId.get(id) || project.palettes[0]).colors[index];
    const offset = (y * scene.w + x) * 4;
    pixels.set([...c.map((v) => Math.round((v * 255) / 31)), 255], offset);
  };
  for (let y = 0; y < scene.h; y++)
    for (let x = 0; x < scene.w; x++)
      paint(x, y, scene.bg || 0, scene.bgPaletteId);
  const cache = new Map();
  for (const [key, cell] of Object.entries(scene.tiles)) {
    const tile = project.tiles.find((t) => t.id === cell.tileId);
    if (!tile) continue;
    const cacheKey = `${tile.id}:${cell.rotation || 0}`;
    if (!cache.has(cacheKey))
      cache.set(cacheKey, rotateImage(tile.image, cell.rotation));
    const image = cache.get(cacheKey),
      [cx, cy] = key.split(",").map(Number);
    const sx = cell.sx || 0,
      sy = cell.sy || 0;
    const sourcePalette = image.tilePal?.[sy * (image.w / 8) + sx] || 0;
    const paletteId = cell.paletteId || tile.palettes[sourcePalette];
    for (let y = 0; y < 8; y++)
      for (let x = 0; x < 8; x++)
        paint(
          cx * 8 + x,
          cy * 8 + y,
          image.pixels[(sy * 8 + (cell.flipY ? 7-y : y)) * image.w + sx * 8 + (cell.flipX ? 7-x : x)],
          paletteId
        );
  }
  for (let y = 0; y < scene.h; y++)
    for (let x = 0; x < scene.w; x++) {
      const key = `${Math.floor(x / 8)},${Math.floor(y / 8)}`;
      paint(
        x,
        y,
        scene.pixels[y * scene.w + x],
        scene.pixelPalettes?.[key] || scene.bgPaletteId
      );
    }
  return pixels;
}
