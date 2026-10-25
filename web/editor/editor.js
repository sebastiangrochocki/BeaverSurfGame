/* Beaver Surf pixel editor. MIT; see LICENSE.
 * Pixels are palette indices 0–3. Colors live in palettes of four RGB555 values,
 * which is what a Game Boy Color tile actually stores. */
(function () {
  const DMG = [[155, 188, 15], [139, 172, 15], [48, 98, 48], [15, 56, 15]];
  const GRAY = [[224, 224, 224], [168, 168, 168], [96, 96, 96], [16, 16, 16]];
  const SIZES = ["8x8", "8x16", "16x16", "24x32", "32x16", "32x32"];
  const SAVE_KEY = "beaver-pixel-palettes";

  function to555(r, g, b) {
    return [Math.round(r * 31 / 255), Math.round(g * 31 / 255), Math.round(b * 31 / 255)];
  }
  function to8(c) {
    return c.map((v) => Math.round(v * 255 / 31));
  }
  function pal(name, triples) {
    return { name, colors: triples.map((t) => to555(t[0], t[1], t[2])) };
  }

  const GAME = [
    pal("HUD", [[8, 32, 72], [255, 255, 255], [220, 48, 56], [64, 192, 88]]),
    pal("Sky", [[16, 52, 128], [40, 108, 188], [96, 176, 224], [176, 216, 240]]),
    pal("Cloud", [[96, 176, 224], [255, 255, 255], [140, 168, 188], [176, 216, 240]]),
    pal("Island", [[20, 88, 156], [56, 168, 64], [220, 184, 112], [72, 164, 208]]),
    pal("Ship", [[20, 88, 156], [255, 255, 255], [128, 72, 36], [208, 40, 48]]),
    pal("Sun", [[96, 176, 224], [255, 184, 24], [255, 228, 128], [255, 255, 255]]),
    pal("Water", [[4, 20, 56], [12, 64, 128], [48, 156, 212], [240, 252, 255]]),
    pal("Waterline", [[20, 88, 156], [240, 252, 255], [48, 156, 212], [4, 20, 56]]),
    pal("Beaver", [[0, 0, 0], [0, 0, 0], [156, 90, 60], [153, 0, 48]]),
    pal("Gull", [[0, 0, 0], [48, 40, 32], [255, 255, 255], [255, 160, 32]]),
    pal("Ring", [[0, 0, 0], [140, 80, 8], [248, 188, 32], [255, 236, 140]]),
    pal("Heart", [[0, 0, 0], [120, 16, 24], [232, 48, 56], [255, 255, 255]]),
    pal("Splash", [[0, 0, 0], [16, 80, 150], [180, 220, 240], [255, 255, 255]]),
    pal("Arrow", [[0, 0, 0], [8, 32, 72], [232, 48, 56], [255, 255, 255]]),
  ];

  const $ = (id) => document.getElementById(id);
  const view = $("view");
  const ctx = view.getContext("2d");

  let doc = null;
  let color = 2;
  let tool = "draw";
  let zoom = 12;
  let hover = null;
  let cell = { x: 0, y: 0 };
  let painting = false;
  let last = null;
  let undo = [];
  let redo = [];
  let playTimer = null;
  let message = "";

  function blankFrame(w, h, paletteIndex) {
    return {
      pixels: new Array(w * h).fill(0),
      tilePal: new Array((w / 8) * (h / 8)).fill(paletteIndex),
    };
  }

  function fresh() {
    doc = {
      system: "gbc",
      kind: "sprite",
      look: "green",
      frameW: 32,
      frameH: 32,
      frame: 0,
      pal: 0,
      palettes: [JSON.parse(JSON.stringify(GAME[8]))],
      frames: [blankFrame(32, 32, 0)],
    };
    color = 2;
    undo = [];
    redo = [];
    message = "";
  }

  function capture() {
    return JSON.stringify({
      frameW: doc.frameW,
      frameH: doc.frameH,
      frame: doc.frame,
      pal: doc.pal,
      palettes: doc.palettes,
      frames: doc.frames,
    });
  }

  function restore(raw) {
    const data = JSON.parse(raw);
    doc.frameW = data.frameW;
    doc.frameH = data.frameH;
    doc.frame = data.frame;
    doc.pal = data.pal;
    doc.palettes = data.palettes;
    doc.frames = data.frames;
  }

  function commit(fn) {
    undo.push(capture());
    if (undo.length > 80) undo.shift();
    redo = [];
    fn();
    render();
  }

  function current() {
    return doc.frames[doc.frame];
  }

  function cols() {
    return doc.frameW / 8;
  }

  function cellAt(x, y) {
    return Math.floor(y / 8) * cols() + Math.floor(x / 8);
  }

  function css(rgb) {
    return `rgb(${rgb[0]}, ${rgb[1]}, ${rgb[2]})`;
  }

  function shade(index, paletteIndex) {
    if (doc.system === "gb") return (doc.look === "gray" ? GRAY : DMG)[index];
    const palIndex = Math.min(paletteIndex, doc.palettes.length - 1);
    return to8(doc.palettes[palIndex].colors[index]);
  }

  function paintPixel(frame, x, y, index) {
    if (x < 0 || y < 0 || x >= doc.frameW || y >= doc.frameH) return;
    frame.pixels[y * doc.frameW + x] = index;
    frame.tilePal[cellAt(x, y)] = doc.pal;
  }

  function plotLine(x0, y0, x1, y1, plot) {
    const dx = Math.abs(x1 - x0);
    const dy = Math.abs(y1 - y0);
    const sx = x0 < x1 ? 1 : -1;
    const sy = y0 < y1 ? 1 : -1;
    let err = dx - dy;
    for (;;) {
      plot(x0, y0);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  function flood(frame, x, y, index) {
    const from = frame.pixels[y * doc.frameW + x];
    if (from === index) return;
    const stack = [[x, y]];
    while (stack.length) {
      const [px, py] = stack.pop();
      if (px < 0 || py < 0 || px >= doc.frameW || py >= doc.frameH) continue;
      const i = py * doc.frameW + px;
      if (frame.pixels[i] !== from) continue;
      frame.pixels[i] = index;
      frame.tilePal[cellAt(px, py)] = doc.pal;
      stack.push([px + 1, py], [px - 1, py], [px, py + 1], [px, py - 1]);
    }
  }

  function drawFrame(frame, z, skipClear) {
    for (let y = 0; y < doc.frameH; y++) {
      for (let x = 0; x < doc.frameW; x++) {
        const index = frame.pixels[y * doc.frameW + x];
        if (skipClear && index === 0) continue;
        const palIndex = frame.tilePal[cellAt(x, y)];
        let rgb;
        if (doc.kind === "sprite" && index === 0) {
          rgb = ((x + y) & 1) ? [28, 44, 58] : [16, 28, 40];
        } else {
          rgb = shade(index, palIndex);
        }
        ctx.fillStyle = css(rgb);
        ctx.fillRect(x * z, y * z, z, z);
      }
    }
  }

  function drawThumb(canvas, frame, on) {
    const scale = Math.max(1, Math.floor(56 / Math.max(doc.frameW, doc.frameH)));
    canvas.width = doc.frameW * scale;
    canvas.height = doc.frameH * scale;
    const g = canvas.getContext("2d");
    g.imageSmoothingEnabled = false;
    for (let y = 0; y < doc.frameH; y++) {
      for (let x = 0; x < doc.frameW; x++) {
        const index = frame.pixels[y * doc.frameW + x];
        const palIndex = frame.tilePal[cellAt(x, y)];
        const rgb = doc.kind === "sprite" && index === 0
          ? [12, 22, 32]
          : shade(index, palIndex);
        g.fillStyle = css(rgb);
        g.fillRect(x * scale, y * scale, scale, scale);
      }
    }
    canvas.parentElement.className = on ? "on" : "";
  }

  function hexByte(n) {
    return "$" + n.toString(16).padStart(2, "0");
  }

  function cellHex() {
    const frame = current();
    const lines = [];
    for (let row = 0; row < 8; row++) {
      let lo = 0;
      let hi = 0;
      for (let col = 0; col < 8; col++) {
        const x = cell.x * 8 + col;
        const y = cell.y * 8 + row;
        if (x >= doc.frameW || y >= doc.frameH) continue;
        const p = frame.pixels[y * doc.frameW + x] & 3;
        if (p & 1) lo |= 0x80 >> col;
        if (p & 2) hi |= 0x80 >> col;
      }
      lines.push("DB " + hexByte(lo) + ", " + hexByte(hi));
    }
    return lines.join("\n");
  }

  function render() {
    const z = zoom;
    view.width = doc.frameW * z;
    view.height = doc.frameH * z;
    ctx.imageSmoothingEnabled = false;
    ctx.clearRect(0, 0, view.width, view.height);
    const showOnion = $("onion").checked && !playTimer && doc.frame > 0;
    if (showOnion) {
      ctx.globalAlpha = 0.35;
      drawFrame(doc.frames[doc.frame - 1], z, true);
      ctx.globalAlpha = 1;
    }
    drawFrame(current(), z, showOnion);
    ctx.strokeStyle = "rgba(232, 238, 242, 0.16)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = 8; x < doc.frameW; x += 8) {
      ctx.moveTo(x * z + 0.5, 0);
      ctx.lineTo(x * z + 0.5, view.height);
    }
    for (let y = 8; y < doc.frameH; y += 8) {
      ctx.moveTo(0, y * z + 0.5);
      ctx.lineTo(view.width, y * z + 0.5);
    }
    ctx.stroke();
    if (hover) {
      ctx.strokeStyle = "#7eb6d6";
      ctx.strokeRect(hover.tx * 8 * z + 1, hover.ty * 8 * z + 1, 8 * z - 2, 8 * z - 2);
    }

    const sw = $("swatches");
    sw.replaceChildren();
    for (let i = 0; i < 4; i++) {
      const button = document.createElement("button");
      button.type = "button";
      button.title = "Color " + i + (doc.kind === "sprite" && i === 0 ? " (clear)" : "");
      button.className = i === color ? "on" : "";
      const clear = doc.kind === "sprite" && i === 0;
      button.style.background = clear
        ? "repeating-conic-gradient(#1a2636 0% 25%, #243044 0% 50%) 50% / 10px 10px"
        : css(shade(i, doc.pal));
      button.addEventListener("click", () => {
        color = i;
        render();
      });
      sw.appendChild(button);
    }

    const tabs = $("pal-tabs");
    tabs.replaceChildren();
    doc.palettes.forEach((palette, index) => {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = String(index + 1);
      button.title = palette.name;
      button.className = index === doc.pal ? "on" : "";
      button.addEventListener("click", () => {
        doc.pal = index;
        render();
      });
      tabs.appendChild(button);
    });

    const frameBox = $("frames");
    frameBox.replaceChildren();
    doc.frames.forEach((frame, index) => {
      const button = document.createElement("button");
      button.type = "button";
      button.title = "Frame " + (index + 1);
      const canvas = document.createElement("canvas");
      button.appendChild(canvas);
      button.addEventListener("click", () => {
        doc.frame = index;
        render();
      });
      frameBox.appendChild(button);
      drawThumb(canvas, frame, index === doc.frame);
    });

    $("system").value = doc.system;
    $("kind").value = doc.kind;
    $("look").value = doc.look;
    $("look-label").hidden = doc.system !== "gb";
    $("sliders").hidden = doc.system !== "gbc";
    $("pal-add").disabled = doc.palettes.length >= 8;
    $("pal-del").disabled = doc.palettes.length < 2;
    $("undo").disabled = undo.length === 0;
    $("redo").disabled = redo.length === 0;
    $("drop-frame").disabled = doc.frames.length < 2;
    ["draw", "fill", "pick"].forEach((name) => {
      $("tool-" + name).className = tool === name ? "on" : "";
    });
    $("zoom-label").textContent = zoom + "×";
    const sizeKey = doc.frameW + "x" + doc.frameH;
    $("size").value = SIZES.includes(sizeKey) ? sizeKey : "custom";
    if (document.activeElement !== $("width")) $("width").value = doc.frameW;
    if (document.activeElement !== $("height")) $("height").value = doc.frameH;
    const palette = doc.palettes[doc.pal];
    if (document.activeElement !== $("pal-name")) $("pal-name").value = palette.name;
    const channels = palette.colors[color];
    if (document.activeElement !== $("ch-r")) $("ch-r").value = channels[0];
    if (document.activeElement !== $("ch-g")) $("ch-g").value = channels[1];
    if (document.activeElement !== $("ch-b")) $("ch-b").value = channels[2];
    $("hex").textContent = cellHex();
    const bits = doc.system === "gbc" ? "Color" : "Game Boy";
    $("status").textContent = message || (
      "Frame " + (doc.frame + 1) + " of " + doc.frames.length +
      " · " + doc.frameW + "×" + doc.frameH +
      " · " + bits + " · " + palette.name
    );
  }

  function eventPixel(event) {
    const rect = view.getBoundingClientRect();
    const x = Math.floor((event.clientX - rect.left) * (view.width / rect.width) / zoom);
    const y = Math.floor((event.clientY - rect.top) * (view.height / rect.height) / zoom);
    return { x, y };
  }

  function useTool(x, y) {
    const frame = current();
    if (x < 0 || y < 0 || x >= doc.frameW || y >= doc.frameH) return;
    cell = { x: Math.floor(x / 8), y: Math.floor(y / 8) };
    if (tool === "pick") {
      color = frame.pixels[y * doc.frameW + x];
      doc.pal = frame.tilePal[cellAt(x, y)];
      return;
    }
    if (tool === "fill") flood(frame, x, y, color);
    else paintPixel(frame, x, y, color);
  }

  function pointerDown(event) {
    view.setPointerCapture(event.pointerId);
    const p = eventPixel(event);
    painting = true;
    if (tool !== "pick") undo.push(capture());
    if (undo.length > 80) undo.shift();
    redo = [];
    useTool(p.x, p.y);
    last = p;
    message = "";
    render();
  }

  function pointerMove(event) {
    const p = eventPixel(event);
    hover = {
      tx: Math.max(0, Math.min(cols() - 1, Math.floor(p.x / 8))),
      ty: Math.max(0, Math.min(doc.frameH / 8 - 1, Math.floor(p.y / 8))),
    };
    if (painting && tool === "draw" && last) {
      plotLine(last.x, last.y, p.x, p.y, (x, y) => paintPixel(current(), x, y, color));
      cell = hover;
      last = p;
    }
    render();
  }

  function pointerUp() {
    painting = false;
    last = null;
  }

  function resizeTo(w, h) {
    w = Math.round(w);
    h = Math.round(h);
    if (w < 8 || h < 8 || w > 256 || h > 256 || w % 8 || h % 8) {
      message = "Width and height are multiples of 8, from 8 to 256.";
      render();
      return;
    }
    if (w === doc.frameW && h === doc.frameH) return;
    let lose = false;
    doc.frames.forEach((frame) => {
      for (let y = 0; y < doc.frameH; y++) {
        for (let x = 0; x < doc.frameW; x++) {
          if ((x >= w || y >= h) && frame.pixels[y * doc.frameW + x]) lose = true;
        }
      }
    });
    if (lose && !window.confirm("Shrinking this frame crops pixels that fall outside the new size.")) return;
    commit(() => {
      doc.frames.forEach((frame) => {
        const pixels = new Array(w * h).fill(0);
        const tilePal = new Array((w / 8) * (h / 8)).fill(doc.pal);
        const copyW = Math.min(w, doc.frameW);
        const copyH = Math.min(h, doc.frameH);
        for (let y = 0; y < copyH; y++) {
          for (let x = 0; x < copyW; x++) pixels[y * w + x] = frame.pixels[y * doc.frameW + x];
        }
        const oldCols = doc.frameW / 8;
        for (let ty = 0; ty < copyH / 8; ty++) {
          for (let tx = 0; tx < copyW / 8; tx++) {
            tilePal[ty * (w / 8) + tx] = frame.tilePal[ty * oldCols + tx];
          }
        }
        frame.pixels = pixels;
        frame.tilePal = tilePal;
      });
      doc.frameW = w;
      doc.frameH = h;
      cell = { x: 0, y: 0 };
    });
  }

  function savedList() {
    try {
      const list = JSON.parse(localStorage.getItem(SAVE_KEY) || "[]");
      return Array.isArray(list) ? list : [];
    } catch (error) {
      return [];
    }
  }

  function fillPresets() {
    const select = $("preset");
    select.replaceChildren();
    const blank = document.createElement("option");
    blank.value = "";
    blank.textContent = "Load a palette…";
    select.appendChild(blank);
    GAME.forEach((palette, index) => {
      const option = document.createElement("option");
      option.value = "game:" + index;
      option.textContent = palette.name;
      select.appendChild(option);
    });
    savedList().forEach((palette, index) => {
      const option = document.createElement("option");
      option.value = "saved:" + index;
      option.textContent = "Saved · " + palette.name;
      select.appendChild(option);
    });
  }

  function applyPreset(value) {
    if (!value) return;
    const [kind, indexText] = value.split(":");
    const source = kind === "game" ? GAME[Number(indexText)] : savedList()[Number(indexText)];
    if (!source) return;
    commit(() => {
      doc.palettes[doc.pal] = JSON.parse(JSON.stringify(source));
    });
    $("preset").value = "";
  }

  function download(filename, blob) {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = filename;
    link.click();
    URL.revokeObjectURL(link.href);
  }

  function fileBase() {
    const name = $("file-name").value.trim() || "sprite";
    return name.replace(/[^\w.-]+/g, "-");
  }

  function sheetSize() {
    return { width: doc.frameW * doc.frames.length, height: doc.frameH };
  }

  function sheetIndex(x, y) {
    const frameIndex = Math.floor(x / doc.frameW);
    const frame = doc.frames[frameIndex];
    const lx = x - frameIndex * doc.frameW;
    return {
      index: frame.pixels[y * doc.frameW + lx],
      pal: frame.tilePal[Math.floor(y / 8) * cols() + Math.floor(lx / 8)],
    };
  }

  const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
      table[n] = c >>> 0;
    }
    return table;
  })();

  function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function adler32(data) {
    let a = 1;
    let b = 0;
    for (let i = 0; i < data.length; i++) {
      a = (a + data[i]) % 65521;
      b = (b + a) % 65521;
    }
    return ((b << 16) | a) >>> 0;
  }

  function concat(parts) {
    const size = parts.reduce((sum, part) => sum + part.length, 0);
    const out = new Uint8Array(size);
    let offset = 0;
    parts.forEach((part) => {
      out.set(part, offset);
      offset += part.length;
    });
    return out;
  }

  function pngChunk(type, data) {
    const out = new Uint8Array(12 + data.length);
    const view = new DataView(out.buffer);
    view.setUint32(0, data.length);
    out.set([type.charCodeAt(0), type.charCodeAt(1), type.charCodeAt(2), type.charCodeAt(3)], 4);
    out.set(data, 8);
    view.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
    return out;
  }

  function zlibStore(data) {
    const blocks = [new Uint8Array([0x78, 0x01])];
    let offset = 0;
    do {
      const n = Math.min(65535, data.length - offset);
      const final = offset + n >= data.length;
      const block = new Uint8Array(5 + n);
      block[0] = final ? 1 : 0;
      block[1] = n & 255;
      block[2] = (n >> 8) & 255;
      block[3] = (~n) & 255;
      block[4] = ((~n) >> 8) & 255;
      block.set(data.subarray(offset, offset + n), 5);
      blocks.push(block);
      offset += n;
    } while (offset < data.length);
    const tail = new Uint8Array(4);
    new DataView(tail.buffer).setUint32(0, adler32(data));
    blocks.push(tail);
    return concat(blocks);
  }

  function exportPngBytes() {
    const { width, height } = sheetSize();
    const colors = [];
    doc.palettes.forEach((palette) => {
      palette.colors.forEach((channel) => colors.push(...to8(channel)));
    });
    const raw = new Uint8Array(height * (width + 1));
    for (let y = 0; y < height; y++) {
      const row = y * (width + 1);
      raw[row] = 0;
      for (let x = 0; x < width; x++) {
        const sample = sheetIndex(x, y);
        raw[row + 1 + x] = sample.pal * 4 + sample.index;
      }
    }
    const ihdr = new Uint8Array(13);
    const ihdrView = new DataView(ihdr.buffer);
    ihdrView.setUint32(0, width);
    ihdrView.setUint32(4, height);
    ihdr[8] = 8;
    ihdr[9] = 3;
    const trns = new Uint8Array(doc.palettes.length * 4);
    trns.fill(255);
    if (doc.kind === "sprite") {
      doc.palettes.forEach((_, index) => {
        trns[index * 4] = 0;
      });
    }
    return concat([
      new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]),
      pngChunk("IHDR", ihdr),
      pngChunk("PLTE", new Uint8Array(colors)),
      pngChunk("tRNS", trns),
      pngChunk("IDAT", zlibStore(raw)),
      pngChunk("IEND", new Uint8Array()),
    ]);
  }

  /* One frame as a normal RGBA PNG, so paste lands in other programs. Color 0 of a sprite is clear. */
  function framePngBytes() {
    const width = doc.frameW;
    const height = doc.frameH;
    const frame = current();
    const raw = new Uint8Array(height * (1 + width * 4));
    for (let y = 0; y < height; y++) {
      const row = y * (1 + width * 4);
      raw[row] = 0;
      for (let x = 0; x < width; x++) {
        const index = frame.pixels[y * width + x];
        const at = row + 1 + x * 4;
        if (doc.kind === "sprite" && index === 0) continue;
        const rgb = shade(index, frame.tilePal[cellAt(x, y)]);
        raw[at] = rgb[0];
        raw[at + 1] = rgb[1];
        raw[at + 2] = rgb[2];
        raw[at + 3] = 255;
      }
    }
    const ihdr = new Uint8Array(13);
    const ihdrView = new DataView(ihdr.buffer);
    ihdrView.setUint32(0, width);
    ihdrView.setUint32(4, height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    return concat([
      new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]),
      pngChunk("IHDR", ihdr),
      pngChunk("IDAT", zlibStore(raw)),
      pngChunk("IEND", new Uint8Array()),
    ]);
  }

  function paeth(a, b, c) {
    const p = a + b - c;
    const pa = Math.abs(p - a);
    const pb = Math.abs(p - b);
    const pc = Math.abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
  }

  function unfilter(data, width, height, bpp) {
    const stride = width * bpp;
    const out = new Uint8Array(height * stride);
    let src = 0;
    for (let y = 0; y < height; y++) {
      const filter = data[src++];
      const rowStart = y * stride;
      for (let x = 0; x < stride; x++) {
        const left = x >= bpp ? out[rowStart + x - bpp] : 0;
        const up = y > 0 ? out[rowStart - stride + x] : 0;
        const ul = y > 0 && x >= bpp ? out[rowStart - stride + x - bpp] : 0;
        let value = data[src++];
        if (filter === 1) value += left;
        else if (filter === 2) value += up;
        else if (filter === 3) value += (left + up) >> 1;
        else if (filter === 4) value += paeth(left, up, ul);
        out[rowStart + x] = value & 255;
      }
    }
    return out;
  }

  async function inflate(bytes) {
    const raw = bytes.subarray(2, bytes.length - 4);
    const stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  async function importPng(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let offset = 8;
    let width = 0;
    let height = 0;
    let bitDepth = 0;
    let colorType = 0;
    let palette = null;
    const idat = [];
    while (offset + 8 <= bytes.length) {
      const length = view.getUint32(offset);
      const type = String.fromCharCode(bytes[offset + 4], bytes[offset + 5], bytes[offset + 6], bytes[offset + 7]);
      const data = bytes.subarray(offset + 8, offset + 8 + length);
      if (type === "IHDR") {
        width = view.getUint32(offset + 8);
        height = view.getUint32(offset + 12);
        bitDepth = data[8];
        colorType = data[9];
      } else if (type === "PLTE") palette = data;
      else if (type === "IDAT") idat.push(data);
      else if (type === "IEND") break;
      offset += 12 + length;
    }
    if (bitDepth !== 8 || (colorType !== 3 && colorType !== 6 && colorType !== 2)) {
      throw new Error("Use an 8-bit PNG, indexed or full color.");
    }
    const inflated = await inflate(concat(idat));
    const bpp = colorType === 2 ? 3 : colorType === 6 ? 4 : 1;
    const pixels = unfilter(inflated, width, height, bpp);
    if (width % 8 || height % 8) throw new Error("PNG width and height need to be multiples of 8.");

    let frameW = doc.frameW;
    let frameH = doc.frameH;
    let count = 1;
    if (height === frameH && width % frameW === 0) {
      count = width / frameW;
      message = "";
    } else {
      frameW = width;
      frameH = height;
      count = 1;
      message = "Imported as one frame. Set the frame size first, then import again to split a strip.";
    }

    const palettes = [];
    if (colorType === 3 && palette) {
      const groups = Math.max(1, Math.round(palette.length / 3 / 4));
      for (let i = 0; i < groups && i < 8; i++) {
        const colors = [];
        for (let c = 0; c < 4; c++) {
          const at = (i * 4 + c) * 3;
          colors.push(to555(palette[at] || 0, palette[at + 1] || 0, palette[at + 2] || 0));
        }
        palettes.push({ name: "PNG " + (i + 1), colors });
      }
    }

    commit(() => {
      if (palettes.length) doc.palettes = palettes;
      doc.frameW = frameW;
      doc.frameH = frameH;
      doc.frame = 0;
      doc.pal = 0;
      doc.frames = [];
      for (let n = 0; n < count; n++) {
        const frame = blankFrame(frameW, frameH, 0);
        for (let y = 0; y < frameH; y++) {
          for (let x = 0; x < frameW; x++) {
            const srcX = n * frameW + x;
            const src = (y * width + srcX) * bpp;
            let index = 0;
            let palIndex = 0;
            if (colorType === 3) {
              const value = pixels[y * width + srcX];
              palIndex = Math.min(doc.palettes.length - 1, Math.floor(value / 4));
              index = value % 4;
            } else {
              const rgb = [pixels[src], pixels[src + 1], pixels[src + 2]];
              let best = Infinity;
              doc.palettes.forEach((paletteEntry, pi) => {
                paletteEntry.colors.forEach((entry, ci) => {
                  const shown = to8(entry);
                  const dist = (shown[0] - rgb[0]) ** 2 + (shown[1] - rgb[1]) ** 2 + (shown[2] - rgb[2]) ** 2;
                  if (dist < best) {
                    best = dist;
                    palIndex = pi;
                    index = ci;
                  }
                });
              });
            }
            frame.pixels[y * frameW + x] = index;
            frame.tilePal[Math.floor(y / 8) * (frameW / 8) + Math.floor(x / 8)] = palIndex;
          }
        }
        doc.frames.push(frame);
      }
    });
  }

  function saveJson() {
    const payload = {
      version: 1,
      system: doc.system,
      kind: doc.kind,
      look: doc.look,
      frameW: doc.frameW,
      frameH: doc.frameH,
      frame: doc.frame,
      pal: doc.pal,
      color,
      palettes: doc.palettes,
      frames: doc.frames,
    };
    download(fileBase() + ".pixel.json", new Blob([JSON.stringify(payload)], { type: "application/json" }));
  }

  function openJson(text) {
    const data = JSON.parse(text);
    if (!data || !Array.isArray(data.frames) || !Array.isArray(data.palettes)) throw new Error("Not a pixel file.");
    undo = [];
    redo = [];
    doc = {
      system: data.system === "gb" ? "gb" : "gbc",
      kind: data.kind === "tile" ? "tile" : "sprite",
      look: data.look === "gray" ? "gray" : "green",
      frameW: data.frameW,
      frameH: data.frameH,
      frame: data.frame || 0,
      pal: data.pal || 0,
      palettes: data.palettes,
      frames: data.frames,
    };
    color = data.color || 0;
    message = "";
    render();
  }

  function bind() {
    view.addEventListener("pointerdown", pointerDown);
    view.addEventListener("pointermove", pointerMove);
    view.addEventListener("pointerup", pointerUp);
    view.addEventListener("pointerleave", () => {
      hover = null;
      if (!painting) render();
    });

    ["draw", "fill", "pick"].forEach((name) => {
      $("tool-" + name).addEventListener("click", () => {
        tool = name;
        render();
      });
    });
    $("undo").addEventListener("click", () => {
      if (!undo.length) return;
      redo.push(capture());
      restore(undo.pop());
      render();
    });
    $("redo").addEventListener("click", () => {
      if (!redo.length) return;
      undo.push(capture());
      restore(redo.pop());
      render();
    });
    $("system").addEventListener("change", () => {
      doc.system = $("system").value;
      render();
    });
    $("kind").addEventListener("change", () => {
      doc.kind = $("kind").value;
      render();
    });
    $("look").addEventListener("change", () => {
      doc.look = $("look").value;
      render();
    });
    $("pal-name").addEventListener("input", () => {
      doc.palettes[doc.pal].name = $("pal-name").value.slice(0, 24);
    });
    ["r", "g", "b"].forEach((channel, index) => {
      const input = $("ch-" + channel);
      input.addEventListener("pointerdown", () => undo.push(capture()));
      input.addEventListener("input", () => {
        doc.palettes[doc.pal].colors[color][index] = Number(input.value);
        render();
      });
    });
    $("pal-add").addEventListener("click", () => {
      if (doc.palettes.length >= 8) return;
      commit(() => {
        const copy = JSON.parse(JSON.stringify(doc.palettes[doc.pal]));
        copy.name = "Palette " + (doc.palettes.length + 1);
        doc.palettes.push(copy);
        doc.pal = doc.palettes.length - 1;
      });
    });
    $("pal-dup").addEventListener("click", () => $("pal-add").click());
    $("pal-del").addEventListener("click", () => {
      if (doc.palettes.length < 2) return;
      commit(() => {
        const removed = doc.pal;
        doc.palettes.splice(removed, 1);
        doc.frames.forEach((frame) => {
          frame.tilePal = frame.tilePal.map((value) => {
            if (value === removed) return 0;
            if (value > removed) return value - 1;
            return value;
          });
        });
        if (doc.pal >= doc.palettes.length) doc.pal = doc.palettes.length - 1;
      });
    });
    $("pal-save").addEventListener("click", () => {
      const list = savedList();
      list.push(JSON.parse(JSON.stringify(doc.palettes[doc.pal])));
      localStorage.setItem(SAVE_KEY, JSON.stringify(list.slice(-24)));
      fillPresets();
      message = "Palette saved in this browser.";
      render();
    });
    $("preset").addEventListener("change", () => applyPreset($("preset").value));
    $("size").addEventListener("change", () => {
      const value = $("size").value;
      if (value === "custom") return;
      const [w, h] = value.split("x").map(Number);
      resizeTo(w, h);
    });
    $("width").addEventListener("change", () => resizeTo(Number($("width").value), doc.frameH));
    $("height").addEventListener("change", () => resizeTo(doc.frameW, Number($("height").value)));
    $("zoom-in").addEventListener("click", () => {
      zoom = Math.min(32, zoom + 2);
      render();
    });
    $("zoom-out").addEventListener("click", () => {
      zoom = Math.max(4, zoom - 2);
      render();
    });
    $("onion").addEventListener("change", render);
    $("clone").addEventListener("click", () => {
      commit(() => {
        const copy = JSON.parse(JSON.stringify(current()));
        doc.frames.splice(doc.frame + 1, 0, copy);
        doc.frame += 1;
      });
    });
    $("copy-png").addEventListener("click", async () => {
      const blob = new Blob([framePngBytes()], { type: "image/png" });
      try {
        await navigator.clipboard.write([new ClipboardItem({ "image/png": Promise.resolve(blob) })]);
        message = "Copied frame " + (doc.frame + 1) + " to the clipboard.";
      } catch (error) {
        message = "The browser blocked clipboard access.";
      }
      render();
    });
    $("drop-frame").addEventListener("click", () => {
      if (doc.frames.length < 2) return;
      commit(() => {
        doc.frames.splice(doc.frame, 1);
        if (doc.frame >= doc.frames.length) doc.frame = doc.frames.length - 1;
      });
    });
    $("play").addEventListener("click", () => {
      if (playTimer) {
        clearInterval(playTimer);
        playTimer = null;
        $("play").textContent = "Play";
        render();
        return;
      }
      $("play").textContent = "Stop";
      playTimer = setInterval(() => {
        doc.frame = (doc.frame + 1) % doc.frames.length;
        render();
      }, 140);
    });
    $("file-new").addEventListener("click", () => {
      if (!window.confirm("Start a new sprite? This clears the frames on screen.")) return;
      fresh();
      render();
    });
    $("file-save").addEventListener("click", saveJson);
    $("file-export").addEventListener("click", () => {
      download(fileBase() + ".png", new Blob([exportPngBytes()], { type: "image/png" }));
    });
    let openMode = "json";
    $("file-open").addEventListener("click", () => {
      openMode = "json";
      $("file-input").click();
    });
    $("file-import").addEventListener("click", () => {
      openMode = "png";
      $("file-input").click();
    });
    $("file-input").addEventListener("change", async () => {
      const file = $("file-input").files[0];
      $("file-input").value = "";
      if (!file) return;
      try {
        if (openMode === "png" || file.name.endsWith(".png")) await importPng(new Uint8Array(await file.arrayBuffer()));
        else openJson(await file.text());
      } catch (error) {
        message = error.message || "Could not open that file.";
        render();
      }
    });

    window.addEventListener("keydown", (event) => {
      const typing = event.target.matches("input, select, textarea");
      if (!typing && event.key >= "1" && event.key <= "4") {
        color = Number(event.key) - 1;
        render();
      } else if (!typing && event.key === "[") {
        doc.frame = (doc.frame + doc.frames.length - 1) % doc.frames.length;
        render();
      } else if (!typing && event.key === "]") {
        doc.frame = (doc.frame + 1) % doc.frames.length;
        render();
      } else if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "z") {
        event.preventDefault();
        $(event.shiftKey ? "redo" : "undo").click();
      } else if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "d") {
        event.preventDefault();
        $("clone").click();
      }
    });
  }

  fresh();
  fillPresets();
  bind();
  render();
  window.pixel = { get doc() { return doc; }, exportPngBytes, importPng };
})();
