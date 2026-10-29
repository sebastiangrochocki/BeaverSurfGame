// Preview metadata references live palettes; it never owns pixel/color copies.
export function paletteAnimation(tile, project) {
  const valid = new Set(project.palettes.map(p => p.id));
  const fallback = tile.palettes?.find(id => valid.has(id)) || project.palettes[0]?.id;
  const source = tile.paletteAnimation;
  const frames = Array.isArray(source?.frames) && source.frames.length
    ? source.frames : [{ paletteId: fallback }];
  const fps = Number(source?.fps);
  return {
    fps: Number.isFinite(fps) && fps > 0 ? Math.max(1, Math.min(30, Math.round(fps))) : 2,
    frames: frames.map((frame, index) => ({
      id: frame?.id || `${tile.id}-palette-frame-${index}`,
      paletteId: valid.has(frame?.paletteId) ? frame.paletteId : fallback,
    })),
  };
}

export function palettePreviewPixels(tile, palette) {
  const pixels = new Uint8ClampedArray(tile.w * tile.h * 4);
  const colors = palette.colors.map(color => color.map(v => Math.round(v * 255 / 31)));
  for (let i = 0; i < tile.w * tile.h; i++) {
    const index = tile.image.pixels[i];
    if (index < 0 || !colors[index]) continue;
    pixels.set([...colors[index], 255], i * 4);
  }
  return pixels;
}
