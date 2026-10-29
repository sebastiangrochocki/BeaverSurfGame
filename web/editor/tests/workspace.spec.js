import { test, expect } from "@playwright/test";
import { fixture } from "./fixtures.js";

async function choose(page, id, value) {
  const label =
    typeof value === "object"
      ? value.label
      : await page
          .locator("#" + id)
          .evaluate(
            (select, value) =>
              [...select.options].find((o) => o.value === value)?.textContent,
            value
          );
  await page.locator("#" + id + "-control").click();
  await page.getByRole("option", { name: label, exact: true }).click();
}

test("scene palette selection, stamping, recolor, undo, reload and clean PNG", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.route("**/__seed", (route) =>
    route.fulfill({
      contentType: "text/html",
      body: "<!doctype html><title>Test setup</title>",
    })
  );
  await page.goto("http://localhost:8086/__seed");
  await page.evaluate(async (p) => {
    const db = await new Promise((ok, no) => {
      const r = indexedDB.open("beaver-gbc-art", 1);
      r.onupgradeneeded = () => r.result.createObjectStore("projects");
      r.onsuccess = () => ok(r.result);
      r.onerror = () => no(r.error);
    });
    await new Promise((ok, no) => {
      const tx = db.transaction("projects", "readwrite");
      tx.objectStore("projects").put(p, "current");
      tx.oncomplete = ok;
      tx.onerror = no;
    });
    db.close();
  }, fixture());
  await page.goto("http://localhost:8086/editor/");
  await page.getByRole("tab", { name: "Scenes", exact: true }).click();
  await choose(page, "tile-scale", "1");
  await choose(page, "brush-palette", "blue");
  const pixel = async (x, y) =>
    page
      .locator("#view")
      .evaluate(
        (c, { x, y }) => [
          ...c.getContext("2d").getImageData(x * 8 + 4, y * 8 + 4, 1, 1).data,
        ],
        { x, y }
      );
  expect(await pixel(0, 0)).toEqual([255, 0, 0, 255]);
  await page.locator("#view").click({ position: { x: 16 * 8 + 4, y: 4 } });
  expect(await pixel(16, 0)).toEqual([0, 0, 255, 255]);
  await page.locator("#tool-recolor-control").click();
  await page.locator("#view").click({ position: { x: 4, y: 4 } });
  expect(await pixel(0, 0)).toEqual([0, 0, 255, 255]);
  await page.locator("#undo-control").click();
  expect(await pixel(0, 0)).toEqual([255, 0, 0, 255]);
  await page.locator("#redo-control").click();
  expect(await pixel(0, 0)).toEqual([0, 0, 255, 255]);
  await page.getByRole("tab", { name: "Palettes", exact: true }).click();
  await expect(page.locator("#palette-workspace")).toBeVisible();
  await expect(page.locator("#art-workspace")).toBeHidden();
  await choose(page, "preset", "builtin:2");
  await page.getByRole("tab", { name: "Scenes", exact: true }).click();
  expect(await pixel(0, 0)).toEqual([0, 0, 255, 255]);
  await page.locator("#file-save-now-control").click();
  await expect(page.locator("#status")).toContainText("Saved");
  await page.reload();
  await page.getByRole("tab", { name: "Scenes", exact: true }).click();
  await choose(page, "tile-scale", "1");
  expect(await pixel(0, 0)).toEqual([0, 0, 255, 255]);
  await page
    .getByRole("button", { name: "Import & export", exact: true })
    .click();
  const download = page.waitForEvent("download");
  await page.locator("#file-export-control").click();
  const file = await download;
  const stream = await file.createReadStream();
  const chunks = [];
  for await (const c of stream) chunks.push(c);
  const bytes = Buffer.concat(chunks);
  expect([...bytes.slice(0, 8)]).toEqual([137, 80, 78, 71, 13, 10, 26, 10]);
  expect(bytes.readUInt32BE(16)).toBe(160);
  expect(bytes.readUInt32BE(20)).toBe(144);
  const bounds = await page.locator("#view").boundingBox();
  await choose(page, "brush-palette", "red");
  await page.locator("#tool-recolor-control").click();
  await page.mouse.move(bounds.x + 4, bounds.y + 4);
  await page.mouse.down();
  await page.mouse.move(bounds.x + 132, bounds.y + 4, { steps: 16 });
  await page.mouse.up();
  expect(await pixel(0, 0)).toEqual([255, 0, 0, 255]);
  expect(await pixel(16, 0)).toEqual([255, 0, 0, 255]);
  await page.locator("#undo-control").click();
  expect(await pixel(0, 0)).toEqual([0, 0, 255, 255]);
  expect(await pixel(16, 0)).toEqual([0, 0, 255, 255]);
  await expect(page.locator("#pal-add")).toBeHidden();
  await page.getByRole("tab", { name: "Palettes", exact: true }).click();
  await page.locator("#pal-add-control").click();
  await page.locator("#pal-name-control").fill("New test palette");
  await page.locator("#hex-input-control").fill("#44BB88");
  await page.locator("#hex-apply-control").click();
  await page.screenshot({
    path: "/private/tmp/beaver-editor-palettes.png",
    fullPage: true,
  });
  await page.getByRole("tab", { name: "Tiles", exact: true }).click();
  await expect(page.locator("#pal-add")).toBeHidden();
  await choose(page, "brush-palette", { label: "New test palette" });
  await expect(page.locator("#tool-draw-control svg.lucide")).toHaveCount(1);
  await expect(page.locator("#erase")).toHaveAttribute("aria-label", "Eraser");
  await page.getByRole("tab", { name: "Sprites", exact: true }).click();
  await choose(page, "brush-palette", { label: "New test palette" });
  await page.getByRole("tab", { name: "Scenes", exact: true }).click();
  expect(errors).toEqual([]);
  await page.screenshot({
    path: "/private/tmp/beaver-editor-radix.png",
    fullPage: true,
  });
});

