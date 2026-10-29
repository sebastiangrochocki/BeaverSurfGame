import {test,expect} from '@playwright/test';

test('local Open Sans weights and purple-300 actions are consistent throughout the editor',async({page,browserName})=>{
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://localhost:8086/editor/');
  await expect(page.locator('#tool-draw-control')).toBeVisible();
  await page.evaluate(async()=>{
    await Promise.all([400,500,600,700].map(weight=>document.fonts.load(`${weight} 12px "Open Sans"`)));
    await document.fonts.ready;
  });
  expect(await page.evaluate(()=>[400,500,600,700].every(weight=>document.fonts.check(`${weight} 12px "Open Sans"`)))).toBe(true);
  for(const locator of [page.locator('body'),page.locator('#tool-draw-control'),page.locator('#asset-name-control')])expect(await locator.evaluate(n=>getComputedStyle(n).fontFamily)).toContain('Open Sans');
  expect(await page.locator('body').evaluate(n=>getComputedStyle(n).fontWeight)).toBe('400');
  expect(await page.locator('#file-save-now-control').evaluate(n=>getComputedStyle(n).fontWeight)).toBe('600');
  for(const locator of [page.getByRole('link',{name:'Editor',exact:true}),page.locator('.mode-list [data-state=active]'),page.locator('#tool-draw-control')])expect(await locator.evaluate(n=>getComputedStyle(n).backgroundColor)).toBe('rgb(73, 101, 212)');
  await page.setViewportSize({width:1280,height:800});
  await expect(page.getByRole('button',{name:'Color 3',exact:true})).toBeVisible();
  await page.screenshot({path:`/private/tmp/beaver-editor-final-${browserName}-desktop.png`});
  await page.getByRole('tab',{name:'Tiles',exact:true}).click();
  await expect(page.locator('#palette-clone-control')).toBeVisible();
  await page.screenshot({path:`/private/tmp/beaver-editor-final-${browserName}-tiles.png`});
  await page.setViewportSize({width:390,height:800});
  await expect(page.getByRole('button',{name:'Palette preview',exact:true})).toBeVisible();
  await expect(page.locator('#palette-frame-palette-0-control')).toBeInViewport();
  await page.screenshot({path:`/private/tmp/beaver-editor-final-${browserName}-mobile.png`});
  expect(errors).toEqual([]);
});
