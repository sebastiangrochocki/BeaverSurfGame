# GBC art workspace

The editor is still served as static files. `workspace.js` is the checked-in local
bundle; visitors do not need Node, a CDN, or an application server.

UI colors use the artist's neutral tokens and purple-300 (`#4965d4`) for selected
actions. Open Sans is served locally from `opensans/`: Regular (400) for text,
Medium (500) for labels, Semibold (600) for buttons, and Bold (700) for headings.
These styles do not alter artwork palette colors.

The workspace fits the viewport; long lists and expanded inspector groups scroll
inside their own panels. Fit sizes artwork to the available canvas area. At 1×,
2× or 3×, use Space-drag, middle-drag or idle trackpad scrolling to pan oversized
artwork. Painting captures the pointer and suppresses scrolling until release,
including cancellation or window blur. Fast strokes interpolate all crossed
pixels and remain one undo step. Pixel tools have a one-pixel cursor; scene tile
stamps snap and step by the full selected tile's rotated width and height.

To rebuild after editing JavaScript or Radix components:

```sh
cd web/editor
npm ci
npm run build
npm test
```

Serve `web/` on port 8086 to run `node node_modules/@playwright/test/cli.js test`.
Tests use fresh browser contexts and seed their own IndexedDB project. They never
operate on the user's browser storage.

Import PNG replaces only the current asset and is undoable. Dimensions must be
multiples of 8, no larger than 1024×1024. Sprites split a horizontal strip when
it matches the current frame size; otherwise they import as one frame. Alpha is
preserved, RGB colors are rounded to native RGB555, and 8×8 cells share up to
eight palettes (three visible colors plus transparency for sprites, four for
background art). Unsupported color counts are rejected without changing art.
Linked tiles must retain their dimensions; scene PNGs must match the scene size
and replace pixel details without removing linked tile placements. Project
creation/import resets playback, frame selection and mode together.

`ui.jsx` supplies Radix mode tabs and collapsible inspector groups.
All editor glyphs use Lucide React: the action registry in `icons.jsx` covers
tools, asset/animation/project actions, flips and mirror clones. Familiar tools
use pencil, paint bucket, pipette, eraser and dashed marquee shapes. Radix Themes
keeps control behavior/accessibility; a build-time icon adapter supplies Lucide
checks and chevrons too. There is no separate Radix Icons dependency. Rebuilding
from a clean install must retain the adapter in `build.js`.
`controls.jsx` renders every visible form/action control through Radix Themes:
Select menus, Slider, Checkbox, TextField, Button, IconButton, Tooltip, Link,
and AlertDialog. The original DOM fields are hidden controller bindings only.
Their native popup menus and browser sliders are never shown. `workspace.css`
is the checked-in bundled Radix stylesheet and must be served with `workspace.js`.
Palette
creation, duplication, presets, and color editing live in the Palettes mode.
Sprites, Tiles, and Scenes select from the shared palette dropdown. `editor.js`
owns editing commands and canvas interaction. `scene-model.js` resolves linked
pixels, rotation, and palette overrides for both canvas rendering and PNG output.

Scene cells store `{tileId, sx, sy, rotation, paletteId, flipX?, flipY?}` at their destination
`"x,y"` coordinate. `paletteId` is a stable project palette UUID, not a library
index. Recolor changes this field, preserving the tile link and source subcell.
Choosing a preset creates/selects a palette; it does not replace an existing one.
Editing a palette's color definition intentionally updates its usages. Referenced
palettes cannot be deleted. Pixel details have their own sparse `pixelPalettes`
map, separate from linked tile placements.

Old version-2 scenes missing palette IDs retain their previously displayed
palette (the first project palette); migration fills the IDs before new edits.
Project backups include palette IDs and definitions. PNG export uses native-size
scene pixels, without editor guides, selection outlines, or zoom.

Select: drag a rectangle; Shift-drag outside it to add another. Drag inside an
existing selection (with or without Shift) to move the selected pixels together.
Delete/Backspace clears only the selected layer; tile pixels clear to opaque
color 0, sprite/detail pixels to transparency. Moves and deletes are undoable;
Escape cancels a move or deselects. Scene tile-layer selections snap to 8×8 cells
and preserve linked tile IDs, rotations, and palette IDs when moved.

Select also exposes two Radix buttons: Flip horizontal (left↔right) and Flip
vertical (top↔bottom). They mirror the entire selection about its union's
center, swapping both halves rather than copying one half over the other.
The separate Mirror clone row offers left→right, right→left, top→bottom and
bottom→top: copy only the named source half to create symmetry, retaining it.
Odd center lines stay unchanged; unselected holes and outside pixels are never
written. Transparent pixels reflect too. Each click is one undoable edit and
affects only the current sprite frame or scene layer. Full 8×8 destination cells
carry the source palette; partial-cell edits copy indices under the destination
palette, matching the editor's hardware cell model. Tile-layer reflection copies
linked placements with per-cell flips after rotation; source tiles remain live.

Sprites show a Radix Onion skin checkbox directly above the canvas. It toggles
the previous frame's overlay without changing artwork or PNG exports. This
view preference is remembered locally and is hidden in Tiles and Scenes.

Tiles have a Palette animation preview beside the editable repeating preview.
Clone/delete palette frames, choose a shared palette for each frame, and set
1–30 FPS (default 2). Play/Stop controls only this preview; painting and undo
continue to update it while it plays. Switching assets/modes or hiding the page
stops playback. Palette names and colors are resolved live by stable ID.

Each tile's optional `paletteAnimation: {fps, frames: [{id, paletteId}]}` is saved
with the project and included in backups. It stores references, not copies of
pixels or palettes. Legacy tiles get a non-mutating default frame. Preview
frames never alter the artwork palette, scene placements, PNGs, or the ROM.
Referenced preview palettes cannot be removed until their frames are reassigned.
Preview edits support undo/redo; Play state and the selected frame are temporary.
