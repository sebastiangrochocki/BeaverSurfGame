// Selection edits read a snapshot before writing, so overlapping moves never smear.
export function selectedIndices(w, h, rectangles) {
  const indices = new Set();
  for (const r of rectangles)
    for (let y = Math.max(0, r.y); y < Math.min(h, r.y + r.h); y++)
      for (let x = Math.max(0, r.x); x < Math.min(w, r.x + r.w); x++)
        indices.add(y * w + x);
  return indices;
}

export function editSelectedPixels(image, rectangles, empty, delta = null) {
  const pixels = image.pixels.slice();
  const indices = selectedIndices(image.w, image.h, rectangles);
  for (const i of indices) pixels[i] = empty;
  if (delta)
    for (const i of indices) {
      const x = i % image.w + delta.x, y = Math.floor(i / image.w) + delta.y;
      if (x >= 0 && y >= 0 && x < image.w && y < image.h)
        pixels[y * image.w + x] = image.pixels[i];
    }
  return pixels;
}

export function editSelectedCells(scene, rectangles, delta = null) {
  const keys = new Set();
  for (const r of rectangles)
    for (let y = Math.max(0, Math.floor(r.y / 8)); y < Math.min(scene.h / 8, Math.ceil((r.y + r.h) / 8)); y++)
      for (let x = Math.max(0, Math.floor(r.x / 8)); x < Math.min(scene.w / 8, Math.ceil((r.x + r.w) / 8)); x++)
        keys.add(`${x},${y}`);
  const cells = { ...scene.tiles };
  for (const key of keys) delete cells[key];
  if (delta)
    for (const key of keys) {
      const [sx, sy] = key.split(",").map(Number);
      const x = sx + delta.x / 8, y = sy + delta.y / 8;
      if (scene.tiles[key] && x >= 0 && y >= 0 && x < scene.w / 8 && y < scene.h / 8)
        cells[`${x},${y}`] = { ...scene.tiles[key] };
    }
  return cells;
}

export function selectionDelta(rectangles, w, h, x, y, grid = 1) {
  const left = Math.min(...rectangles.map(r => r.x));
  const top = Math.min(...rectangles.map(r => r.y));
  const right = Math.max(...rectangles.map(r => r.x + r.w));
  const bottom = Math.max(...rectangles.map(r => r.y + r.h));
  return {
    x: Math.max(-left, Math.min(w - right, Math.round(x / grid) * grid)),
    y: Math.max(-top, Math.min(h - bottom, Math.round(y / grid) * grid)),
  };
}

// Axis names flip both halves. Directional names clone only the source half.
// Snapshot reads prevent smearing; holes and outside artwork stay intact.
export function reflectionPairs(w, h, rectangles, direction) {
  if (!["horizontal", "vertical", "left-right", "right-left", "top-bottom", "bottom-top"].includes(direction))
    throw new Error("Unknown reflection direction");
  const indices = selectedIndices(w, h, rectangles);
  if (!indices.size) return [];
  let left=w, right=0, top=h, bottom=0;
  for (const i of indices) {
    const x=i%w, y=Math.floor(i/w);
    left=Math.min(left,x); right=Math.max(right,x);
    top=Math.min(top,y); bottom=Math.max(bottom,y);
  }
  const horizontal=["horizontal","left-right","right-left"].includes(direction);
  const clone=direction!=="horizontal" && direction!=="vertical";
  const forward=direction==="left-right" || direction==="top-bottom";
  const pairs=[];
  for (const i of indices) {
    const x=i%w, y=Math.floor(i/w);
    const nx=horizontal?left+right-x:x, ny=horizontal?y:top+bottom-y;
    const j=ny*w+nx;
    const a=horizontal?x:y, b=horizontal?nx:ny;
    if (indices.has(j) && (!clone || (forward?a<b:a>b))) pairs.push([i,j]);
  }
  return pairs;
}

export function reflectSelectedPixels(image, rectangles, direction) {
  const pixels=image.pixels.slice();
  for (const [source,dest] of reflectionPairs(image.w,image.h,rectangles,direction))
    pixels[dest]=image.pixels[source];
  return pixels;
}

// Hardware palettes belong to whole 8×8 cells. Only transfer when every pixel
// is reflected from one source cell. Partial-cell edits keep the destination
// palette, just like drawing indexed pixels.
export function reflectedPaletteCells(w,h,rectangles,direction) {
  const cells=new Map();
  for (const [s,d] of reflectionPairs(w,h,rectangles,direction)) {
    const source=`${Math.floor((s%w)/8)},${Math.floor(s/w/8)}`;
    const dest=`${Math.floor((d%w)/8)},${Math.floor(d/w/8)}`;
    const cell=cells.get(dest) || {source,count:0,mixed:false};
    cell.count++; cell.mixed ||= cell.source!==source;
    cells.set(dest,cell);
  }
  return [...cells].filter(([,c])=>c.count===64 && !c.mixed).map(([d,c])=>[c.source,d]);
}

export function reflectSelectedCells(scene,rectangles,direction) {
  const cells={...scene.tiles};
  const rects=rectangles.map(r=>({x:r.x/8,y:r.y/8,w:r.w/8,h:r.h/8}));
  const w=scene.w/8;
  const horizontal=["horizontal","left-right","right-left"].includes(direction);
  for (const [s,d] of reflectionPairs(w,scene.h/8,rects,direction)) {
    const source=scene.tiles[`${s%w},${Math.floor(s/w)}`];
    const key=`${d%w},${Math.floor(d/w)}`;
    if (!source) delete cells[key];
    else {
      const flag=horizontal?"flipX":"flipY";
      cells[key]={...source,[flag]:!source[flag]};
    }
  }
  return cells;
}