test("all editor form controls use Radix, including the asset popup and dialogs", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://localhost:8086/editor/");
  await expect(page.locator("#asset-list-control")).toContainText("New sprite");
  await page.locator("#asset-dup-control").click();
  await expect(page.locator("#asset-list-control")).toContainText(
    "New sprite copy"
  );
  await page.locator("#asset-list-control").click();
  await expect(page.getByRole("listbox")).toBeVisible();
  await expect(
    page.getByRole("option", { name: "New sprite", exact: true })
  ).toBeVisible();
  await page.getByRole("option", { name: "New sprite", exact: true }).click();
  await expect(page.locator("#asset-list-control")).toContainText("New sprite");
  await page.locator("#onion-control").click();
  await expect(page.locator("#onion-control")).toHaveAttribute(
    "data-state",
    "unchecked"
  );
  await expect(page.locator("#onion")).not.toBeChecked();
  await page.getByRole("tab", { name: "Palettes", exact: true }).click();
  await page.locator("#ch-r-control [role=slider]").focus();
  await page.keyboard.press("Home");
  await expect(page.locator("#ch-r-control [role=slider]")).toHaveAttribute(
    "aria-valuenow",
    "0"
  );
  await page.keyboard.press("ArrowRight");
  await expect(page.locator("#ch-r-control [role=slider]")).toHaveAttribute(
    "aria-valuenow",
    "1"
  );
  await page.locator("#hex-input-control").fill("wrong");
  await page.locator("#hex-apply-control").click();
  await expect(page.getByRole("alertdialog")).toBeVisible();
  await page.getByRole("button", { name: "OK", exact: true }).click();
  await page.getByRole("tab", { name: "Sprites", exact: true }).click();
  await page
    .getByRole("button", { name: "Import & export", exact: true })
    .click();
  await page.locator("#file-new-control").click();
  await expect(page.getByRole("alertdialog")).toBeVisible();
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(page.locator("#asset-list-control")).toContainText("New sprite");
  expect(
    await page
      .locator(".control-source")
      .evaluateAll((nodes) =>
        nodes.every((n) => getComputedStyle(n).display === "none")
      )
  ).toBe(true);
  expect(
    await page
      .locator("button:visible")
      .evaluateAll((nodes) =>
        nodes.every(
          (n) =>
            n.closest(".radix-themes") ||
            n.closest("#workspace-modes") ||
            n.classList.contains("inspector-trigger")
        )
      )
  ).toBe(true);
  expect(errors).toEqual([]);
});
