import { test,expect } from '@playwright/test';
import { fixture } from './fixtures.js';

async function seed(page,{tileWidth=32,tileHeight=32}={}) {
  const project=fixture();
  for(const a of [project.sprites[0],project.tiles[0]]) {a.w=a.h=32;}
  project.sprites[0].frames[0]={id:'frame',w:32,h:32,pixels:Array(1024).fill(-1),tilePal:Array(16).fill(0)};
  project.tiles[0].w=tileWidth;project.tiles[0].h=tileHeight;
  project.tiles[0].image={w:tileWidth,h:tileHeight,pixels:Array(tileWidth*tileHeight).fill(0),tilePal:Array(tileWidth*tileHeight/64).fill(0)};
  project.scenes[0].pixels=Array(160*144).fill(-1);
  await page.route('**/__seed',r=>r.fulfill({contentType:'text/html',body:'<!doctype html><title>Gesture test</title>'}));
  await page.goto('http://localhost:8086/__seed');
  await page.evaluate(async p=>{
    const db=await new Promise((ok,no)=>{const r=indexedDB.open('beaver-gbc-art',1);r.onupgradeneeded=()=>r.result.createObjectStore('projects');r.onsuccess=()=>ok(r.result);r.onerror=()=>no(r.error);});
    await new Promise((ok,no)=>{const t=db.transaction('projects','readwrite');t.objectStore('projects').put(p,'current');t.oncomplete=ok;t.onerror=()=>no(t.error);});db.close();
  },project);
  await page.goto('http://localhost:8086/editor/');
  await expect(page.locator('#view')).toBeVisible();
  await expect(page.getByRole('button',{name:'Color 3',exact:true})).toBeVisible();
}
async function saved(page) {
  await page.locator('#file-save-now-control').click();
  await expect(page.locator('#status')).toContainText('Saved');
  return page.evaluate(async()=>{
    const db=await new Promise(ok=>{const r=indexedDB.open('beaver-gbc-art',1);r.onsuccess=()=>ok(r.result);});
    const p=await new Promise(ok=>{const r=db.transaction('projects').objectStore('projects').get('current');r.onsuccess=()=>ok(r.result);});db.close();return p;
  });
}
async function scale(page,label) {await page.locator('#tile-scale-control').click();await page.getByRole('option',{name:label,exact:true}).click();}
const pixels=(p,mode)=>mode==='Sprites'?p.sprites[0].frames[0].pixels:mode==='Tiles'?p.tiles[0].image.pixels:p.scenes[0].pixels;

for(const mode of ['Sprites','Tiles','Scenes'])for(const zoom of ['2×','3×'])test(`${mode} ${zoom}: fast stroke has no gaps, no page/trackpad scroll, one undo`,async({page})=>{
  await seed(page);await page.getByRole('tab',{name:mode,exact:true}).click();
  if(mode==='Scenes')await page.locator('#layer-pixels-control').click();
  await scale(page,zoom);await page.getByRole('button',{name:'Color 3',exact:true}).click();
  const view=page.locator('#view'),box=await view.boundingBox(),z=await view.evaluate(c=>c.width/(c.width>1000?160:32));
  const before=await page.evaluate(()=>({x:scrollX,y:scrollY,top:document.querySelector('#viewport').scrollTop,left:document.querySelector('#viewport').scrollLeft}));
  await page.mouse.move(box.x+1.5*z,box.y+2.5*z);await page.mouse.down();
  await page.mouse.move(box.x+14.5*z,box.y+2.5*z,{steps:1});
  await page.mouse.wheel(100,180);await page.waitForTimeout(80);
  expect(await page.evaluate(()=>({x:scrollX,y:scrollY,top:document.querySelector('#viewport').scrollTop,left:document.querySelector('#viewport').scrollLeft}))).toEqual(before);
  await page.mouse.up();
  const p=await saved(page),w=mode==='Scenes'?160:32;
  expect(pixels(p,mode).slice(w*2+1,w*2+15)).toEqual(Array(14).fill(3));
  await page.locator('#undo-control').click();
  expect(pixels(await saved(page),mode).slice(w*2+1,w*2+15)).toEqual(Array(14).fill(mode==='Tiles'?0:-1));
});

