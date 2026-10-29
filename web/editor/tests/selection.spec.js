import { test, expect } from "@playwright/test";
import { fixture } from "./fixtures.js";

async function savedProject(page) {
  return page.evaluate(async () => {
    const db = await new Promise((resolve, reject) => {
      const r = indexedDB.open("beaver-gbc-art", 1);
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
    const value = await new Promise((resolve, reject) => {
      const r = db.transaction("projects").objectStore("projects").get("current");
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
    db.close();
    return value;
  });
}

for (const mode of ["sprite", "tile", "scene"]) {
  test(`${mode}: Shift-drag previews and moves selection; Delete/Backspace, undo, reload`, async ({ page }) => {
    const errors = [];
    page.on("pageerror", e => errors.push(e.message));
    const p = fixture();
    const pixels = Array(256).fill(mode === "tile" ? 0 : -1);
    pixels[17] = 1; pixels[18] = 2; pixels[19] = 3;
    p.sprites[0].w = p.sprites[0].h = p.tiles[0].w = p.tiles[0].h = 16;
    p.sprites[0].frames[0] = { id: "frame", w: 16, h: 16, pixels: pixels.slice(), tilePal: [0, 0, 0, 0] };
    p.sprites[0].frames.push({ id: "other-frame", w: 16, h: 16, pixels: Array(256).fill(3), tilePal: [0, 0, 0, 0] });
    p.tiles[0].image = { w: 16, h: 16, pixels: pixels.slice(), tilePal: [0, 0, 0, 0] };
    p.scenes[0].w = p.scenes[0].h = 16;
    p.scenes[0].pixels = pixels.slice();
    p.scenes[0].pixelPalettes = { "0,0": "red" };
    await page.route("**/__seed", r => r.fulfill({ contentType: "text/html", body: "<!doctype html><title>Selection setup</title>" }));
    await page.goto("http://localhost:8086/__seed");
    await page.evaluate(async project => {
      const db = await new Promise((resolve, reject) => {
        const r = indexedDB.open("beaver-gbc-art", 1);
        r.onupgradeneeded = () => r.result.createObjectStore("projects");
        r.onsuccess = () => resolve(r.result); r.onerror = () => reject(r.error);
      });
      await new Promise((resolve, reject) => {
        const tx = db.transaction("projects", "readwrite");
        tx.objectStore("projects").put(project, "current");
        tx.oncomplete = resolve; tx.onerror = () => reject(tx.error);
      });
      db.close();
    }, p);
    await page.goto("http://localhost:8086/editor/");
    const enterMode = async () => {
      await page.getByRole("tab", { name: mode === "sprite" ? "Sprites" : mode === "tile" ? "Tiles" : "Scenes", exact: true }).click();
      if (mode === "scene") await page.locator("#layer-pixels-control").click();
      await page.locator("#tile-scale-control").click();
      await page.getByRole("option", { name: "1×", exact: true }).click();
    };
    await enterMode();
    await page.locator("#tool-select-control").click();
    const view = page.locator("#view");
    await view.scrollIntoViewIfNeeded();
    const box = await view.boundingBox();
    const zoom = await view.evaluate(c => c.width / 16);
    const at = (x, y) => ({ x: box.x + (x + .5) * zoom, y: box.y + (y + .5) * zoom });
    const moveMouse = async (x, y) => { const q = at(x, y); await page.mouse.move(q.x, q.y); };
    const canvasPixel = (x, y) => view.evaluate((c, { x, y, zoom }) => [...c.getContext("2d").getImageData((x + .5) * zoom, (y + .5) * zoom, 1, 1).data], { x, y, zoom });
    const art = project => mode === "sprite" ? project.sprites[0].frames[0].pixels : mode === "tile" ? project.tiles[0].image.pixels : project.scenes[0].pixels;
    await moveMouse(1, 1); await page.mouse.down(); await moveMouse(3, 2); await page.mouse.up();
    await page.keyboard.down("Shift");
    await moveMouse(2, 1); await page.mouse.down();
    await moveMouse(3, 2); await moveMouse(4, 3);
    expect(await canvasPixel(4, 3)).toEqual([0, 255, 0, 255]);
    expect(art(await savedProject(page))).toEqual(pixels); // preview is not a saved edit
    await page.mouse.up(); await page.keyboard.up("Shift");
    const moved = pixels.slice();
    for (const y of [1, 2]) for (const x of [1, 2, 3]) moved[y * 16 + x] = mode === "tile" ? 0 : -1;
    for (const y of [1, 2]) for (const x of [1, 2, 3]) moved[(y + 2) * 16 + x + 2] = pixels[y * 16 + x];
    await expect.poll(async () => art(await savedProject(page))).toEqual(moved);
    await page.locator("#undo-control").click();
    await expect.poll(async () => art(await savedProject(page))).toEqual(pixels);
    await page.locator("#redo-control").click();
    await expect.poll(async () => art(await savedProject(page))).toEqual(moved);
    await view.focus(); await page.keyboard.press("Delete");
    const cleared = moved.slice();
    for (const y of [3, 4]) for (const x of [3, 4, 5]) cleared[y * 16 + x] = mode === "tile" ? 0 : -1;
    await expect.poll(async () => art(await savedProject(page))).toEqual(cleared);
    await page.locator("#undo-control").click();
    await expect.poll(async () => art(await savedProject(page))).toEqual(moved);
    await view.focus(); await page.keyboard.press("Backspace");
    await expect.poll(async () => art(await savedProject(page))).toEqual(cleared);
    await page.locator("#undo-control").click();
    await expect.poll(async () => art(await savedProject(page))).toEqual(moved);
    await page.locator("#asset-name-control").fill("Selection test");
    await page.keyboard.press("Delete");
    await page.locator("#file-save-now-control").click();
    await expect.poll(async () => art(await savedProject(page))).toEqual(moved);
    const saved = await savedProject(page);
    expect(saved.sprites[0].frames[1].pixels).toEqual(Array(256).fill(3));
    if (mode === "scene") expect(saved.scenes[0].tiles).toEqual(p.scenes[0].tiles);
    // Cancel a live move without touching the saved pixels or adding history.
    await view.scrollIntoViewIfNeeded();
    const cancelBox = await view.boundingBox();
    await page.keyboard.down("Shift");
    await page.mouse.move(cancelBox.x + 4.5 * zoom, cancelBox.y + 3.5 * zoom);
    await page.mouse.down();
    await page.mouse.move(cancelBox.x + 6.5 * zoom, cancelBox.y + 5.5 * zoom);
    await page.keyboard.press("Escape");
    await page.mouse.up(); await page.keyboard.up("Shift");
    expect(art(await savedProject(page))).toEqual(moved);
    await page.screenshot({ path: `/private/tmp/beaver-selection-${mode}.png`, fullPage: true });
    await page.reload(); await enterMode();
    expect(art(await savedProject(page))).toEqual(moved);
    expect(errors).toEqual([]);
  });
}

test("sprite onion skin is a visible Radix checkbox, toggles the ghost and remembers its state", async ({ page }) => {
  await page.goto("http://localhost:8086/editor/");
  await page.locator("#tile-scale-control").click();
  await page.getByRole("option", { name: "1×", exact: true }).click();
  const view = page.locator("#view");
  await view.click({ position: { x: 12, y: 12 } });
  await page.locator("#blank-frame-control").click();
  const onion = page.locator("#onion-control");
  await expect(onion).toBeVisible();
  await expect(onion).toHaveAttribute("role", "checkbox");
  await expect(onion).toHaveAttribute("data-state", "checked");
  await expect(page.locator("#onion")).toBeHidden();
  expect(await onion.evaluate(n => !!n.closest("#art-workspace .canvas-view-options"))).toBe(true);
  const pixel = () => view.evaluate(c => [...c.getContext("2d").getImageData(12, 12, 1, 1).data]);
  const ghost = await pixel();
  expect(ghost).not.toEqual([17, 17, 20, 255]);
  await onion.click();
  await expect(onion).toHaveAttribute("data-state", "unchecked");
  expect(await pixel()).toEqual([17, 17, 20, 255]);
  await onion.click();
  expect(await pixel()).toEqual(ghost);
  await onion.click();
  await page.locator("#file-save-now-control").click();
  await expect(page.locator("#status")).toContainText("Saved");
  const p = await savedProject(page);
  expect(p.sprites[0].frames[0].pixels[33]).toBe(2);
  expect(p.sprites[0].frames[1].pixels.every(v => v === -1)).toBe(true);
  await page.screenshot({ path: "/private/tmp/beaver-onion-radix.png", fullPage: true });
  await page.reload();
  await expect(onion).toHaveAttribute("data-state", "unchecked");
  await page.getByRole("tab", { name: "Tiles", exact: true }).click();
  await expect(onion).toBeHidden();
  await page.getByRole("tab", { name: "Scenes", exact: true }).click();
  await expect(onion).toBeHidden();
  await page.getByRole("tab", { name: "Sprites", exact: true }).click();
  await expect(onion).toBeVisible();
});
