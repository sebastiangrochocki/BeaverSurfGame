export function fixture() {
  const colorsA = [
    [0, 0, 0],
    [31, 0, 0],
    [0, 31, 0],
    [31, 31, 31],
  ];
  const colorsB = [
    [0, 0, 0],
    [0, 0, 31],
    [31, 31, 0],
    [31, 31, 31],
  ];
  return {
    version: 2,
    id: "test-project",
    name: "Palette test",
    palettes: [
      { id: "red", name: "Red", colors: colorsA },
      { id: "blue", name: "Blue", colors: colorsB },
    ],
    tiles: [
      {
        id: "tile",
        name: "Asymmetric",
        w: 16,
        h: 8,
        palettes: ["red"],
        image: {
          w: 16,
          h: 8,
          pixels: Array.from({ length: 128 }, (_, i) => (i % 16 < 8 ? 1 : 2)),
          tilePal: [0, 0],
        },
      },
    ],
    sprites: [
      {
        id: "sprite",
        name: "Sprite",
        w: 8,
        h: 8,
        palettes: ["red"],
        fps: 8,
        frames: [
          { id: "frame", w: 8, h: 8, pixels: Array(64).fill(1), tilePal: [0] },
        ],
      },
    ],
    scenes: [
      {
        id: "scene",
        name: "Scene",
        w: 160,
        h: 144,
        bg: 0,
        bgPaletteId: "red",
        pixels: Array(160 * 144).fill(-1),
        tiles: {
          "0,0": {
            tileId: "tile",
            sx: 0,
            sy: 0,
            rotation: 0,
            paletteId: "red",
          },
        },
      },
    ],
  };
}
