// Clip first, then rasterize: sparse/coalesced pointer events must not leave gaps
// or iterate thousands of off-artboard pixels when capture leaves the viewport.
export function* rasterLine(from, to, width, height) {
  if (width < 1 || height < 1) return;
  if(from.x>to.x || (from.x===to.x && from.y>to.y)) {
    yield* [...rasterLine(to,from,width,height)].reverse();return;
  }
  const dx = to.x - from.x, dy = to.y - from.y;
  let lo = 0, hi = 1;
  for (const [p,q] of [[-dx,from.x],[dx,width-1-from.x],[-dy,from.y],[dy,height-1-from.y]]) {
    if (!p) { if (q < 0) return; continue; }
    const t=q/p;
    if (p<0) lo=Math.max(lo,t); else hi=Math.min(hi,t);
    if (lo>hi) return;
  }
  const clamp=(n,max)=>Math.max(0,Math.min(max-1,Math.round(n)));
  let x=clamp(from.x+lo*dx,width),y=clamp(from.y+lo*dy,height);
  const endX=clamp(from.x+hi*dx,width),endY=clamp(from.y+hi*dy,height);
  const ax=Math.abs(endX-x),ay=-Math.abs(endY-y),sx=x<endX?1:-1,sy=y<endY?1:-1;
  let error=ax+ay;
  while(true) {
    yield {x,y};
    if(x===endX && y===endY)return;
    const twice=error*2;
    if(twice>=ay){error+=ay;x+=sx;}
    if(twice<=ax){error+=ax;y+=sy;}
  }
}
