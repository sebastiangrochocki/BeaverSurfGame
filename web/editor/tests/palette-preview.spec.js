import { test, expect } from "@playwright/test";
import { fixture } from "./fixtures.js";

const origin="http://localhost:8086";
async function seed(page,project=fixture()) {
  await page.route("**/__seed",r=>r.fulfill({contentType:"text/html",body:"<!doctype html><title>Isolated preview test</title>"}));
  await page.goto(origin+"/__seed");
  await page.evaluate(async project=>{
    const db=await new Promise((resolve,reject)=>{
      const request=indexedDB.open("beaver-gbc-art",1);
      request.onupgradeneeded=()=>request.result.createObjectStore("projects");
      request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);
    });
    await new Promise((resolve,reject)=>{
      const tx=db.transaction("projects","readwrite");tx.objectStore("projects").put(project,"current");
      tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);
    });db.close();
  },project);
  await page.goto(origin+"/editor/");
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
}
async function saved(page) {
  return page.evaluate(async()=>{
    const db=await new Promise(resolve=>{const r=indexedDB.open("beaver-gbc-art",1);r.onsuccess=()=>resolve(r.result);});
    const project=await new Promise(resolve=>{const r=db.transaction("projects").objectStore("projects").get("current");r.onsuccess=()=>resolve(r.result);});
    db.close();return project;
  });
}
async function choose(page,id,label) {
  await page.locator(`#${id}-control`).click();
  await page.getByRole("option",{name:label,exact:true}).click();
}
async function pixel(page,id="palette-animation-view",x=0,y=0) {
  return page.locator(`#${id}`).evaluate((c,{x,y})=>[...c.getContext("2d").getImageData(x,y,1,1).data],{x,y});
}
async function bytesOf(download) {
  const chunks=[];for await(const chunk of await download.createReadStream())chunks.push(chunk);
  return Buffer.concat(chunks);
}
function withFoam() {
  const project=fixture();
  project.palettes.push({id:"foam",name:"Foam",colors:[[0,0,0],[31,31,31],[31,15,0],[31,31,31]]});
  return project;
}

