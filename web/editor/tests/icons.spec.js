import { test, expect } from "@playwright/test";
import { readFileSync } from "node:fs";

test("one Lucide icon family across tools, controls, popups, animation and inspectors",async({page})=>{
  const errors=[];page.on("pageerror",e=>errors.push(e.message));
  const pkg=JSON.parse(readFileSync(new URL("../package.json",import.meta.url)));
  expect(pkg.dependencies["lucide-react"]).toBeTruthy();
  expect(pkg.dependencies["@radix-ui/react-icons"]).toBeUndefined();
  expect(readFileSync(new URL("../package-lock.json",import.meta.url),"utf8")).not.toContain('"node_modules/@radix-ui/react-icons"');
  await page.goto("http://localhost:8086/editor/");
  await expect(page.locator("#asset-list-control")).toContainText("New sprite",{timeout:15000});
  for(const [id,icon] of Object.entries({
    "tool-draw":"pencil","tool-fill":"paint-bucket","tool-pick":"pipette",
    "erase":"eraser","tool-select":"square-dashed","undo":"undo-2","redo":"redo-2",
    "asset-add":"plus","asset-dup":"copy","asset-delete":"trash-2",
    "zoom-in":"zoom-in","zoom-out":"zoom-out","file-save-now":"save","file-save":"download",
  })) {
    await expect(page.locator(`#${id}-control svg`)).toHaveClass(new RegExp(`lucide-${icon}(?:\\s|$)`));
    await expect(page.locator(`#${id}-control svg`)).toHaveAttribute("viewBox","0 0 24 24");
    await expect(page.locator(`#${id}-control svg`)).toHaveAttribute("aria-hidden","true");
  }
  await expect(page.locator(".inspector-chevron").first()).toHaveClass(/lucide-chevron-down/);
  await expect(page.locator("#asset-list-control svg")).toHaveClass(/lucide-chevron-down/);
  await expect(page.locator("#onion-control svg")).toHaveClass(/lucide-check/);
  await page.locator("#asset-list-control").click();
  await expect(page.getByRole("option",{name:"New sprite",exact:true}).locator("svg")).toHaveClass(/lucide-check/);
  await page.keyboard.press("Escape");
  await page.locator("#play-control").click();
  await expect(page.locator("#play-control svg")).toHaveClass(/lucide-square/);
  await page.locator("#play-control").click();
  await expect(page.locator("#play-control svg")).toHaveClass(/lucide-play/);
  await page.getByRole("tab",{name:"Scenes",exact:true}).click();
  await page.locator("#layer-tiles-control").click();
  await expect(page.locator("#tool-draw-control svg")).toHaveClass(/lucide-stamp/);
  await expect(page.locator("#tool-recolor-control svg")).toHaveClass(/lucide-paintbrush/);
  await page.locator("#tool-select-control").click();
  await expect(page.locator("#reflect-horizontal-control svg")).toHaveClass(/lucide-flip-horizontal-2/);
  await expect(page.locator("#reflect-left-right-control svg")).toHaveClass(/lucide-arrow-right/);
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  await expect(page.locator("#palette-clone-control svg")).toHaveClass(/lucide-copy-plus/);
  await expect(page.locator("#palette-drop-control svg")).toHaveClass(/lucide-trash-2/);
  await page.getByRole("tab",{name:"Palettes",exact:true}).click();
  await expect(page.locator("#pal-add-control svg")).toHaveClass(/lucide-plus/);
  await expect(page.locator("#pal-save-control svg")).toHaveClass(/lucide-bookmark-plus/);
  await page.getByRole("tab",{name:"Sprites",exact:true}).click();
  await page.locator("#tool-fill-control").click();
  await expect(page.locator("#tool-fill-control")).toHaveAttribute("aria-pressed","true");
  await page.mouse.move(0,0);
  await page.screenshot({path:"/private/tmp/beaver-editor-lucide-icons.png",fullPage:true});
  expect(errors).toEqual([]);
});
