import { test, expect } from "@playwright/test";
import { fixture } from "./fixtures.js";
import { readFileSync } from "node:fs";

async function seed(page, project) {
  await page.route("**/__seed", r => r.fulfill({contentType:"text/html",body:"<!doctype html><title>Reflection setup</title>"}));
  await page.goto("http://localhost:8086/__seed");
  await page.evaluate(async project => {
    const db=await new Promise((resolve,reject)=>{
      const r=indexedDB.open("beaver-gbc-art",1);
      r.onupgradeneeded=()=>r.result.createObjectStore("projects");
      r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);
    });
    await new Promise((resolve,reject)=>{
      const tx=db.transaction("projects","readwrite");
      tx.objectStore("projects").put(project,"current");
      tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);
    });
    db.close();
  },project);
  await page.goto("http://localhost:8086/editor/");
}

async function saved(page) {
  return page.evaluate(async()=>{
    const db=await new Promise(resolve=>{const r=indexedDB.open("beaver-gbc-art",1);r.onsuccess=()=>resolve(r.result);});
    const project=await new Promise(resolve=>{const r=db.transaction("projects").objectStore("projects").get("current");r.onsuccess=()=>resolve(r.result);});
    db.close();return project;
  });
}

async function select(page,w,h) {
  const view=page.locator("#view");
  await page.locator("#tool-select-control").click();
  await view.scrollIntoViewIfNeeded();
  const box=await view.boundingBox(),z=await view.evaluate((c,width)=>c.width/width,w);
  await page.mouse.move(box.x+.5*z,box.y+.5*z);await page.mouse.down();
  await page.mouse.move(box.x+(w-.5)*z,box.y+(h-.5)*z);await page.mouse.up();
}

for (const mode of ["sprite","tile","scene"]) {
  test(`${mode}: separate flips and mirror clones, Radix controls, palettes, undo/redo and reload`,async({page})=>{
    const errors=[];page.on("pageerror",e=>errors.push(e.message));
    const p=fixture();
    const pixels=Array.from({length:256},(_,i)=>i%7===0 && mode!=="tile"?-1:(Math.floor(i/16)*3+i%16)%4);
    const image={w:16,h:16,pixels,tilePal:[0,1,1,0]};
    Object.assign(p.sprites[0],{w:16,h:16,palettes:["red","blue"],frames:[{...image,id:"frame"},{...image,id:"other"}]});
    Object.assign(p.tiles[0],{w:16,h:16,palettes:["red","blue"],image});
    Object.assign(p.scenes[0],{w:16,h:16,pixels,pixelPalettes:{"0,0":"red","1,0":"blue","0,1":"blue","1,1":"red"}});
    await seed(page,p);
    await page.getByRole("tab",{name:mode==="sprite"?"Sprites":mode==="tile"?"Tiles":"Scenes",exact:true}).click();
    if(mode==="scene")await page.locator("#layer-pixels-control").click();
    await page.locator("#tile-scale-control").click();await page.getByRole("option",{name:"1×",exact:true}).click();
    await page.locator("#tool-select-control").click();
    await expect(page.locator("#reflect-horizontal-control")).toBeDisabled();
    await select(page,16,16);
    const art=q=>mode==="sprite"?q.sprites[0].frames[0]:mode==="tile"?q.tiles[0].image:q.scenes[0];
    for (const dir of ["horizontal","vertical","left-right","right-left","top-bottom","bottom-top"]) {
      const button=page.locator(`#reflect-${dir}-control`);
      await expect(button).toBeEnabled();
      expect(await button.evaluate(n=>!!n.closest(".radix-themes"))).toBe(true);
      await button.click();
      const expected=pixels.slice(),horizontal=["horizontal","left-right","right-left"].includes(dir);
      const clone=dir!=="horizontal" && dir!=="vertical",forward=dir==="left-right"||dir==="top-bottom";
      for(let y=0;y<16;y++)for(let x=0;x<16;x++){
        const a=horizontal?x:y;
        if(!clone || (forward?a<15-a:a>15-a))expected[(horizontal?y:15-y)*16+(horizontal?15-x:x)]=pixels[y*16+x];
      }
      await expect.poll(async()=>art(await saved(page)).pixels).toEqual(expected);
      const q=await saved(page);
      const pals=!clone?[1,0,0,1]:dir==="left-right"?[0,0,1,1]:dir==="right-left"?[1,1,0,0]:dir==="top-bottom"?[0,1,0,1]:[1,0,1,0];
      if(mode==="scene")expect(q.scenes[0].pixelPalettes).toEqual(Object.fromEntries(["0,0","1,0","0,1","1,1"].map((key,i)=>[key,pals[i]?"blue":"red"])));
      else expect(art(q).tilePal).toEqual(pals);
      if(mode==="sprite")expect(q.sprites[0].frames[1].pixels).toEqual(pixels);
      if(mode==="scene")expect(q.scenes[0].tiles).toEqual(p.scenes[0].tiles);
      await page.locator("#undo-control").click();
      await expect.poll(async()=>art(await saved(page)).pixels).toEqual(pixels);
      await page.locator("#redo-control").click();
      await expect.poll(async()=>art(await saved(page)).pixels).toEqual(expected);
      await page.locator("#undo-control").click();
      await expect.poll(async()=>art(await saved(page)).pixels).toEqual(pixels);
    }
    await page.locator("#reflect-horizontal-control").click();
    await page.locator("#reflect-horizontal-control").click();
    await expect.poll(async()=>art(await saved(page)).pixels).toEqual(pixels);
    await page.locator("#reflect-horizontal-control").click();
    await page.locator("#file-save-now-control").click();
    const before=await saved(page);
    await page.screenshot({path:`/private/tmp/beaver-reflection-${mode}.png`,fullPage:true});
    await page.reload();expect(art(await saved(page))).toEqual(art(before));
    expect(errors).toEqual([]);
  });
}

