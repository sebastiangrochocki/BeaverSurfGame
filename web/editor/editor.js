/* GBC Art Workspace: sprites, reusable tiles, and linked scenes. */
import {
  rotateImage,
  stampCells,
  recolorCell,
  scenePixels,
  tileBrushBounds,
} from "./scene-model.js";
import {syncControls,notify as alert,confirmAction as confirm} from './controls.jsx';
import { editSelectedPixels, editSelectedCells, selectionDelta, reflectSelectedPixels, reflectSelectedCells, reflectedPaletteCells } from './selection-model.js';
import { paletteAnimation, palettePreviewPixels } from './palette-preview.js';
import { rasterLine } from './paint-gesture.js';
import { indexedPng } from './png-import.js';
export function startEditor() {
  const $ = (id) => document.getElementById(id),
    DB = "beaver-gbc-art",
    STORE = "projects",
    KEY = "current";
  const uid = (p) => p + "-" + crypto.randomUUID(),
    cp = (x) => JSON.parse(JSON.stringify(x));
  const GAME = [
    [
      "HUD",
      [
        [8, 32, 72],
        [255, 255, 255],
        [220, 48, 56],
        [64, 192, 88],
      ],
    ],
    [
      "Sky",
      [
        [16, 52, 128],
        [40, 108, 188],
        [96, 176, 224],
        [176, 216, 240],
      ],
    ],
    [
      "Water",
      [
        [18, 77, 114],
        [36, 125, 163],
        [101, 184, 204],
        [228, 245, 237],
      ],
    ],
    [
      "Beaver",
      [
        [0, 0, 0],
        [0, 0, 0],
        [156, 90, 60],
        [153, 0, 48],
      ],
    ],
  ];
  const pal = (name, colors) => ({
    id: uid("pal"),
    name,
    colors: colors.map((c) => c.map((v) => Math.round((v * 31) / 255))),
  });
  const blank = (w, h, p = 0, transparent = false) => ({
    w,
    h,
    pixels: new Array(w * h).fill(transparent ? -1 : 0),
    tilePal: new Array((w * h) / 64).fill(p),
  });
  function fresh() {
    const palettes = GAME.map((x) => pal(x[0], x[1]));
    return {
      version: 2,
      id: uid("project"),
      name: "Beaver Surf Art",
      palettes,
      tiles: [
        {
          id: uid("tile"),
          name: "Blank tile",
          w: 8,
          h: 8,
          palettes: [palettes[2].id],
          image: blank(8, 8),
        },
      ],
      sprites: [
        {
          id: uid("sprite"),
          name: "New sprite",
          w: 32,
          h: 32,
          fps: 8,
          palettes: [palettes[3].id],
          frames: [{ id: uid("frame"), ...blank(32, 32, 0, true) }],
        },
      ],
      scenes: [
        {
          id: uid("scene"),
          name: "New scene",
          w: 160,
          h: 144,
          bg: 0,
          tiles: {},
          pixels: new Array(160 * 144).fill(-1),
        },
      ],
    };
  }
  function migrate(d) {
    if (d && d.version === 2 && d.tiles && d.sprites && d.scenes) return d;
    const p = fresh();
    if (!d || !d.frames) return p;
    const pals = (d.palettes || []).map((x) => ({
      id: uid("pal"),
      name: x.name || "Imported",
      colors: x.colors || [
        [0, 0, 0],
        [31, 31, 31],
        [16, 16, 16],
        [8, 8, 8],
      ],
    }));
    p.palettes.push(...pals);
    const w = d.frameW || 32,
      h = d.frameH || 32;
    if (d.kind === "tile")
      (d.frames || []).forEach((f, i) =>
        p.tiles.push({
          id: uid("tile"),
          name: "Imported tile " + (i + 1),
          w,
          h,
          palettes: pals.map((x) => x.id),
          image: { w, h, pixels: f.pixels, tilePal: f.tilePal },
        })
      );
    else
      p.sprites.push({
        id: uid("sprite"),
        name: "Imported sprite",
        w,
        h,
        fps: 8,
        palettes: pals.map((x) => x.id),
        frames: d.frames.map((f) => ({
          id: uid("frame"),
          w,
          h,
          pixels: f.pixels,
          tilePal: f.tilePal,
        })),
      });
    return p;
  }
  let project = fresh(),
    mode = "sprite",
    paletteMode = false,
    assetId = project.sprites[0].id,
    selectedTileId = project.tiles[0].id,
    frameIndex = 0,
    playFrame = 0,
    layer = "pixels",
    rotation = 0,
    color = 2,
    selectedPalette = 0,
    tool = "draw",
    drag = false,
    last = null,
    hover = null,
    timer = null,
    saving = false,
    readonly = false,
    selections = [],
    selectionStart = null,
    selectionPreview = null,
    selectionMove = null;
  let paletteTimer = null, paletteFrame = 0, paletteAsset = null, paletteUIKey = null;
  let activePointer=null,pan=null,spacePan=false,paintBatch=null,strokeSeen=new Set();
  const hist = new Map(),
    redo = new Map(),
    view = $("view"),
    ctx = view.getContext("2d"),
    tabId = uid("tab"),
    lockKey = "beaver-gbc-art-lock";
  function acquireLock() {
    try {
      const old = JSON.parse(localStorage.getItem(lockKey) || "null"),
        now = Date.now();
      if (old && old.tab !== tabId && now - old.at < 5000) {
        readonly = true;
      } else {
        localStorage.setItem(lockKey, JSON.stringify({ tab: tabId, at: now }));
        readonly = false;
      }
    } catch (_) {
      readonly = false;
    }
  }
  setInterval(() => {
    if (!readonly)
      try {
        localStorage.setItem(
          lockKey,
          JSON.stringify({ tab: tabId, at: Date.now() })
        );
      } catch (_) {}
  }, 2000);
  window.addEventListener("storage", (e) => {
    if (e.key === lockKey) acquireLock();
  });
  window.addEventListener("beforeunload", () => {
    try {
      if (JSON.parse(localStorage.getItem(lockKey) || "null")?.tab === tabId)
        localStorage.removeItem(lockKey);
    } catch (_) {}
  });
  acquireLock();
  function db() {
    return new Promise((ok, no) => {
      const r = indexedDB.open(DB, 1);
      r.onupgradeneeded = () => r.result.createObjectStore(STORE);
      r.onsuccess = () => ok(r.result);
      r.onerror = () => no(r.error);
    });
  }
  async function load() {
    try {
      const d = await db(),
        v = await new Promise((ok, no) => {
          const r = d.transaction(STORE).objectStore(STORE).get(KEY);
          r.onsuccess = () => ok(r.result);
          r.onerror = () => no(r.error);
        });
      if (v) project = migrate(v);
      preserveLegacyScenePalettes(project);
    } catch (_) {
      $("status").textContent =
        "Storage unavailable; export your project for recovery.";
    }
    assetId = assets()[0].id;
  }
  async function save() {
    if (readonly) return;
    saving = true;
    status();
    try {
      const d = await db();
      await new Promise((ok, no) => {
        const tx = d.transaction(STORE, "readwrite");
        tx.objectStore(STORE).put(project, KEY);
        tx.oncomplete = ok;
        tx.onerror = tx.onabort = () => no(tx.error);
      });
      saving = false;
      status();
    } catch (_) {
      saving = false;
      $("status").textContent = "Save failed; export the project for recovery.";
    }
  }
  function assets() {
    return mode === "sprite"
      ? project.sprites
      : mode === "tile"
      ? project.tiles
      : project.scenes;
  }
  function current() {
    return assets().find((x) => x.id === assetId);
  }
  function image() {
    const a = current();
    return mode === "sprite"
      ? a.frames[frameIndex]
      : mode === "tile"
      ? a.image
      : null;
  }
  function palettes(a) {
    return ((a && a.palettes) || [])
      .map((id) => project.palettes.find((p) => p.id === id))
      .filter(Boolean);
  }
  function rgb(a, p, transparent = false, index = color) {
    if (transparent) return [17, 17, 20];
    const q = a?.palettes ? palettes(a) : project.palettes,
      c = (q[p] || q[0] || project.palettes[0]).colors[index] || [0, 0, 0];
    return c.map((v) => Math.round((v * 255) / 31));
  }
  function changed(fn) {
    if (readonly) return;
    if(paintBatch) {paintBatch.push(fn);return;}
    if (timer) { clearInterval(timer); timer = null; }
    const k = mode + ":" + assetId;
    if (!hist.has(k)) hist.set(k, []);
    if (!drag || !changed.stroke) hist.get(k).push(cp(project));
    if (drag) changed.stroke = true;
    if (hist.get(k).length > 80) hist.get(k).shift();
    redo.set(k, []);
    fn();
    cachedScene=null;
    render(!drag);
    clearTimeout(changed.timer);
    changed.timer = setTimeout(save, 300);
  }
  function rotated(im, r) {
    let o = im;
    for (let n = 0; n < r / 90; n++) {
      const q = {
        w: o.h,
        h: o.w,
        pixels: new Array(o.w * o.h).fill(0),
        tilePal: new Array((o.w * o.h) / 64).fill(0),
      };
      for (let y = 0; y < o.h; y++)
        for (let x = 0; x < o.w; x++)
          q.pixels[x * q.w + q.w - 1 - y] = o.pixels[y * o.w + x];
      o = q;
    }
    return o;
  }
  function draw(a, im, z, ox = 0, oy = 0, alpha = 1) {
    if (!im) return;
    ctx.globalAlpha = alpha;
    for (let y = 0; y < im.h; y++)
      for (let x = 0; x < im.w; x++) {
        const v = im.pixels[y * im.w + x];
        if (v < 0) continue;
        const p = im.tilePal
          ? im.tilePal[Math.floor(y / 8) * (im.w / 8) + Math.floor(x / 8)]
          : 0;
        ctx.fillStyle = `rgb(${rgb(a, p, false, v).join(",")})`;
        ctx.fillRect((x + ox) * z, (y + oy) * z, z, z);
      }
    ctx.globalAlpha = 1;
  }
  function drawPart(a, im, z, ox, oy, sx, sy) {
    const part = { w: 8, h: 8, pixels: [], tilePal: [0] };
    for (let y = 0; y < 8; y++)
      for (let x = 0; x < 8; x++)
        part.pixels.push(im.pixels[(sy * 8 + y) * im.w + sx * 8 + x] ?? -1);
    draw(a, part, z, ox, oy);
  }
  function scene(a, z) {
    const art = sceneCanvas(a);
    ctx.drawImage(art, 0, 0, a.w * z, a.h * z);
    if ($("screen-preview").checked) {
      ctx.strokeStyle = "#fff";
      ctx.lineWidth = 2;
      ctx.strokeRect(1, 1, 160 * z - 2, 144 * z - 2);
    }
  }
  function renderSelection(z) {
    const rects = selectionMove
      ? selections.map(r => ({ ...r, x: r.x + selectionMove.delta.x, y: r.y + selectionMove.delta.y }))
      : selectionPreview
      ? [...selections, selectionPreview]
      : selections;
    if (!rects.length) return;
    ctx.save();
    ctx.setLineDash([4, 3]);
    ctx.lineWidth = 2;
    ctx.strokeStyle = "#fff";
    for (const r of rects)
      ctx.strokeRect(
        r.x * z + 1,
        r.y * z + 1,
        Math.max(1, r.w * z - 2),
        Math.max(1, r.h * z - 2)
      );
    ctx.restore();
  }
  let cachedScene = null, selectionContext = null;
  function preserveLegacyScenePalettes(p) {
    for (const s of p.scenes) {
      s.bgPaletteId ||= p.palettes[0].id;
      for (const cell of Object.values(s.tiles))
        cell.paletteId ||= p.palettes[0].id;
    }
  }
  function sceneCanvas(a) {
    if (cachedScene?.asset === a) return cachedScene.canvas;
    const canvas = document.createElement("canvas");
    canvas.width = a.w;
    canvas.height = a.h;
    canvas
      .getContext("2d")
      .putImageData(new ImageData(scenePixels(project, a), a.w, a.h), 0, 0);
    cachedScene = { asset: a, canvas };
    return canvas;
  }
  function render(refresh = true) {
    if (refresh) cachedScene = null;
    const asset = current();
    if(mode==="sprite")frameIndex=Math.max(0,Math.min(frameIndex,asset.frames.length-1));
    const context = `${paletteMode}:${mode}:${assetId}:${frameIndex}:${layer}:${asset.w}:${asset.h}`;
    if (context !== selectionContext) { resetSelection(); selectionContext = context; }
    const a = selectionMove?.preview || current(),
      z = Number($("zoom-label").dataset.zoom || 8),
      w = a.w,
      h = a.h;
    if(view.width!==w*z)view.width=w*z;
    if(view.height!==h*z)view.height=h*z;
    ctx.imageSmoothingEnabled = false;
    ctx.clearRect(0, 0, view.width, view.height);
    if (mode === "scene") scene(a, z);
    else {
      ctx.fillStyle = "#111114";
      ctx.fillRect(0, 0, w * z, h * z);
      const shown = mode === "sprite" ? a.frames[timer ? playFrame : frameIndex] : a.image;
      if (mode === "sprite" && $("onion").checked && a.frames[frameIndex - 1])
        draw(a, a.frames[frameIndex - 1], z, 0, 0, 0.3);
      draw(a, shown, z);
      for (let x = 8; x < w; x += 8) {
        ctx.strokeStyle = "rgba(255,255,255,.14)";
        ctx.beginPath();
        ctx.moveTo(x * z, 0);
        ctx.lineTo(x * z, h * z);
        ctx.stroke();
      }
      for (let y = 8; y < h; y += 8) {
        ctx.beginPath();
        ctx.moveTo(0, y * z);
        ctx.lineTo(w * z, y * z);
        ctx.stroke();
      }
    }
    const cellCursor=mode==="scene" && (layer==="tiles" || tool==="recolor");
    if (hover && tool !== "select" && !pan && !spacePan) {
      const brush=brushBounds(hover.pixelX,hover.pixelY);
      const bounds=brush || {x:cellCursor?hover.x*8:hover.pixelX,y:cellCursor?hover.y*8:hover.pixelY,w:cellCursor?8:1,h:cellCursor?8:1};
      ctx.strokeStyle = "#ebedef";
      ctx.lineWidth=1;
      ctx.strokeRect(
        bounds.x*z+.5,bounds.y*z+.5,
        Math.max(1,bounds.w*z-1),Math.max(1,bounds.h*z-1)
      );
    }
    view.style.cursor = pan || selectionMove ? "grabbing" : spacePan ? "grab" : tool === "select" && hover &&
      selections.some(r => hover.pixelX >= r.x && hover.pixelY >= r.y && hover.pixelX < r.x + r.w && hover.pixelY < r.y + r.h)
      ? "grab" : "crosshair";
    renderSelection(z);
    renderPattern();
    if (refresh) ui();
  }
  function ui() {
    const a = current();
    document.querySelector(".app").dataset.workspace = paletteMode
      ? "palette"
      : mode;
    $("art-workspace").hidden = paletteMode;
    $("palette-workspace").hidden = !paletteMode;
    $("brush-palette-label").textContent = paletteMode
      ? "Palette"
      : "Brush palette";
    selectedPalette = Math.min(selectedPalette, paletteList().length - 1);
    for (const n of ["draw", "fill", "pick", "select", "recolor"]) {
      $("tool-" + n).classList.toggle("on", tool === n);
      $("tool-" + n).setAttribute("aria-pressed", String(tool === n));
    }
    $("erase").classList.toggle("on", tool === "erase");
    $("tool-recolor").hidden = mode !== "scene";
    $("tool-draw").title =
      mode === "scene" && layer === "tiles" ? "Stamp tiles" : "Brush";
    $("tool-recolor").title =
      layer === "tiles" ? "Recolor tiles" : "Recolor pixels";
    $("tool-fill").disabled = mode === "scene" && layer === "tiles";
    $("selection-reflect").hidden = paletteMode || tool !== "select";
    for (const direction of ["horizontal","vertical","left-right","right-left","top-bottom","bottom-top"])
      $("reflect-"+direction).disabled = readonly || !selections.length || !!selectionMove || !!selectionStart;
    $("play").hidden = mode !== "sprite";
    $("file-import").disabled=readonly || paletteMode;
    $("onion").closest("label").hidden = mode !== "sprite";
    $("kind").closest("label").hidden = true;
    $("layer-tiles").classList.toggle("on", layer === "tiles");
    $("layer-pixels").classList.toggle("on", layer === "pixels");
    $("brush-help").textContent =
      tool === "select"
        ? "Drag to select. Shift-drag inside to move; outside to add. Flip mirrors the whole selection; Mirror clone copies one half. Delete clears; Esc deselects."
        : mode === "scene"
        ? tool === "recolor"
          ? "Recolor: drag across existing 8×8 cells. Pixels and rotation stay linked."
          : layer === "tiles"
          ? "Stamp: choose a tile and brush palette. Each placement keeps its own palette."
          : "Paint transparent details above tiles. Each 8×8 detail cell keeps its palette."
        : "Choose a color, then draw on the canvas.";
    $("asset-list").replaceChildren(
      ...assets().map((x) => {
        const o = document.createElement("option");
        o.value = x.id;
        o.textContent = x.name;
        return o;
      })
    );
    $("asset-list").value = assetId;
    $("asset-name").value = a.name;
    $("scene-controls").hidden = paletteMode || mode !== "scene";
    $("tile-library").hidden = paletteMode || mode !== "scene";
    $("tile-library-title").hidden = paletteMode || mode !== "scene";
    $("scene-size-label").hidden = mode !== "scene";
    $("screen-preview-label").hidden = mode !== "scene";
    $("fps-label").hidden = mode !== "sprite";
    $("frames").hidden = mode !== "sprite";
    $("clone").hidden = mode !== "sprite";
    $("blank-frame").hidden = mode !== "sprite";
    $("drop-frame").hidden = mode !== "sprite";
    $("asset-settings-title").textContent =
      mode === "sprite" ? "Sprite set" : mode === "tile" ? "Tile" : "Scene";
    $("width").value = a.w;
    $("height").value = a.h;
    const preset = `${a.w}x${a.h}`;
    $("size").value = [...$("size").options].some((o) => o.value === preset)
      ? preset
      : "custom";
    $("fps").value = a.fps || 8;
    renderSwatches();
    if (mode === "sprite") frames(a);
    if (mode === "scene") tiles();
    status();
    syncControls();
  }
  function status() {
    $("status").textContent = readonly
      ? "Read-only: another tab owns this project."
      : saving
      ? "Saving…"
      : `${mode} · ${current().name} · ${current().w}×${current().h} · Saved`;
  }
  function renderSwatches() {
    renderPalettes();
    refreshPalettePresets();
    syncHex();
    const box = $("swatches");
    box.replaceChildren();
    for (let i = 0; i < 4; i++) {
      const b = document.createElement("button");
      b.className = i === color ? "on" : "";
      b.title = "Color " + i;
      b.style.background = `rgb(${paletteList()
        [selectedPalette].colors[i].map((v) => Math.round((v * 255) / 31))
        .join(",")})`;
      b.onclick = () => {
        color = i;
        render();
      };
      box.appendChild(b);
    }
  }
  function frames(a) {
    const b = $("frames");
    b.replaceChildren();
    a.frames.forEach((_, i) => {
      const q = document.createElement("button");
      q.textContent = "Frame " + (i + 1);
      q.className = i === frameIndex ? "on" : "";
      q.onclick = () => {
        frameIndex = i;
        render();
      };
      b.appendChild(q);
    });
    $("play").textContent = timer ? "Stop" : "Play";
  }
  function tiles() {
    const b = $("tile-library");
    b.replaceChildren();
    project.tiles.forEach((t) => {
      const q = document.createElement("button");
      q.textContent = `${t.name} (${t.w}×${t.h})`;
      q.className = t.id === selectedTileId ? "on" : "";
      q.onclick = () => {
        selectedTileId = t.id;
        layer = "tiles";
        render();
      };
      b.appendChild(q);
    });
    const t =
      project.tiles.find((t) => t.id === selectedTileId) || project.tiles[0];
    if (t) {
      selectedTileId = t.id;
      $(
        "selected-tile"
      ).textContent = `Selected tile: ${t.name} (${t.w}×${t.h})`;
    }
  }
  function renderPattern() {
    const wrap = $("pattern-wrap"),
      pc = $("pattern-view"),
      pctx = pc && pc.getContext("2d");
    if (!pctx) return;
    const visible = mode === "tile" && !paletteMode;
    $("tile-previews").hidden = wrap.hidden = !visible;
    if (!visible) { stopPalettePlayback(); return; }
    const previewAsset = `${project.id}:${assetId}`;
    if (paletteAsset !== previewAsset) {
      stopPalettePlayback();paletteFrame=0;paletteAsset=previewAsset;paletteUIKey=null;
    }
    const t = selectionMove?.preview || current(),
      z = Math.max(1, Math.floor(160 / t.w));
    pc.width = 160;
    pc.height = 144;
    pctx.imageSmoothingEnabled = false;
    pctx.fillStyle = "#111114";
    pctx.fillRect(0, 0, 160, 144);
    for (let y = 0; y < 144; y += t.h)
      for (let x = 0; x < 160; x += t.w) {
        for (let py = 0; py < t.h; py++)
          for (let px = 0; px < t.w; px++) {
            const v = t.image.pixels[py * t.w + px];
            if (v < 0) continue;
            pctx.fillStyle = `rgb(${rgb(
              t,
              t.image.tilePal[
                Math.floor(py / 8) * (t.w / 8) + Math.floor(px / 8)
              ] || 0,
              false,
              v
            ).join(",")})`;
            pctx.fillRect(x + px, y + py, 1, 1);
          }
      }
    renderPalettePreview();
  }

  function stopPalettePlayback() {
    if (paletteTimer) clearTimeout(paletteTimer);
    paletteTimer=null;
  }
  function editPalettePreview(fn) {
    if (readonly || mode!=="tile" || paletteMode) return;
    stopPalettePlayback();
    changed(() => {
      const tile=current();
      tile.paletteAnimation=paletteAnimation(tile,project);
      fn(tile.paletteAnimation);
    });
  }
  function renderPalettePreview() {
    if(mode!=="tile" || paletteMode) return;
    const tile=selectionMove?.preview || current();
    const config=paletteAnimation(tile,project);
    if(config.frames.length<2 && paletteTimer) stopPalettePlayback();
    paletteFrame=Math.max(0,Math.min(paletteFrame,config.frames.length-1));
    const frame=config.frames[paletteFrame];
    const palette=project.palettes.find(p=>p.id===frame.paletteId) || project.palettes[0];
    const source=document.createElement("canvas");
    source.width=tile.w;source.height=tile.h;
    source.getContext("2d").putImageData(new ImageData(palettePreviewPixels(tile,palette),tile.w,tile.h),0,0);
    const canvas=$("palette-animation-view"),cx=canvas.getContext("2d");
    cx.imageSmoothingEnabled=false;
    cx.fillStyle="#111114";cx.fillRect(0,0,160,144);
    cx.fillStyle=cx.createPattern(source,"repeat");cx.fillRect(0,0,160,144);
    canvas.dataset.frame=String(paletteFrame);
    canvas.dataset.paletteId=palette.id;
    $("palette-frame-status").textContent=`Frame ${paletteFrame+1} / ${config.frames.length}`;
    $("palette-play").textContent=paletteTimer?"Stop":"Play";
    $("palette-play").setAttribute("aria-label",paletteTimer?"Stop palette animation":"Play palette animation");
    $("palette-play").setAttribute("aria-pressed",String(!!paletteTimer));
    $("palette-play").disabled=config.frames.length<2;
    if(!document.activeElement?.closest("#palette-fps-control")) $("palette-fps").value=config.fps;
    $("palette-fps").disabled=readonly;
    $("palette-clone").disabled=readonly;
    $("palette-drop").disabled=readonly || config.frames.length<2;
    const key=JSON.stringify([paletteAsset,config,project.palettes,readonly]);
    if(key!==paletteUIKey) {
      paletteUIKey=key;
      const rows=config.frames.map((entry,index)=>{
        const row=document.createElement("div");row.className="palette-frame-row";
        const selectFrame=document.createElement("button");
        selectFrame.id=`palette-frame-${index}`;
        selectFrame.type="button";selectFrame.textContent=`Frame ${index+1}`;
        selectFrame.setAttribute("aria-label",`Select palette frame ${index+1}`);
        selectFrame.onclick=()=>{stopPalettePlayback();paletteFrame=index;renderPalettePreview();syncControls();};
        const select=document.createElement("select");select.id=`palette-frame-palette-${index}`;
        select.setAttribute("aria-label",`Palette for frame ${index+1}`);
        select.replaceChildren(...project.palettes.map(p=>new Option(p.name,p.id)));
        select.value=entry.paletteId;select.disabled=readonly;
        select.onchange=e=>editPalettePreview(animation=>{animation.frames[index].paletteId=e.target.value;paletteFrame=index;});
        const swatches=document.createElement("span");swatches.className="palette-frame-swatches";
        swatches.setAttribute("aria-hidden","true");
        const colors=(project.palettes.find(p=>p.id===entry.paletteId)||palette).colors;
        for(const color of colors) {
          const chip=document.createElement("span");
          chip.style.background=`rgb(${color.map(v=>Math.round(v*255/31)).join(",")})`;
          swatches.append(chip);
        }
        row.append(selectFrame,select,swatches);return row;
      });
      $("palette-frames").replaceChildren(...rows);
    }
    config.frames.forEach((_,index)=>{
      const button=$(`palette-frame-${index}`);
      button.classList.toggle("on",index===paletteFrame);
      button.setAttribute("aria-pressed",String(index===paletteFrame));
    });
  }
  function startPalettePlayback() {
    if(mode!=="tile" || paletteMode) return;
    const config=paletteAnimation(current(),project);
    if(config.frames.length<2) return;
    stopPalettePlayback();
    const schedule=()=>{
      paletteTimer=setTimeout(()=>{
        if(mode!=="tile" || paletteMode || paletteAsset!==`${project.id}:${assetId}`) {stopPalettePlayback();return;}
        paletteFrame=(paletteFrame+1)%paletteAnimation(current(),project).frames.length;
        renderPalettePreview();syncControls();
        if(paletteTimer) schedule();
      },1000/paletteAnimation(current(),project).fps);
    };
    schedule();
    renderPalettePreview();syncControls();
  }
  $("palette-play").onclick=()=>{
    if(paletteTimer) {stopPalettePlayback();renderPalettePreview();syncControls();}
    else startPalettePlayback();
  };
  $("palette-clone").onclick=()=>editPalettePreview(config=>{
    config.frames.splice(paletteFrame+1,0,{...config.frames[paletteFrame],id:uid("palette-frame")});
    paletteFrame++;
  });
  $("palette-drop").onclick=()=>{
    if(mode!=="tile" || paletteAnimation(current(),project).frames.length<2) return;
    editPalettePreview(config=>{config.frames.splice(paletteFrame,1);paletteFrame=Math.min(paletteFrame,config.frames.length-1);});
  };
  $("palette-fps").onchange=e=>{
    const playing=!!paletteTimer;
    const fps=Math.max(1,Math.min(30,Math.round(Number(e.target.value)||2)));
    editPalettePreview(config=>{config.fps=fps;});
    if(playing)startPalettePlayback();
  };
  const patternCanvas = $("pattern-view");
  let patternDrag = false;
  function patternPoint(e) {
    const t = current(),
      r = patternCanvas.getBoundingClientRect();
    return {
      x: Math.floor(((e.clientX - r.left) * 160) / r.width),
      y: Math.floor(((e.clientY - r.top) * 144) / r.height),
    };
  }
  patternCanvas.onpointerdown = (e) => {
    if (readonly || mode !== "tile" || tool === "select" || e.button!==0 || activePointer || e.isPrimary===false) return;
    beginGesture(e,patternCanvas);
    patternDrag = true;
    drag=true;changed.stroke=false;last=null;strokeTo(e,true);
  };
  patternCanvas.onpointermove = (e) => {
    if (!patternDrag || mode !== "tile") return;
    e.preventDefault();strokeTo(e,true);
  };
  patternCanvas.onpointerup = e=>{if(patternDrag)strokeTo(e,true);finishGesture(e);};
  patternCanvas.onpointercancel=patternCanvas.onlostpointercapture=finishGesture;
  function point(e) {
    const r = view.getBoundingClientRect(),
      z = Number($("zoom-label").dataset.zoom || 8);
    return {
      x: Math.floor((e.clientX - r.left) / z),
      y: Math.floor((e.clientY - r.top) / z),
    };
  }
  function beginGesture(e,node) {
    e.preventDefault();node.setPointerCapture(e.pointerId);
    activePointer={node,id:e.pointerId};strokeSeen=new Set();
    document.body.classList.add("canvas-gesture");
  }
  function finishGesture(e) {
    if(!activePointer || (e?.pointerId!==undefined && e.pointerId!==activePointer.id))return;
    const captured=activePointer;activePointer=null;
    pan=null;patternDrag=false;drag=false;changed.stroke=false;last=null;
    selectionMove=selectionPreview=selectionStart=null;
    document.body.classList.remove("canvas-gesture");
    if(captured.node.hasPointerCapture(captured.id))captured.node.releasePointerCapture(captured.id);
    render();
  }
  function strokeTo(e,pattern=false) {
    const a=current(),width=pattern?160:a.w,height=pattern?144:a.h;
    const samples=e.getCoalescedEvents?.() || [];
    const mutations=[];paintBatch=mutations;
    try {
      for(const sample of [...samples,e]) {
        const p=pattern?patternPoint(sample):point(sample);
        if(["draw","erase","recolor"].includes(tool)) {
          for(const q of rasterLine(last || p,p,width,height)) {
            let x=pattern?q.x%a.w:q.x,y=pattern?q.y%a.h:q.y;
            const brush=brushBounds(x,y);
            if(brush) {x=brush.x;y=brush.y;}
            const cell=mode==="scene" && (layer==="tiles" || tool==="recolor");
            const key=cell?`${x>>3},${y>>3}`:`${x},${y}`;
            if(!strokeSeen.has(key)) {strokeSeen.add(key);edit(x,y);}
          }
        } else if(!last)edit(pattern?p.x%a.w:p.x,pattern?p.y%a.h:p.y);
        last=p;
      }
    } finally {paintBatch=null;}
    if(mutations.length)changed(()=>{for(const mutate of mutations)mutate();});
  }
  // A trackpad may emit wheel events while its button is held. Painting must
  // never pan a scroller or chain into the embedding app, even outside capture.
  document.addEventListener("wheel",e=>{
    if(activePointer) {e.preventDefault();e.stopPropagation();}
  },{capture:true,passive:false});
  const viewport=$("viewport");
  viewport.addEventListener("wheel",e=>{
    if(activePointer || e.ctrlKey)return;
    e.preventDefault();e.stopPropagation();
    const unit=e.deltaMode===1?16:e.deltaMode===2?viewport.clientHeight:1;
    viewport.scrollLeft+=(e.shiftKey && !e.deltaX?e.deltaY:e.deltaX)*unit;
    viewport.scrollTop+=(e.shiftKey && !e.deltaX?0:e.deltaY)*unit;
  },{passive:false});
  window.addEventListener("blur",()=>{spacePan=false;finishGesture();});
  function flood(im, x, y, value, onPaint = () => {}) {
    const old = im.pixels[y * im.w + x];
    if (old === value) return;
    const q = [[x, y]],
      seen = new Set();
    while (q.length) {
      const [px, py] = q.pop(),
        k = px + "," + py;
      if (
        seen.has(k) ||
        px < 0 ||
        py < 0 ||
        px >= im.w ||
        py >= im.h ||
        im.pixels[py * im.w + px] !== old
      )
        continue;
      seen.add(k);
      im.pixels[py * im.w + px] = value;
      onPaint(px, py);
      q.push([px + 1, py], [px - 1, py], [px, py + 1], [px, py - 1]);
    }
  }
  function stamp(a, x, y) {
    const t = project.tiles.find((t) => t.id === selectedTileId);
    if (!t) return;
    changed(() =>
      stampCells(a, t, x, y, rotation, paletteList()[selectedPalette].id)
    );
  }
  function brushBounds(x,y) {
    if(mode!=="scene" || layer!=="tiles" || !["draw","fill"].includes(tool))return null;
    const tile=project.tiles.find(t=>t.id===selectedTileId);
    return tile?tileBrushBounds(tile,rotation,x,y):null;
  }
  function edit(x, y) {
    const a = current();
    if (x < 0 || y < 0 || x >= a.w || y >= a.h) return;
    if (mode === "scene" && layer === "tiles") {
      const k = `${Math.floor(x / 8)},${Math.floor(y / 8)}`;
      if (tool === "erase") changed(() => delete a.tiles[k]);
      else if (tool === "recolor") {
        const cell = a.tiles[k],
          id = paletteList()[selectedPalette].id;
        if (cell && cell.paletteId !== id)
          changed(() => recolorCell(a, x, y, id));
      } else if (tool === "pick") {
        const cell = a.tiles[k];
        if (cell) {
          selectedTileId = cell.tileId;
          selectedPalette = Math.max(
            0,
            project.palettes.findIndex((p) => p.id === cell.paletteId)
          );
          render();
        }
      } else {
        const brush=brushBounds(x,y);
        stamp(a,brush?.x ?? x,brush?.y ?? y);
      }
      return;
    }
    const im =
      mode === "scene" ? { w: a.w, h: a.h, pixels: a.pixels } : image();
    if (!im) return;
    if (tool === "pick") {
      color = Math.max(0, im.pixels[y * im.w + x]);
      render();
      return;
    }
    changed(() => {
      const cell = `${Math.floor(x / 8)},${Math.floor(y / 8)}`;
      if (tool === "recolor") {
        if (mode === "scene")
          (a.pixelPalettes ||= {})[cell] = paletteList()[selectedPalette].id;
        return;
      }
      const applyPalette = (px, py) => {
        const id = paletteList()[selectedPalette].id;
        if (mode === "scene") {
          (a.pixelPalettes ||= {})[
            `${Math.floor(px / 8)},${Math.floor(py / 8)}`
          ] = id;
          return;
        }
        if (!a.palettes.includes(id)) a.palettes.push(id);
        im.tilePal[
          Math.floor(py / 8) * (im.w / 8) + Math.floor(px / 8)
        ] = a.palettes.indexOf(id);
      };
      if (tool === "fill") flood(im, x, y, color, applyPalette);
      else {
        im.pixels[y * im.w + x] = tool === "erase" ? -1 : color;
        if (tool !== "erase") applyPalette(x, y);
      }
    });
  }
  function selectionRect(a, b) {
    const x = Math.max(0, Math.min(a.x, b.x)),
      y = Math.max(0, Math.min(a.y, b.y)),
      right = Math.min(current().w, Math.max(a.x, b.x) + 1),
      bottom = Math.min(current().h, Math.max(a.y, b.y) + 1);
    if (mode === "scene" && layer === "tiles") {
      const sx = Math.floor(x / 8) * 8, sy = Math.floor(y / 8) * 8;
      return { x: sx, y: sy, w: Math.ceil(right / 8) * 8 - sx, h: Math.ceil(bottom / 8) * 8 - sy };
    }
    return { x, y, w: Math.max(1, right - x), h: Math.max(1, bottom - y) };
  }
  function resetSelection() {
    selections = [];
    selectionStart = selectionPreview = selectionMove = null;
  }
  function selectionArtwork(source, delta = null) {
    const a = cp(source);
    if (mode === "scene" && layer === "tiles")
      a.tiles = editSelectedCells(source, selections, delta);
    else {
      const im = mode === "sprite" ? a.frames[frameIndex] : mode === "tile" ? a.image : a;
      im.pixels = editSelectedPixels(im, selections, mode === "tile" ? 0 : -1, delta);
    }
    return a;
  }
  function clearSelectionPixels() {
    if (readonly || paletteMode || !selections.length || selectionMove || selectionStart) return;
    const next = selectionArtwork(current());
    changed(() => Object.assign(current(), next));
  }
  for (const direction of ["horizontal","vertical","left-right","right-left","top-bottom","bottom-top"])
    $("reflect-"+direction).onclick = () => {
      if (readonly || paletteMode || !selections.length || selectionMove || selectionStart) return;
      changed(() => {
        const a=current();
        if (mode==="scene" && layer==="tiles") {
          a.tiles=reflectSelectedCells(a,selections,direction);
        } else {
          const im=mode==="sprite"?a.frames[frameIndex]:mode==="tile"?a.image:a;
          const palettes=mode==="scene"?{...a.pixelPalettes}:im.tilePal?.slice();
          im.pixels=reflectSelectedPixels(im,selections,direction);
          for (const [s,d] of reflectedPaletteCells(im.w,im.h,selections,direction)) {
            if (mode==="scene") (a.pixelPalettes ||= {})[d]=palettes[s] || a.bgPaletteId;
            else if (palettes) {
              const index=k=>{const [x,y]=k.split(",").map(Number);return y*(im.w/8)+x;};
              im.tilePal[index(d)]=palettes[index(s)] || 0;
            }
          }
        }
      });
    };
  view.onpointerdown = (e) => {
    if (activePointer || e.isPrimary===false || (e.button!==0 && e.button!==1)) return;
    if(spacePan || e.button===1) {
      beginGesture(e,view);pan={x:e.clientX,y:e.clientY,left:viewport.scrollLeft,top:viewport.scrollTop};
      render(false);return;
    }
    if(readonly)return;
    beginGesture(e,view);
    view.focus({ preventScroll: true });
    const p = point(e);
    if (tool === "select") {
      if (timer) { clearInterval(timer); timer = null; }
      if (selections.some(r => p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h)) {
        selectionMove = { start: p, source: cp(current()), delta: { x: 0, y: 0 }, preview: null };
        render();
        return;
      }
      if (!e.shiftKey) selections = [];
      selectionStart = p;
      selectionPreview = { x: p.x, y: p.y, w: 1, h: 1 };
      render();
      return;
    }
    drag = true;
    changed.stroke = false;
    last = null;
    strokeTo(e);
  };
  view.onpointermove = (e) => {
    if(activePointer) {
      e.preventDefault();
      if(pan) {
        viewport.scrollLeft=pan.left+pan.x-e.clientX;viewport.scrollTop=pan.top+pan.y-e.clientY;
        return;
      }
    }
    const p = point(e);
    hover = { x: Math.floor(p.x / 8), y: Math.floor(p.y / 8), pixelX: p.x, pixelY: p.y };
    if (selectionMove) {
      const a = current();
      selectionMove.delta = selectionDelta(selections, a.w, a.h,
        p.x - selectionMove.start.x, p.y - selectionMove.start.y,
        mode === "scene" && layer === "tiles" ? 8 : 1);
      selectionMove.preview = selectionArtwork(selectionMove.source, selectionMove.delta);
      render(false);
      return;
    }
    if (tool === "select" && selectionStart) {
      selectionPreview = selectionRect(selectionStart, p);
      render();
      return;
    }
    if (drag && ["draw", "recolor", "erase"].includes(tool)) strokeTo(e);
    render(false);
  };
  view.onpointerup = (e) => {
    if(!activePointer || e.pointerId!==activePointer.id)return;
    if(drag)strokeTo(e);
    if (selectionMove) {
      const { preview, delta } = selectionMove;
      selectionMove = null;
      if (preview && (delta.x || delta.y)) {
        selections = selections.map(r => ({ ...r, x: r.x + delta.x, y: r.y + delta.y }));
        changed(() => Object.assign(current(), preview));
      } else render();
    }
    if (tool === "select" && selectionPreview) {
      selections.push(selectionPreview);
      selectionPreview = null;
      selectionStart = null;
      render();
    }
    finishGesture(e);
  };
  view.onpointercancel = view.onlostpointercapture = finishGesture;
  view.onpointerleave=()=>{if(!activePointer){hover=null;render(false);}};
  view.tabIndex = 0;
  function add() {
    const p = project.palettes[2] || project.palettes[0];
    let a;
    if (mode === "sprite")
      a = {
        id: uid("sprite"),
        name: "New sprite",
        w: 32,
        h: 32,
        fps: 8,
        palettes: [p.id],
        frames: [{ id: uid("frame"), ...blank(32, 32, 0, true) }],
      };
    else if (mode === "tile")
      a = {
        id: uid("tile"),
        name: "New tile",
        w: 8,
        h: 8,
        palettes: [p.id],
        image: blank(8, 8),
      };
    else
      a = {
        id: uid("scene"),
        name: "New scene",
        w: 160,
        h: 144,
        bg: 0,
        tiles: {},
        pixels: new Array(160 * 144).fill(-1),
      };
    assets().push(a);
    assetId = a.id;
    frameIndex=playFrame=0;
    if(timer) {clearInterval(timer);timer=null;}
    save();
    render();
  }
  document.querySelectorAll("[data-mode]").forEach(
    (b) =>
      (b.onclick = () => {
        paletteMode = b.dataset.mode === "palette";
        if (paletteMode) {
          if (timer) {
            clearInterval(timer);
            timer = null;
          }
          render();
          return;
        }
        mode = b.dataset.mode;
        if (timer) {
          clearInterval(timer);
          timer = null;
        }
        layer = mode === "scene" ? "tiles" : "pixels";
        tool = "draw";
        selections = [];
        assetId = assets()[0].id;
        frameIndex = 0;
        render();
      })
  );
  $("asset-add").onclick = add;
  $("asset-list").onchange = (e) => {
    resetSelection();
    assetId = e.target.value;
    frameIndex = 0;
    render();
  };
  $("asset-name").oninput = (e) => {
    current().name = e.target.value;
    clearTimeout(changed.timer);
    changed.timer = setTimeout(save, 300);
  };
  $("asset-dup").onclick = () => {
    const a = cp(current());
    a.id = uid(mode);
    a.name += " copy";
    if (mode === "sprite")
      a.frames = a.frames.map((f) => ({ ...f, id: uid("frame") }));
    assets().push(a);
    assetId = a.id;
    save();
    render();
  };
  $("asset-delete").onclick = () => {
    if (assets().length < 2) return;
    if (
      mode === "tile" &&
      project.scenes.some((s) =>
        Object.values(s.tiles).some((c) => c.tileId === assetId)
      )
    ) {
      alert("This tile is still used by a scene.");
      return;
    }
    assets().splice(
      assets().findIndex((a) => a.id === assetId),
      1
    );
    assetId = assets()[0].id;
    frameIndex=playFrame=0;
    save();
    render();
  };
  ["draw", "fill", "pick", "recolor"].forEach(
    (n) =>
      ($("tool-" + n).onclick = () => {
        tool = n;
        render();
      })
  );
  $("erase").onclick = () => {
    tool = tool === "erase" ? "draw" : "erase";
    render();
  };
  function setLayer(next) {
    resetSelection();
    layer = next;
    $("layer-tiles").classList.toggle("on", next === "tiles");
    $("layer-pixels").classList.toggle("on", next === "pixels");
    render();
  }
  $("layer-tiles").onclick = () => setLayer("tiles");
  $("layer-pixels").onclick = () => setLayer("pixels");
  $("scene-erase").onclick = () => {
    tool = "erase";
    setLayer("tiles");
  };
  [0, 90, 180, 270].forEach(
    (r) =>
      ($("rotate-" + r).onclick = () => {
        rotation = r;
        document
          .querySelectorAll("[id^=rotate-]")
          .forEach((x) => x.classList.remove("on"));
        $("rotate-" + r).classList.add("on");
      })
  );
  $("clone").onclick = () => {
    const a = current();
    changed(() => {
      a.frames.splice(frameIndex + 1, 0, {
        id: uid("frame"),
        ...cp(a.frames[frameIndex]),
      });
      frameIndex++;
    });
  };
  $("blank-frame").onclick = () => {
    const a = current();
    changed(() => {
      a.frames.splice(frameIndex + 1, 0, {
        id: uid("frame"),
        ...blank(a.w, a.h, 0, true),
      });
      frameIndex++;
    });
  };
  $("drop-frame").onclick = () => {
    const a = current();
    if (a.frames.length > 1)
      changed(() => {
        a.frames.splice(frameIndex, 1);
        frameIndex = Math.min(frameIndex, a.frames.length - 1);
      });
  };
  $("play").onclick = () => {
    if (timer) {
      clearInterval(timer);
      timer = null;
      render();
      return;
    }
    playFrame = frameIndex;
    timer = setInterval(() => {
      playFrame = (playFrame + 1) % current().frames.length;
      render();
    }, 1000 / (current().fps || 8));
    render();
  };
  document.addEventListener("visibilitychange", () => {
    if(document.hidden && paletteTimer) {stopPalettePlayback();renderPalettePreview();syncControls();}
    if (document.hidden && timer) {
      clearInterval(timer);
      timer = null;
      render();
    }
  });
  function resize(w, h) {
    const a = current();
    if (w < 8 || h < 8 || w > 1024 || h > 1024 || w % 8 || h % 8) {
      alert("Dimensions must be multiples of 8, from 8 to 1024.");
      return;
    }
    changed(() => {
      if (mode === "sprite") {
        a.w = w;
        a.h = h;
        a.frames.forEach((f) => resizeImage(f, w, h, true));
      } else if (mode === "tile") {
        a.w = w;
        a.h = h;
        resizeImage(a.image, w, h, false);
      } else {
        const old = a.pixels,
          ow = a.w;
        a.w = w;
        a.h = h;
        a.pixels = new Array(w * h).fill(-1);
        for (let y = 0; y < Math.min(h, Math.floor(old.length / ow)); y++)
          for (let x = 0; x < Math.min(w, ow); x++)
            a.pixels[y * w + x] = old[y * ow + x];
      }
    });
  }
  function resizeImage(im, w, h, t) {
    const old = im.pixels,
      ow = im.w,
      oh = im.h;
    im.w = w;
    im.h = h;
    im.pixels = new Array(w * h).fill(t ? -1 : 0);
    for (let y = 0; y < Math.min(h, oh); y++)
      for (let x = 0; x < Math.min(w, ow); x++)
        im.pixels[y * w + x] = old[y * ow + x];
    im.tilePal = new Array((w * h) / 64).fill(0);
  }
  $("size").onchange = (e) => {
    if (e.target.value !== "custom") {
      const [w, h] = e.target.value.split("x").map(Number);
      resize(w, h);
    }
  };
  $("width").onchange = (e) => resize(+e.target.value, current().h);
  $("height").onchange = (e) => resize(current().w, +e.target.value);
  $("fps").onchange = (e) => {
    current().fps = Math.max(1, Math.min(30, +e.target.value));
    save();
  };
  $("scene-size").onchange = (e) => {
    $("scene-size-custom").hidden = e.target.value !== "custom";
    if (e.target.value !== "custom") {
      const [w, h] = e.target.value.split("x").map(Number);
      resize(w, h);
    }
    render();
  };
  $("scene-width").onchange = (e) => resize(+e.target.value, current().h);
  $("scene-height").onchange = (e) => resize(current().w, +e.target.value);
  $("zoom-in").onclick = () => {
    $("tile-scale").value="custom";
    $("zoom-label").dataset.zoom = Math.min(
      24,
      +($("zoom-label").dataset.zoom || 8) + 2
    );
    $("zoom-label").textContent = $("zoom-label").dataset.zoom + "×";
    render();
  };
  $("zoom-out").onclick = () => {
    $("tile-scale").value="custom";
    $("zoom-label").dataset.zoom = Math.max(
      2,
      +($("zoom-label").dataset.zoom || 8) - 2
    );
    $("zoom-label").textContent = $("zoom-label").dataset.zoom + "×";
    render();
  };
  $("undo").onclick = () => {
    const k = mode + ":" + assetId;
    if (!hist.get(k)?.length) return;
    redo.set(k, [...(redo.get(k) || []), cp(project)]);
    project = hist.get(k).pop();
    render();
    save();
  };
  $("redo").onclick = () => {
    const k = mode + ":" + assetId;
    if (!redo.get(k)?.length) return;
    hist.set(k, [...(hist.get(k) || []), cp(project)]);
    project = redo.get(k).pop();
    render();
    save();
  };
  document.querySelectorAll("[data-mode]").forEach((b) =>
    b.addEventListener("click", () => {
      document
        .querySelectorAll("[data-mode]")
        .forEach((x) => x.classList.toggle("on", x === b));
      $("tile-scale-label").hidden = false;
      $("tile-library-title").hidden = b.dataset.mode !== "scene";
      render();
      applyDrawingScale();
    })
  );
  function fitCanvas() {
    if(paletteMode || $("tile-scale").value!=="fit" || activePointer)return;
    const a=current(),z=Math.max(1,Math.min(24,Math.floor(Math.min((viewport.clientWidth-2)/a.w,(viewport.clientHeight-2)/a.h))));
    if(Number($("zoom-label").dataset.zoom)===z)return;
    $("zoom-label").dataset.zoom=z;$("zoom-label").textContent=z+"×";
    // Fitting must not rebuild the controls: their asynchronous mounts briefly
    // change available height and would feed that transient size back into Fit.
    viewport.scrollLeft=viewport.scrollTop=0;render(false);
  }
  function applyDrawingScale() {
    if($("tile-scale").value==="fit") {requestAnimationFrame(fitCanvas);return;}
    if($("tile-scale").value==="custom")return;
    $("zoom-label").dataset.zoom = 8 * Number($("tile-scale").value);
    $("zoom-label").textContent = $("zoom-label").dataset.zoom + "×";
    render();
  }
  $("tile-scale").onchange=applyDrawingScale;
  $("fit-view").onclick=()=>{$("tile-scale").value="fit";viewport.scrollLeft=viewport.scrollTop=0;fitCanvas();syncControls();};
  new ResizeObserver(()=>requestAnimationFrame(fitCanvas)).observe(viewport);
  function paletteList() {
    return project.palettes;
  }
  function renderPalettes() {
    const box = $("pal-tabs");
    if (!box) return;
    box.replaceChildren(
      ...paletteList().map((p, i) => {
        const b = document.createElement("button");
        b.textContent = p.name;
        b.className = i === selectedPalette ? "on" : "";
        b.onclick = () => {
          selectedPalette = i;
          render();
        };
        return b;
      })
    );
    const p = paletteList()[selectedPalette] || project.palettes[0];
    $("brush-palette").replaceChildren(
      ...paletteList().map((p) => new Option(p.name, p.id))
    );
    $("brush-palette").value = p.id;
    $("pal-name").value = p.name;
    for (let i = 0; i < 3; i++)
      $("ch-" + "rgb"[i]).value = Math.round((p.colors[color][i] * 255) / 31);
  }
  ["r", "g", "b"].forEach(
    (ch, i) =>
      ($("ch-" + ch).oninput = (e) => {
        const p = paletteList()[selectedPalette];
        if (p) {
          p.colors[color][i] = Math.round(
            (Math.max(0, Math.min(255, +e.target.value)) * 31) / 255
          );
          render();
          clearTimeout(changed.timer);
          changed.timer = setTimeout(save, 300);
        }
      })
  );
  $("pal-name").oninput = (e) => {
    const p = paletteList()[selectedPalette];
    if (p) {
      p.name = e.target.value;
      clearTimeout(changed.timer);
      changed.timer = setTimeout(save, 300);
    }
  };
  $("pal-add").onclick = () => {
    project.palettes.push(
      pal(
        "New palette",
        [
          [0, 0, 0],
          [31, 31, 31],
          [16, 16, 16],
          [8, 8, 8],
        ].map((c) => c.map((v) => v))
      )
    );
    selectedPalette = paletteList().length - 1;
    render();
    save();
  };
  $("pal-dup").onclick = () => {
    const p = cp(paletteList()[selectedPalette]);
    p.id = uid("pal");
    p.name += " copy";
    project.palettes.push(p);
    selectedPalette = paletteList().length - 1;
    render();
    save();
  };
  $("pal-del").onclick = () => {
    const id = paletteList()[selectedPalette]?.id;
    if (
      [...project.tiles, ...project.sprites].some((a) =>
        a.palettes.includes(id) || a.paletteAnimation?.frames?.some(frame=>frame.paletteId===id)
      ) ||
      project.scenes.some(
        (s) =>
          s.bgPaletteId === id ||
          Object.values(s.tiles).some((c) => c.paletteId === id) ||
          Object.values(s.pixelPalettes || {}).includes(id)
      )
    ) {
      alert(
        "This palette is used by artwork or palette-preview frames. Recolor or replace its usages before removing it."
      );
      return;
    }
    if (project.palettes.length > 1) {
      project.palettes.splice(
        project.palettes.indexOf(paletteList()[selectedPalette]),
        1
      );
      selectedPalette = 0;
      render();
      save();
    }
  };
  function download(name, data, type = "application/json") {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([data], { type }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }
  $("file-save").onclick = () =>
    download(project.name + ".gbc-art.json", JSON.stringify(project));
  $("file-new").onclick = async () => {
    if (!readonly && await confirm("Create a new project?")) {
      replaceProject(fresh());
      save();
      render();
    }
  };
  function replaceProject(next) {
    if(!next || ![next.palettes,next.sprites,next.tiles,next.scenes].every(a=>Array.isArray(a) && a.length) ||
       next.sprites.some(a=>!Array.isArray(a.frames) || !a.frames.length))throw new Error("Project needs palettes, sprites, tiles, scenes and at least one sprite frame.");
    if(timer) {clearInterval(timer);timer=null;}
    stopPalettePlayback();clearTimeout(changed.timer);
    project=next;mode="sprite";paletteMode=false;frameIndex=playFrame=0;
    selectedPalette=0;selectedTileId=project.tiles[0].id;assetId=project.sprites[0].id;
    hist.clear();redo.clear();resetSelection();hover=null;layer="pixels";tool="draw";
    window.dispatchEvent(new CustomEvent("workspace-mode-change",{detail:"sprite"}));
    requestAnimationFrame(fitCanvas);
  }
  let importKind="project";
  function chooseImport(kind) {
    if(readonly)return;
    importKind=kind;
    $("file-input").accept=kind==="png"?".png,image/png":".json,application/json";
    $("file-input").click();
  }
  $("file-open").onclick = () => chooseImport("project");
  $("file-import").onclick = () => {if(!paletteMode)chooseImport("png");};
  async function importPng(file) {
    const target=current(),targetMode=mode,targetProject=project;
    const signature=new Uint8Array(await file.slice(0,8).arrayBuffer());
    if(signature.join(",")!=="137,80,78,71,13,10,26,10")throw new Error("Choose a valid PNG image.");
    const url=URL.createObjectURL(file),image=new Image();
    try {
      image.src=url;await image.decode();
      if(image.width>1024 || image.height>1024)throw new Error("PNG dimensions cannot exceed 1024 pixels.");
      const canvas=document.createElement("canvas");canvas.width=image.width;canvas.height=image.height;
      const context=canvas.getContext("2d");context.drawImage(image,0,0);
      const art=indexedPng(context.getImageData(0,0,image.width,image.height),{sprite:targetMode==="sprite",frameWidth:target.w,frameHeight:target.h});
      if(readonly || project!==targetProject || mode!==targetMode || current()!==target)throw new Error("Artwork changed while importing. Select the target asset and try again.");
      if(targetMode==="scene" && (art.w!==target.w || art.h!==target.h))throw new Error("Scene PNG dimensions must match the current scene.");
      if(targetMode==="tile" && (art.w!==target.w || art.h!==target.h) && project.scenes.some(scene=>Object.values(scene.tiles).some(cell=>cell.tileId===target.id)))
        throw new Error("PNG dimensions must match this linked tile. Import a different size into a new tile instead.");
      const imported=art.palettes.map((colors,i)=>({id:uid("pal"),name:`${file.name.replace(/\.png$/i,"")} ${i+1}`,colors}));
      frameIndex=playFrame=0;stopPalettePlayback();
      changed(()=>{
        project.palettes.push(...imported);
        target.w=art.w;target.h=art.h;
        if(targetMode==="scene") {
          target.pixels=art.images[0].pixels;target.pixelPalettes={};
          art.images[0].tilePal.forEach((p,i)=>{target.pixelPalettes[`${i%(art.w/8)},${Math.floor(i/(art.w/8))}`]=imported[p].id;});
        } else {
          target.palettes=imported.map(p=>p.id);
          if(targetMode==="sprite")target.frames=art.images.map(im=>({id:uid("frame"),...im}));
          else target.image=art.images[0];
        }
      });
      selectedPalette=project.palettes.length-imported.length;render();applyDrawingScale();
    } finally {URL.revokeObjectURL(url);}
  }
  $("file-input").onchange = async (e) => {
    const f = e.target.files[0];
    e.target.value="";
    if (!f) return;
    try {
      if(readonly)return;
      if(importKind==="png") {await importPng(f);return;}
      const next=migrate(JSON.parse(await f.text()));
      preserveLegacyScenePalettes(next);
      replaceProject(next);
      await save();
      render();
    } catch (err) {
      alert("Could not import " + (importKind==="png"?"PNG":"project") + ": " + err.message);
    }
  };
  $("file-export").onclick = () =>
    download(current().name + ".png", view.toDataURL("image/png"), "image/png");
  $("copy-png").onclick = async () => {
    try {
      await navigator.clipboard.write([
        new ClipboardItem({
          "image/png": await new Promise((r) => view.toBlob(r, "image/png")),
        }),
      ]);
    } catch (_) {
      alert("Clipboard access is unavailable.");
    }
  };
  function crc32(bytes) {
    let c = ~0;
    for (const b of bytes) {
      c ^= b;
      for (let i = 0; i < 8; i++) c = (c >>> 1) ^ (-(c & 1) & 0xedb88320);
    }
    return ~c >>> 0;
  }
  function zipStored(files) {
    const enc = new TextEncoder(),
      chunks = [],
      central = [];
    let off = 0;
    const put16 = (a, n) => {
        a.push(n & 255, (n >>> 8) & 255);
      },
      put32 = (a, n) => {
        a.push(n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255);
      };
    for (const f of files) {
      const name = enc.encode(f.name),
        data = typeof f.data === "string" ? enc.encode(f.data) : f.data,
        crc = crc32(data),
        h = [];
      put32(h, 0x04034b50);
      put16(h, 20);
      put16(h, 0);
      put16(h, 0);
      put16(h, 0);
      put16(h, 0);
      put32(h, crc);
      put32(h, data.length);
      put32(h, data.length);
      put16(h, name.length);
      put16(h, 0);
      chunks.push(new Uint8Array([...h, ...name, ...data]));
      const c = [];
      put32(c, 0x02014b50);
      put16(c, 20);
      put16(c, 20);
      put16(c, 0);
      put16(c, 0);
      put16(c, 0);
      put16(c, 0);
      put32(c, crc);
      put32(c, data.length);
      put32(c, data.length);
      put16(c, name.length);
      put16(c, 0);
      put16(c, 0);
      put16(c, 0);
      put16(c, 0);
      put32(c, 0);
      put32(c, off);
      central.push(new Uint8Array([...c, ...name]));
      off += h.length + name.length + data.length;
    }
    const end = [];
    const centralSize = central.reduce((n, x) => n + x.length, 0);
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, files.length);
    put16(end, files.length);
    put32(end, centralSize);
    put32(end, off);
    put16(end, 0);
    return new Blob([...chunks, ...central, new Uint8Array(end)], {
      type: "application/zip",
    });
  }
  $("file-save-now").onclick = async () => {
    clearTimeout(changed.timer);
    await save();
    render();
  };
  $("file-gbc").onclick = () => {
    const manifest = {
      version: 1,
      system: "gbc",
      projectId: project.id,
      limits: {
        screen: [160, 144],
        map: [256, 256],
        colorsPerTile: 4,
        bgPalettes: 8,
        patterns: 512,
        objectScanlineLimit: 10,
      },
      note:
        "Quarter-turns are baked into exported variants; runtime streaming is the game developer's responsibility.",
    };
    download(
      "gbc-art-bundle.zip",
      zipStored([
        { name: "manifest.json", data: JSON.stringify(manifest, null, 2) },
        { name: "project.json", data: JSON.stringify(project) },
      ]),
      "application/zip"
    );
  };
  function syncHex() {
    const p = paletteList()[selectedPalette];
    if (!p || !$("hex-input")) return;
    const c = p.colors[color].map((v) => Math.round((v * 255) / 31));
    $("hex-input").value =
      "#" +
      c
        .map((v) => v.toString(16).padStart(2, "0"))
        .join("")
        .toUpperCase();
  }
  function applyHex() {
    const raw = $("hex-input").value.trim().replace(/^#/, "");
    if (!/^[0-9a-fA-F]{6}$/.test(raw)) {
      alert("Enter a hex color like #65B8CC.");
      return;
    }
    const p = paletteList()[selectedPalette];
    if (!p) return;
    const c = [0, 1, 2].map((i) => parseInt(raw.slice(i * 2, i * 2 + 2), 16));
    p.colors[color] = c.map((v) => Math.round((v * 31) / 255));
    render();
    save();
  }
  $("hex-apply").onclick = applyHex;
  $("hex-input").onkeydown = (e) => {
    if (e.key === "Enter") applyHex();
  };
  const PALETTE_PRESETS = "beaver-gbc-palette-presets";
  function readPalettePresets() {
    try {
      return JSON.parse(localStorage.getItem(PALETTE_PRESETS) || "[]");
    } catch (_) {
      return [];
    }
  }
  $("pal-save").onclick = () => {
    const p = paletteList()[selectedPalette];
    if (!p) return;
    const all = readPalettePresets().filter((x) => x.name !== p.name);
    all.push({ name: p.name, colors: cp(p.colors) });
    localStorage.setItem(PALETTE_PRESETS, JSON.stringify(all));
    refreshPalettePresets();
  };
  window.addEventListener("keydown", (e) => {
    if (e.target.closest('input,textarea,select,[contenteditable=true],[role=combobox],[role=option],[role=slider],[role=textbox]')) return;
    if(e.code==="Space" && (e.target===view || e.target===document.body)) {
      e.preventDefault();spacePan=true;render(false);return;
    }
    if (!paletteMode && (e.key === "Delete" || e.key === "Backspace") && selections.length) {
      e.preventDefault();
      clearSelectionPixels();
      return;
    }
    if (e.key === "Escape") { resetSelection(); render(); return; }
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "z")
      { e.preventDefault(); $(e.shiftKey ? "redo" : "undo").click(); }
    if (!e.target.closest('input,select,[role=combobox],[role=option],[role=slider]') && e.key >= "1" && e.key <= "4") {
      color = +e.key - 1;
      render();
    }
  });
  window.addEventListener("keyup",e=>{if(e.code==="Space") {spacePan=false;if(!pan)render(false);}});
  $("zoom-label").dataset.zoom = "16";
  $("zoom-label").textContent = "16×";
  load().then(()=>{render();requestAnimationFrame(fitCanvas);});
  function builtInPalettePresets() {
    return GAME.map((x) => ({
      name: x[0],
      colors: x[1].map((c) => c.map((v) => Math.round((v * 31) / 255))),
    }));
  }
  function refreshPalettePresets() {
    const select = $("preset");
    if (!select) return;
    const builtIns = builtInPalettePresets();
    select.replaceChildren(
      new Option("Choose palette…", ""),
      ...builtIns.map(
        (p, i) => new Option(p.name + " (built-in)", "builtin:" + i)
      ),
      ...readPalettePresets().map(
        (p, i) => new Option(p.name || "Preset " + (i + 1), "saved:" + i)
      )
    );
  }
  $("preset").addEventListener("change", (e) => {
    const [kind, index] = e.target.value.split(":");
    const saved =
      kind === "builtin"
        ? builtInPalettePresets()[Number(index)]
        : readPalettePresets()[Number(index)];
    if (!saved || readonly) return;
    let p = project.palettes.find(
      (p) =>
        JSON.stringify(p.colors) === JSON.stringify(saved.colors) &&
        p.name === saved.name
    );
    if (!p) {
      p = { id: uid("pal"), name: saved.name, colors: cp(saved.colors) };
      project.palettes.push(p);
    }
    selectedPalette = paletteList().findIndex((x) => x.id === p.id);
    render();
    save();
    e.target.value = "";
  });
  function cleanArtworkCanvas() {
    const a = current(),
      im =
        mode === "sprite"
          ? a.frames[frameIndex]
          : mode === "tile"
          ? a.image
          : null,
      c = document.createElement("canvas"),
      cx = c.getContext("2d");
    c.width = a.w;
    c.height = a.h;
    cx.imageSmoothingEnabled = false;
    if (mode === "scene") return sceneCanvas(a);
    if (!im) return c;
    for (let y = 0; y < im.h; y++)
      for (let x = 0; x < im.w; x++) {
        const v = im.pixels[y * im.w + x];
        if (v < 0) continue;
        const p = im.tilePal
          ? im.tilePal[Math.floor(y / 8) * (im.w / 8) + Math.floor(x / 8)]
          : 0;
        cx.fillStyle = `rgb(${rgb(a, p, false, v).join(",")})`;
        cx.fillRect(x, y, 1, 1);
      }
    return c;
  }
  $("file-export").onclick = () =>
    download(
      current().name + ".png",
      // A PNG download must contain PNG bytes, rather than a data-URL text file.
      Uint8Array.from(
        atob(cleanArtworkCanvas().toDataURL("image/png").split(",")[1]),
        (c) => c.charCodeAt(0)
      ),
      "image/png"
    );
  $("copy-png").onclick = async () => {
    try {
      await navigator.clipboard.write([
        new ClipboardItem({
          "image/png": await new Promise((r) =>
            cleanArtworkCanvas().toBlob(r, "image/png")
          ),
        }),
      ]);
    } catch (_) {
      alert("Clipboard access is unavailable.");
    }
  };
  $("tool-select").onclick = () => {
    tool = "select";
    selectionPreview = null;
    selectionStart = null;
    render();
  };
  $("brush-palette").onchange = (e) => {
    selectedPalette = paletteList().findIndex((p) => p.id === e.target.value);
    render();
  };
  try {
    const onionPreference = localStorage.getItem("beaver-editor-onion");
    if (onionPreference !== null) $("onion").checked = onionPreference === "on";
  } catch (_) {}
  $("onion").addEventListener("change", () => {
    try { localStorage.setItem("beaver-editor-onion", $("onion").checked ? "on" : "off"); } catch (_) {}
  });
  for (const id of ['onion','screen-preview']) $(id).addEventListener('workspace-redraw',()=>render());
}