for(const width of [1440,1280,1024,768,540,390])test(`panels fit ${width}×800 without document overflow or clipped tabs`,async({page})=>{
  await page.setViewportSize({width,height:800});await seed(page);
  for(const mode of ['Sprites','Tiles','Scenes','Palettes']) {
    await page.getByRole('tab',{name:mode,exact:true}).click();
    expect(await page.evaluate(()=>({w:document.documentElement.scrollWidth,h:document.documentElement.scrollHeight}))).toEqual({w:width,h:800});
    for(const tab of await page.getByRole('tab').all()) {
      const box=await tab.boundingBox();expect(box.x).toBeGreaterThanOrEqual(0);expect(box.x+box.width).toBeLessThanOrEqual(width);
    }
    expect(await page.locator('.library').evaluate(n=>n.scrollWidth<=n.clientWidth)).toBe(true);
  }
});

test('pixel hover is not an 8×8 cell selection; neutral theme, Fit and explicit pan preserve artwork',async({page})=>{
  await seed(page);await scale(page,'3×');
  const view=page.locator('#view'),box=await view.boundingBox(),z=24;
  await page.mouse.move(box.x+3.5*z,box.y+3.5*z);
  expect(await view.evaluate(c=>[...c.getContext('2d').getImageData(1,1,1,1).data])).toEqual([17,17,20,255]);
  expect(await page.evaluate(()=>getComputedStyle(document.body).backgroundColor)).toBe('rgb(17, 17, 20)');
  expect(await page.locator('.mode-list [data-state=active]').evaluate(n=>getComputedStyle(n).backgroundColor)).toBe('rgb(73, 101, 212)');
  const before=pixels(await saved(page),'Sprites');
  await view.focus();await page.keyboard.down('Space');
  await page.mouse.move(box.x+200,box.y+150);await page.mouse.down();
  await page.mouse.move(box.x+100,box.y+60,{steps:5});await page.mouse.up();await page.keyboard.up('Space');
  expect(await page.locator('#viewport').evaluate(n=>n.scrollTop)).toBeGreaterThan(0);
  expect(pixels(await saved(page),'Sprites')).toEqual(before);
  await page.locator('#fit-view-control').click();
  await expect.poll(()=>page.locator('#viewport').evaluate(n=>({width:n.scrollWidth-n.clientWidth,height:n.scrollHeight-n.clientHeight,zoom:document.querySelector('#zoom-label').dataset.zoom}))).toEqual({width:0,height:0,zoom:expect.any(String)});
});

test('capture outside canvas and cancellation never leave painting/scroll locks stuck',async({page})=>{
  await seed(page);await scale(page,'2×');
  const view=page.locator('#view'),box=await view.boundingBox();
  await page.mouse.move(box.x+24,box.y+24);await page.mouse.down();
  await page.mouse.move(box.x+250,box.y+24,{steps:1});
  await view.dispatchEvent('pointercancel',{pointerId:1,pointerType:'mouse'});
  await page.mouse.up();await expect(page.locator('body')).not.toHaveClass(/canvas-gesture/);
  const after=pixels(await saved(page),'Sprites');
  await page.mouse.move(box.x+120,box.y+80);await page.mouse.move(box.x+180,box.y+80);
  expect(pixels(await saved(page),'Sprites')).toEqual(after);
  await page.mouse.move(box.x+200,box.y+100);await page.mouse.wheel(0,150);
  await expect.poll(()=>page.locator('#viewport').evaluate(n=>n.scrollTop)).toBeGreaterThan(0);
});

test('repeating preview drag wraps adjacent repeat pixels, groups undo and cannot scroll',async({page})=>{
  await seed(page);await page.getByRole('tab',{name:'Tiles',exact:true}).click();
  await page.getByRole('button',{name:'Color 3',exact:true}).click();
  const view=page.locator('#pattern-view'),box=await view.boundingBox(),ratio=box.width/160;
  await page.mouse.move(box.x+29.5*ratio,box.y+2.5*ratio);await page.mouse.down();
  await page.mouse.move(box.x+35.5*ratio,box.y+2.5*ratio,{steps:1});
  await page.mouse.wheel(0,200);await page.mouse.up();
  const tile=pixels(await saved(page),'Tiles');
  for(const x of [29,30,31,0,1,2,3])expect(tile[2*32+x]).toBe(3);
  expect(tile[2*32+16]).toBe(0);
  await page.locator('#undo-control').click();
  expect(pixels(await saved(page),'Tiles')).toEqual(Array(1024).fill(0));
});

