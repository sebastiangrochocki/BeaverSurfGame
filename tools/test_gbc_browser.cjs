/* Actual web emulator smoke check, on a separate origin/profile from user data.
 * Start `python3 -m http.server 8086 --directory web` first. */
const {chromium}=require('../web/editor/node_modules/playwright');
const {writeFileSync,readFileSync}=require('node:fs');
const assert=require('node:assert/strict');
(async()=>{
  const browser=await chromium.launch({headless:true});
  try {
    const context=await browser.newContext({viewport:{width:1280,height:1000}});
    // Retain the rendered WebGL buffer for pixel inspection; this does not
    // change the ROM/core or input and avoids reading a cleared GPU backbuffer.
    await context.addInitScript(()=>{
      const getContext=HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext=function(type,options){
        return getContext.call(this,type,/webgl/.test(type)?{...options,preserveDrawingBuffer:true}:options);
      };
    });
    const page=await context.newPage();
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto('http://localhost:8086/?sys=gbc');
    await page.locator('#game canvas').first().waitFor({state:'visible',timeout:120000});
    await page.waitForTimeout(6000);
    const canvas=page.locator('#game canvas').first();
    const image=async name=>writeFileSync(`/private/tmp/beaver-browser-${name}.png`,Buffer.from((await canvas.evaluate(c=>c.toDataURL('image/png'))).split(',')[1],'base64'));
    const rom=readFileSync(require('node:path').join(__dirname,'../web/beaver.gbc'));
    const noi=readFileSync(require('node:path').join(__dirname,'../gbc/build/beaver.noi'),'utf8');
    const fontAddress=parseInt(noi.match(/DEF _bg_tiles 0x([0-9a-f]+)/i)[1],16);
    const font=(fontAddress>>>16)*0x4000+(fontAddress&0x3fff);
    const expectedHeader=(text,center=true)=>{
      const mask=Array(160*8).fill(false),left=center?Math.floor((20-text.length)/2)*8:0;
      [...text].forEach((ch,i)=>{
        const tile=ch===' '?0:14+ch.charCodeAt(0)-65;
        for(let y=0;y<8;y++)for(let x=0;x<8;x++) {
          const lo=rom[font+tile*16+y*2],hi=rom[font+tile*16+y*2+1];
          mask[y*160+left+i*8+x]=(((lo>>>(7-x))&1)|(((hi>>>(7-x))&1)<<1))===1;
        }
      });return mask;
    };
    const headerMatches=async text=>{
      const actual=await canvas.evaluate(c=>{
        const copy=document.createElement('canvas');copy.width=c.width;copy.height=c.height;
        const cx=copy.getContext('2d');cx.drawImage(c,0,0);const pixels=cx.getImageData(0,0,c.width,c.height).data;
        return Array.from({length:160*8},(_,i)=>{
          const x=i%160,y=Math.floor(i/160),p=(Math.floor((y+.5)*c.height/144)*c.width+Math.floor((x+.5)*c.width/160))*4;
          return pixels[p]>180&&pixels[p+1]>180&&pixels[p+2]>180;
        });
      });const expected=expectedHeader(text);return actual.every((v,i)=>v===expected[i]);
    };
    console.log('Emulator canvas',await canvas.evaluate(c=>({width:c.width,height:c.height})));
    const flagMask=await page.evaluate(async url=>{
      const img=new Image();img.src=url;await img.decode();
      const c=document.createElement('canvas');c.width=c.height=16;
      const cx=c.getContext('2d');cx.drawImage(img,0,0);const p=cx.getImageData(0,0,16,16).data;
      return Array.from({length:256},(_,i)=>p[i*4]===0?'black':p[i*4]===255?'white':'background');
    },'data:image/png;base64,'+readFileSync(require('node:path').join(__dirname,'../gbc/art/progress_finish.png')).toString('base64'));
    let footerChecks=0;
    const checkFooter=async()=>{
      const frame=await canvas.evaluate(c=>{
        const copy=document.createElement('canvas');copy.width=c.width;copy.height=c.height;
        const cx=copy.getContext('2d');cx.drawImage(c,0,0);const p=cx.getImageData(0,0,c.width,c.height).data;
        const rgb=(x,y)=>{const i=(Math.floor((y+.5)*c.height/144)*c.width+Math.floor((x+.5)*c.width/160))*4;return [...p.slice(i,i+3)];};
        const flag=Array.from({length:256},(_,i)=>rgb(144+i%16,128+Math.floor(i/16)));
        const head=[];
        for(let col=0;col<16;col++){
          let ink=0;for(let y=0;y<8;y++)for(let x=0;x<8;x++)if(Math.max(...rgb(16+col*8+x,128+y))<45)ink++;
          if(ink)head.push(col);
        }
        return {flag,head};
      });
      for(let i=0;i<256;i++) {
        const rgb=frame.flag[i];
        if(flagMask[i]==='black')assert(Math.max(...rgb)<45,`flag flicker at check ${footerChecks}, pixel ${i}: ${rgb}`);
        if(flagMask[i]==='white')assert(Math.min(...rgb)>200,`flag white became HUD colors: ${rgb}`);
      }
      assert(frame.head.length===1,`missing/duplicate 8x8 beaver head: ${frame.head}`);
      footerChecks++;
      return frame.head[0];
    };
    await page.keyboard.press('Enter',{delay:100});await page.waitForTimeout(700);
    await image('beach');
    assert(await headerMatches('BEACH START'),'did not reach beach start');
    for(let i=0;i<12;i++){await page.waitForTimeout(35);await checkFooter();}
    await page.keyboard.down('ArrowRight');
    let inside=false;
    for(let i=0;i<80;i++) {
      await checkFooter();
      await page.keyboard.press('z',{delay:100});await page.waitForTimeout(80);
      if(await headerMatches('BEAVER SURF SHOP')) {inside=true;break;}
    }
    await page.keyboard.up('ArrowRight');assert(inside,'did not enter surf shop');
    await page.waitForTimeout(500);
    await image('shop');
    const fingerprints=()=>canvas.evaluate(c=>{
      const image=document.createElement('canvas');image.width=c.width;image.height=c.height;
      const ctx=image.getContext('2d');ctx.drawImage(c,0,0);
      const hash=bytes=>{let h=2166136261;for(const v of bytes)h=Math.imul(h^v,16777619);return h>>>0;};
      const hud=ctx.getImageData(0,c.height*128/144,c.width,c.height*16/144).data;
      const footer=Array.from({length:160*16},(_,i)=>{
        const x=i%160,y=Math.floor(i/160),p=(Math.floor((y+.5)*c.height/144)*c.width+Math.floor((x+.5)*c.width/160))*4;
        return hud[p]<100&&hud[p+1]<100&&hud[p+2]<100?'1':'0';
      }).join('');
      return {hud:hash(hud),footer,wall:hash(ctx.getImageData(0,c.height*20/144,c.width,3).data)};
    });
    const baseline=await fingerprints(),walls=new Set();
    const footerMasks=['WELCOME TO SURF SHOP','A TALK TO SHOPKEEPER'].map(text=>
      expectedHeader(text,false).concat(expectedHeader('WALK LEFT TO EXIT',false)).map(Number).join(''));
    assert(footerMasks.includes(baseline.footer),'initial shop dialogue has incorrect glyph pixels');
    await page.keyboard.down('ArrowRight');
    let moving=false;
    for(let i=0;i<80;i++) {
      await page.waitForTimeout(60);
      if((await fingerprints()).wall!==baseline.wall) {moving=true;break;}
    }
    assert(moving,'room camera never started scrolling');
    for(let i=0;i<16;i++) {
      await page.waitForTimeout(35);
      const frame=await fingerprints();walls.add(frame.wall);
      if(!footerMasks.includes(frame.footer))await image('shop-text-mismatch');
      assert(footerMasks.includes(frame.footer),`Browser emulator dialogue glyphs corrupted during scroll sample ${i}`);
    }
    await page.keyboard.up('ArrowRight');
    await page.waitForTimeout(250);
    await image('shop-scroll');
    assert(walls.size>1,'shop wall did not scroll');
    await page.keyboard.down('ArrowLeft');
    for(let i=0;i<120;i++) {
      await page.waitForTimeout(60);
      if(await headerMatches('BEACH START'))break;
    }
    await page.keyboard.up('ArrowLeft');
    assert(await headerMatches('BEACH START'),'did not leave shop onto beach');
    await page.keyboard.down('ArrowRight');
    for(let i=0;i<45;i++){await page.waitForTimeout(100);await checkFooter();}
    await page.keyboard.up('ArrowRight');
    assert(!await headerMatches('BEACH START'),'did not reach surfing');
    const headPositions=new Set();
    for(let i=0;i<40;i++){await page.waitForTimeout(125);headPositions.add(await checkFooter());}
    assert(headPositions.size>1,'beaver-head progress marker never advanced while surfing');
    await image('surfing-footer');
    assert(errors.every(message=>message.includes('Wake Lock')),errors.join('\n'));
    console.log(`Browser emulator: shop scroll/dialogue intact; ${footerChecks} exact black/white flag checks while idle, walking and surfing; one moving 8x8 beaver head; no game errors`);
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exit(1);});
