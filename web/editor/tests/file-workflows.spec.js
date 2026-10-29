import {test,expect} from '@playwright/test';
import {fixture} from './fixtures.js';

async function seed(page) {
  await page.route('**/__seed',r=>r.fulfill({contentType:'text/html',body:'<!doctype html><title>File workflow test</title>'}));
  await page.goto('http://localhost:8086/__seed');
  await page.evaluate(async project=>{
    const db=await new Promise(ok=>{const r=indexedDB.open('beaver-gbc-art',1);r.onupgradeneeded=()=>r.result.createObjectStore('projects');r.onsuccess=()=>ok(r.result);});
    await new Promise(ok=>{const tx=db.transaction('projects','readwrite');tx.objectStore('projects').put(project,'current');tx.oncomplete=ok;});db.close();
  },fixture());
  await page.goto('http://localhost:8086/editor/');
  await expect(page.locator('#clone-control')).toBeVisible();
  await page.getByRole('button',{name:'Import & export',exact:true}).click();
}
async function saved(page) {
  await page.locator('#file-save-now-control').click();
  return page.evaluate(async()=>{
    const db=await new Promise(ok=>{const r=indexedDB.open('beaver-gbc-art',1);r.onsuccess=()=>ok(r.result);});
    const result=await new Promise(ok=>{const r=db.transaction('projects').objectStore('projects').get('current');r.onsuccess=()=>ok(r.result);});db.close();return result;
  });
}
async function png(page,colors=2,width=16,height=8) {
  const url=await page.evaluate(({colors,width,height})=>{
    const c=document.createElement('canvas');c.width=width;c.height=height;
    const x=c.getContext('2d');x.fillStyle='#ff0000';x.fillRect(0,0,8,8);x.fillStyle='#00ff00';x.fillRect(8,0,8,8);
    x.clearRect(0,0,1,1);x.fillStyle='#000000';x.fillRect(1,0,1,1);
    if(colors>3){x.fillStyle='#ffffff';x.fillRect(2,0,1,1);x.fillStyle='#0000ff';x.fillRect(3,0,1,1);}
    return c.toDataURL();
  },{colors,width,height});
  return {name:'strip.png',mimeType:'image/png',buffer:Buffer.from(url.split(',')[1],'base64')};
}
async function upload(page,button,file) {
  const chooser=page.waitForEvent('filechooser');await page.locator(`#${button}-control`).click();
  await (await chooser).setFiles(file);
}
test('new project resets active/playback frames before cloning or painting',async({page})=>{
  const errors=[];page.on('pageerror',e=>errors.push(e.message));await seed(page);
  await page.locator('#clone-control').click();await page.locator('#play-control').click();
  await page.locator('#file-new-control').click();await page.getByRole('button',{name:'Create project',exact:true}).click();
  await expect(page.getByRole('tab',{name:'Sprites',exact:true})).toHaveAttribute('data-state','active');
  await expect(page.locator('#art-workspace')).toBeVisible();
  await expect(page.locator('#frames button:not(.control-source)')).toHaveCount(1);
  await page.locator('#clone-control').click();
  const p=await saved(page);expect(p.sprites[0].frames).toHaveLength(2);
  expect(p.sprites[0].frames[0].pixels).toEqual(p.sprites[0].frames[1].pixels);
  await page.waitForTimeout(200);expect(errors).toEqual([]);
});
test('new and deleted sprite assets reset the active animation frame',async({page})=>{
  const errors=[];page.on('pageerror',e=>errors.push(e.message));await seed(page);
  await page.locator('#clone-control').click();await page.locator('#asset-add-control').click();
  await page.locator('#clone-control').click();
  let p=await saved(page);expect(p.sprites[1].frames).toHaveLength(2);
  await page.locator('#clone-control').click();await page.locator('#asset-delete-control').click();
  await page.locator('#clone-control').click();p=await saved(page);
  expect(p.sprites).toHaveLength(1);expect(p.sprites[0].frames).toHaveLength(3);expect(errors).toEqual([]);
});
test('PNG chooser imports transparent sprite strips, preserves other assets, supports undo/redo and reload',async({page})=>{
  const errors=[];page.on('pageerror',e=>errors.push(e.message));await seed(page);
  const before=await saved(page);
  await upload(page,'file-import',await png(page));
  await expect(page.locator('#frames button:not(.control-source)')).toHaveCount(2);
  const p=await saved(page),s=p.sprites[0];
  expect(s.frames.map(f=>[f.w,f.h])).toEqual([[8,8],[8,8]]);
  expect(s.frames[0].pixels[0]).toBe(-1);expect(s.frames[0].pixels[1]).toBe(1);
  expect(p.palettes.find(p=>p.id===s.palettes[0]).colors).toEqual([[0,0,0],[0,0,0],[31,0,0],[0,31,0]]);
  expect(p.tiles).toEqual(before.tiles);expect(p.scenes).toEqual(before.scenes);
  await page.locator('#undo-control').click();expect((await saved(page)).sprites).toEqual(before.sprites);
  await page.locator('#redo-control').click();expect((await saved(page)).sprites).toEqual(p.sprites);
  await page.reload();await expect(page.locator('#frames button:not(.control-source)')).toHaveCount(2);
  expect(errors).toEqual([]);
});
test('unsupported PNG is rejected without changing artwork; same file can be selected again',async({page})=>{
  await seed(page);const before=await saved(page),invalid=await png(page,5);
  for(let i=0;i<2;i++) {
    await upload(page,'file-import',invalid);await expect(page.getByRole('alertdialog')).toContainText('at most 3');
    await page.getByRole('button',{name:'OK',exact:true}).click();
    expect(await saved(page)).toEqual(before);
  }
  await upload(page,'file-import',{name:'broken.png',mimeType:'image/png',buffer:Buffer.from('not a PNG')});
  await expect(page.getByRole('alertdialog')).toContainText('valid PNG');
  await page.getByRole('button',{name:'OK',exact:true}).click();expect(await saved(page)).toEqual(before);
});
test('project import resets stale frame/mode state and rejects empty projects without replacing saved data',async({page})=>{
  const errors=[];page.on('pageerror',e=>errors.push(e.message));await seed(page);
  await page.locator('#clone-control').click();await page.getByRole('tab',{name:'Tiles',exact:true}).click();
  await upload(page,'file-open',{name:'project.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(fixture()))});
  await expect(page.getByRole('tab',{name:'Sprites',exact:true})).toHaveAttribute('data-state','active');
  await page.locator('#clone-control').click();const before=await saved(page);expect(before.sprites[0].frames).toHaveLength(2);
  await upload(page,'file-open',{name:'empty.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify({...fixture(),sprites:[]}))});
  await expect(page.getByRole('alertdialog')).toContainText('Project needs');
  await page.getByRole('button',{name:'OK',exact:true}).click();expect(await saved(page)).toEqual(before);expect(errors).toEqual([]);
});

for(const mode of ['Tiles','Scenes'])test(`PNG imports into ${mode} without changing other assets and undo restores palette links`,async({page})=>{
  const errors=[];page.on('pageerror',e=>errors.push(e.message));await seed(page);
  await page.getByRole('tab',{name:mode,exact:true}).click();const before=await saved(page);
  await upload(page,'file-import',await png(page,2,mode==='Scenes'?160:16,mode==='Scenes'?144:8));
  await expect.poll(async()=> (await saved(page)).palettes.length).toBe(before.palettes.length+1);
  const p=await saved(page);expect(p.sprites).toEqual(before.sprites);
  if(mode==='Tiles') {
    expect(p.scenes).toEqual(before.scenes);expect(p.tiles[0].image.pixels[0]).toBe(-1);
    await page.getByRole('tab',{name:'Scenes',exact:true}).click();
    await expect(page.locator('#view')).toBeVisible(); // Linked placement still renders.
    await page.getByRole('tab',{name:'Tiles',exact:true}).click();
  } else {
    expect(p.tiles).toEqual(before.tiles);expect(p.scenes[0].tiles).toEqual(before.scenes[0].tiles);
    expect(p.scenes[0].pixels[0]).toBe(-1);expect(p.scenes[0].pixelPalettes['0,0']).toBe(p.palettes.at(-1).id);
  }
  await page.locator('#undo-control').click();expect(await saved(page)).toEqual(before);expect(errors).toEqual([]);
});
test('PNG cannot resize a tile used by scene placements',async({page})=>{
  await seed(page);await page.getByRole('tab',{name:'Tiles',exact:true}).click();
  const before=await saved(page);await upload(page,'file-import',await png(page,2,8,8));
  await expect(page.getByRole('alertdialog')).toContainText('linked tile');
  await page.getByRole('button',{name:'OK',exact:true}).click();expect(await saved(page)).toEqual(before);
});