test("Radix palette frames play and stop independently; artwork, PNG and backup stay linked",async({page})=>{
  const errors=[];page.on("pageerror",e=>errors.push(e.message));
  const project=withFoam();await seed(page,project);
  await expect(page.locator("#palette-animation-preview")).toBeVisible();
  await expect(page.locator("#palette-play-control")).toBeDisabled();
  expect(await pixel(page)).toEqual([255,0,0,255]);
  await page.locator("#palette-clone-control").click();
  await choose(page,"palette-frame-palette-1","Blue");
  await page.locator("#palette-clone-control").click();
  await choose(page,"palette-frame-palette-2","Foam");
  await expect(page.locator("#palette-frame-palette-2-control")).toHaveAttribute("role","combobox");
  await page.locator("#palette-fps-control").fill("4");
  await page.locator("#palette-animation-title").click();
  await page.locator("#file-save-now-control").click();
  await expect.poll(async()=> (await saved(page)).tiles[0].paletteAnimation.fps).toBe(4);
  const before=await saved(page);
  expect(before.tiles[0].image).toEqual(project.tiles[0].image);
  expect(before.tiles[0].palettes).toEqual(["red"]);
  expect(before.scenes).toEqual(project.scenes);
  expect(before.tiles[0].paletteAnimation.frames.map(f=>f.paletteId)).toEqual(["red","blue","foam"]);
  await page.locator("#palette-play-control").click();
  await expect(page.locator("#palette-play-control")).toHaveText("Stop");
  for(const id of ["red","blue","foam"])await expect.poll(()=>page.locator("#palette-animation-view").getAttribute("data-palette-id")).toBe(id);
  await page.locator("#palette-play-control").click();
  const stopped=await page.locator("#palette-animation-view").getAttribute("data-frame");
  await page.waitForTimeout(400);
  await expect(page.locator("#palette-animation-view")).toHaveAttribute("data-frame",stopped);
  expect(await saved(page)).toEqual(before);
  // Preview Blue while exporting: exported pixel remains artwork Red.
  await page.locator("#palette-frame-1-control").click();
  expect(await pixel(page)).toEqual([0,0,255,255]);
  expect(await pixel(page,"pattern-view")).toEqual([255,0,0,255]);
  await page.getByRole("button",{name:"Import & export",exact:true}).click();
  const pngEvent=page.waitForEvent("download");await page.locator("#file-export-control").click();
  const png=await bytesOf(await pngEvent);
  const exported=await page.evaluate(async bytes=>{
    const image=await createImageBitmap(new Blob([new Uint8Array(bytes)],{type:"image/png"}));
    const canvas=document.createElement("canvas");canvas.width=image.width;canvas.height=image.height;
    const cx=canvas.getContext("2d");cx.drawImage(image,0,0);
    return {w:image.width,h:image.height,pixel:[...cx.getImageData(0,0,1,1).data]};
  },[...png]);
  expect(exported).toEqual({w:16,h:8,pixel:[255,0,0,255]});
  const backupEvent=page.waitForEvent("download");await page.locator("#file-save-control").click();
  const backup=await bytesOf(await backupEvent);
  expect(JSON.parse(backup.toString()).tiles[0].paletteAnimation).toEqual(before.tiles[0].paletteAnimation);
  await page.locator("#palette-drop-control").click();
  await expect(page.locator(".palette-frame-row")).toHaveCount(2);
  await page.locator("#undo-control").click();await expect(page.locator(".palette-frame-row")).toHaveCount(3);
  await page.locator("#redo-control").click();await expect(page.locator(".palette-frame-row")).toHaveCount(2);
  await page.locator("#file-input").setInputFiles({name:"preview.gbc-art.json",mimeType:"application/json",buffer:backup});
  await expect(page.locator("#asset-list-control")).toContainText("Sprite");
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  await expect(page.locator(".palette-frame-row")).toHaveCount(3);
  await page.screenshot({path:"/private/tmp/beaver-palette-animation-desktop.png",fullPage:true});
  await page.reload();await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  await expect(page.locator(".palette-frame-row")).toHaveCount(3);
  await expect(page.locator("#palette-play-control")).toHaveText("Play");
  expect((await saved(page)).tiles[0].image).toEqual(project.tiles[0].image);
  await page.setViewportSize({width:680,height:1050});
  expect(await page.locator("#tile-previews").evaluate(n=>n.scrollWidth<=n.clientWidth)).toBe(true);
  await page.screenshot({path:"/private/tmp/beaver-palette-animation-mobile.png",fullPage:true});
  expect(errors).toEqual([]);
});

test("tile edits, undo, resize and palette colors/names sync into preview without stopping painting",async({page})=>{
  const errors=[];page.on("pageerror",e=>errors.push(e.message));
  await seed(page);
  await choose(page,"tile-scale","1×");
  await choose(page,"palette-frame-palette-0","Blue");
  await page.locator("#palette-clone-control").click();
  await page.locator("#palette-play-control").click();
  await page.getByRole("button",{name:"Color 3",exact:true}).click();
  const zoom=await page.locator("#view").evaluate(canvas=>canvas.width/16);
  await page.locator("#view").click({position:{x:zoom/2,y:zoom/2}});
  await expect(page.locator("#palette-play-control")).toHaveText("Stop");
  expect(await pixel(page)).toEqual([255,255,255,255]);
  expect(await pixel(page,"pattern-view")).toEqual([255,255,255,255]);
  await page.locator("#undo-control").click();expect(await pixel(page)).toEqual([0,0,255,255]);
  await page.locator("#redo-control").click();expect(await pixel(page)).toEqual([255,255,255,255]);
  await page.locator("#palette-fps-control").fill("12");
  await page.waitForTimeout(600); // a playback tick must not overwrite an in-progress edit
  await expect(page.locator("#palette-fps-control")).toHaveValue("12");
  await page.locator("#palette-animation-title").click();
  await expect.poll(async()=> (await saved(page)).tiles[0].paletteAnimation.fps).toBe(12);
  await page.getByRole("tab",{name:"Palettes",exact:true}).click();
  await expect(page.locator("#tile-previews")).toBeHidden();
  await choose(page,"brush-palette","Blue");
  await page.getByRole("button",{name:"Color 1",exact:true}).click();
  await page.locator("#hex-input-control").fill("#00FFFF");await page.locator("#hex-apply-control").click();
  await page.locator("#pal-name-control").fill("Sea cycle");await page.locator("#file-save-now-control").click();
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  await expect(page.locator("#palette-play-control")).toHaveText("Play");
  await expect(page.locator("#palette-frame-palette-0-control")).toContainText("Sea cycle");
  expect(await pixel(page,undefined,1,0)).toEqual([0,255,255,255]);
  expect(await pixel(page,"pattern-view",1,0)).toEqual([255,0,0,255]);
  await choose(page,"size","16×16");
  expect(await pixel(page,undefined,0,8)).toEqual([0,0,0,255]);
  await page.locator("#palette-play-control").click();
  await page.getByRole("tab",{name:"Sprites",exact:true}).click();
  await expect(page.locator("#tile-previews")).toBeHidden();
  await page.locator("#clone-control").click();await page.locator("#play-control").click();
  await expect(page.locator("#play-control")).toHaveText("Stop");
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  await expect(page.locator("#palette-play-control")).toHaveText("Play");
  await page.locator("#palette-drop-control").click();
  await page.locator("#palette-clone-control").click();
  await page.locator("#palette-play-control").click();
  await page.locator("#undo-control").click(); // removes the cloned second frame
  await expect(page.locator("#palette-play-control")).toHaveText("Play");
  await expect(page.locator("#palette-play-control")).toBeDisabled();
  await page.locator("#redo-control").click();
  await expect(page.locator("#palette-play-control")).toBeEnabled();
  expect(errors).toEqual([]);
});