test('narrow inspector and preview tabs keep every control reachable without page scroll',async({page})=>{
  await page.setViewportSize({width:390,height:800});await seed(page);
  await page.locator('#toggle-inspector-control').click();
  await expect(page.locator('.panel')).toBeVisible();
  await expect(page.locator('#width-control')).toBeVisible();
  await page.keyboard.press('Escape');await expect(page.locator('.panel')).toBeHidden();
  await page.getByRole('tab',{name:'Tiles',exact:true}).click();
  await page.getByRole('button',{name:'Palette preview',exact:true}).click();
  await expect(page.locator('#palette-clone-control')).toBeInViewport();
  await expect(page.locator('#palette-frame-palette-0-control')).toBeInViewport();
  await page.getByRole('button',{name:'Repeating preview',exact:true}).click();
  await expect(page.locator('#pattern-view')).toBeInViewport();
  expect(await page.evaluate(()=>scrollY)).toBe(0);
});

for(const [w,h,rotation] of [[16,16,0],[32,16,90]])test(`scene brush uses complete ${w}×${h} tile at ${rotation}° without overlapping drag stamps`,async({page})=>{
  await seed(page,{tileWidth:w,tileHeight:h});
  await page.getByRole('tab',{name:'Scenes',exact:true}).click();await scale(page,'1×');
  if(rotation)await page.locator(`#rotate-${rotation}-control`).click();
  const bw=rotation%180?h:w,bh=rotation%180?w:h;
  const box=await page.locator('#view').boundingBox(),z=8;
  await page.mouse.move(box.x+9.5*z,box.y+9.5*z);
  // The footprint reaches the tile edge, not the internal 8×8 hardware cell.
  expect(await page.locator('#view').evaluate((c,bw)=>[...c.getContext('2d').getImageData(bw*8-1,4*8,1,1).data],bw)).not.toEqual([0,0,0,255]);
  await page.mouse.down();await page.mouse.move(box.x+(bw*3+1.5)*z,box.y+9.5*z,{steps:1});await page.mouse.up();
  const cells=(await saved(page)).scenes[0].tiles;
  expect(Object.keys(cells)).toHaveLength(4*bw*bh/64);
  for(let y=0;y<bh/8;y++)for(let x=0;x<bw*4/8;x++)expect(cells[`${x},${y}`]).toMatchObject({sx:x%(bw/8),sy:y,rotation});
  await page.locator('#undo-control').click();
  expect(Object.keys((await saved(page)).scenes[0].tiles)).toHaveLength(1);
});

test.describe('native touch',()=>{
  test.use({hasTouch:true});
  test('touchpad/finger-style drag paints continuously and cancelled contact releases capture',async({page,browserName})=>{
    test.skip(browserName!=='chromium','Native touch injection uses Chromium DevTools, mouse gestures run in all engines.');
    await seed(page);await scale(page,'2×');
    await page.getByRole('button',{name:'Color 3',exact:true}).click();
    const box=await page.locator('#view').boundingBox(),cdp=await page.context().newCDPSession(page);
    const touch=(x,y)=>({x:box.x+(x+.5)*16,y:box.y+(y+.5)*16,radiusX:1,radiusY:1,force:1,id:1});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[touch(1,2)]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[touch(14,2)]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    expect(pixels(await saved(page),'Sprites').slice(65,79)).toEqual(Array(14).fill(3));
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[touch(4,4)]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});
    await expect(page.locator('body')).not.toHaveClass(/canvas-gesture/);
    expect(await page.evaluate(()=>({x:scrollX,y:scrollY,top:document.querySelector('#viewport').scrollTop}))).toEqual({x:0,y:0,top:0});
  });
});