test("scene tile layer: reflected source pixels stay linked and render flipped, undo and reload",async({page})=>{
  const p=fixture();
  p.scenes[0].w=p.scenes[0].h=16;p.scenes[0].pixels=Array(256).fill(-1);
  p.tiles[0].image.pixels=Array.from({length:128},(_,i)=>(Math.floor(i/16)+i%16)%4);
  await seed(page,p);
  await page.getByRole("tab",{name:"Scenes",exact:true}).click();
  await page.locator("#layer-tiles-control").click();
  await page.locator("#tile-scale-control").click();await page.getByRole("option",{name:"1×",exact:true}).click();
  await select(page,16,16);
  await page.locator("#reflect-horizontal-control").click();
  await expect.poll(async()=>(await saved(page)).scenes[0].tiles["1,0"]).toEqual({...p.scenes[0].tiles["0,0"],flipX:true});
  await page.locator("#reflect-vertical-control").click();
  await expect.poll(async()=>(await saved(page)).scenes[0].tiles["1,1"]).toEqual({...p.scenes[0].tiles["0,0"],flipX:true,flipY:true});
  const view=page.locator("#view");
  const z=await view.evaluate(c=>c.width/16);
  const pixel=async(x,y)=>view.evaluate((c,{x,y,z})=>[...c.getContext("2d").getImageData((x+.5)*z,(y+.5)*z,1,1).data],{x,y,z});
  expect(await pixel(13,12)).toEqual([255,0,0,255]);
  await page.locator("#undo-control").click();
  await expect.poll(async()=>(await saved(page)).scenes[0].tiles["1,1"]).toBeUndefined();
  await page.locator("#redo-control").click();
  await page.locator("#file-save-now-control").click();
  const before=await saved(page);await page.reload();expect((await saved(page)).scenes[0]).toEqual(before.scenes[0]);
});

test("supplied back-walk sprite: horizontal flip moves the only board to the opposite side, not both",async({page})=>{
  const png=readFileSync(new URL("../../../gbc/art/walk_back.png",import.meta.url)).toString("base64");
  const pixels=await page.evaluate(async url=>{
    const img=new Image();img.src=url;await img.decode();
    const c=document.createElement("canvas");c.width=c.height=32;
    const cx=c.getContext("2d");cx.drawImage(img,0,0);
    const data=cx.getImageData(0,0,32,32).data;
    return Array.from({length:1024},(_,i)=>!data[i*4+3]?-1:!data[i*4]?0:data[i*4+1]?1:2);
  },`data:image/png;base64,${png}`);
  const p=fixture();
  p.palettes[0].colors=[[0,0,0],[19,11,7],[19,0,6],[31,31,31]];
  Object.assign(p.sprites[0],{w:32,h:32,frames:[{id:"frame",w:32,h:32,pixels,tilePal:Array(16).fill(0)}]});
  await seed(page,p);
  await page.locator("#tile-scale-control").click();await page.getByRole("option",{name:"1×",exact:true}).click();
  await select(page,32,32);
  await page.locator("#reflect-horizontal-control").click();
  const expected=Array.from({length:1024},(_,i)=>pixels[Math.floor(i/32)*32+31-i%32]);
  await expect.poll(async()=>(await saved(page)).sprites[0].frames[0].pixels).toEqual(expected);
  await page.screenshot({path:"/private/tmp/beaver-flip-real-sprite.png",fullPage:true});
  await page.locator("#reflect-horizontal-control").click();
  await expect.poll(async()=>(await saved(page)).sprites[0].frames[0].pixels).toEqual(pixels);
});