test("preview settings are per tile, stable by palette ID, and protect palette references",async({page})=>{
  const project=withFoam();
  project.palettes=[project.palettes[0],project.palettes[2],project.palettes[1]];
  project.tiles.push({...structuredClone(project.tiles[0]),id:"second",name:"Second tile"});
  await seed(page,project);
  await choose(page,"palette-frame-palette-0","Blue");
  await choose(page,"asset-list","Second tile");
  expect(await pixel(page)).toEqual([255,0,0,255]);
  await page.locator("#palette-clone-control").click();await choose(page,"palette-frame-palette-1","Foam");
  await page.getByRole("tab",{name:"Palettes",exact:true}).click();
  await choose(page,"brush-palette","Foam");await page.locator("#pal-del-control").click();
  await expect(page.getByRole("alertdialog")).toContainText("palette-preview frames");
  await page.getByRole("button",{name:"OK",exact:true}).click();
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  expect(await pixel(page)).toEqual([0,0,255,255]);
  await expect(page.locator(".palette-frame-row")).toHaveCount(1);
  await choose(page,"asset-list","Second tile");await expect(page.locator(".palette-frame-row")).toHaveCount(2);
  await choose(page,"palette-frame-palette-1","Red");
  await page.getByRole("tab",{name:"Palettes",exact:true}).click();
  await choose(page,"brush-palette","Foam");await page.locator("#pal-del-control").click();
  await expect(page.getByRole("alertdialog")).toHaveCount(0);
  await page.getByRole("tab",{name:"Tiles",exact:true}).click();
  expect(await pixel(page)).toEqual([0,0,255,255]);
  await page.locator("#file-save-now-control").click();
  await expect.poll(async()=> (await saved(page)).palettes.map(p=>p.id)).toEqual(["red","blue"]);
  expect((await saved(page)).tiles[0].paletteAnimation.frames[0].paletteId).toBe("blue");
});

test("read-only tabs can preview but cannot change saved palette frames",async({page})=>{
  const project=fixture();
  project.tiles[0].paletteAnimation={fps:4,frames:[{id:"a",paletteId:"red"},{id:"b",paletteId:"blue"}]};
  await seed(page,project);
  const before=await saved(page);
  const other=await page.context().newPage();
  try {
    await other.goto(origin+"/editor/");
    await expect(other.locator("#status")).toContainText("Read-only");
    await other.getByRole("tab",{name:"Tiles",exact:true}).click();
    for(const id of ["palette-clone","palette-drop","palette-fps","palette-frame-palette-0"])
      await expect(other.locator(`#${id}-control`)).toBeDisabled();
    await other.locator("#palette-play-control").click();
    await expect(other.locator("#palette-play-control")).toHaveText("Stop");
    await expect.poll(()=>other.locator("#palette-animation-view").getAttribute("data-palette-id")).toBe("blue");
    await other.locator("#palette-frame-0-control").click();
    await expect(other.locator("#palette-play-control")).toHaveText("Play");
    expect(await saved(page)).toEqual(before);
  } finally { await other.close(); }
});
