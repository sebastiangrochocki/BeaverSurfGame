// Convert decoded PNG pixels into the editor's indexed, per-8×8 palette model.
// Reject unsupported art rather than silently recoloring or overwriting it.
export function indexedPng({width,height,data},{sprite=false,frameWidth=width,frameHeight=height}={}) {
  if(!Number.isInteger(width) || !Number.isInteger(height) || width<8 || height<8 || width>1024 || height>1024 || width%8 || height%8)
    throw new Error('PNG dimensions must be multiples of 8, between 8 and 1024 pixels.');
  if(data.length!==width*height*4)throw new Error('Incomplete PNG pixel data.');
  const strip=sprite && height===frameHeight && width%frameWidth===0;
  const w=strip?frameWidth:width,h=strip?frameHeight:height,count=width/w;
  const palettes=[],images=[],limit=sprite?3:4;
  const colorAt=i=>data[i+3]<128?null:[data[i],data[i+1],data[i+2]].map(v=>Math.round(v*31/255)).join(',');
  for(let frame=0;frame<count;frame++) {
    const image={w,h,pixels:Array(w*h).fill(-1),tilePal:Array(w*h/64).fill(0)};
    for(let cy=0;cy<h;cy+=8)for(let cx=0;cx<w;cx+=8) {
      const colors=new Set();
      for(let y=0;y<8;y++)for(let x=0;x<8;x++) {
        const color=colorAt(((cy+y)*width+frame*w+cx+x)*4);
        if(color!==null)colors.add(color);
      }
      if(colors.size>limit)throw new Error(`Each 8×8 ${sprite?'sprite':'tile'} cell supports at most ${limit} visible colors. Reduce the PNG colors first.`);
      let p=palettes.findIndex(existing=>new Set([...existing,...colors]).size<=limit);
      if(p<0) {
        if(palettes.length===8)throw new Error('PNG needs more than eight hardware palettes. Reduce the PNG colors first.');
        p=palettes.length;palettes.push([]);
      }
      for(const color of colors)if(!palettes[p].includes(color))palettes[p].push(color);
      image.tilePal[(cy/8)*(w/8)+cx/8]=p;
      for(let y=0;y<8;y++)for(let x=0;x<8;x++) {
        const color=colorAt(((cy+y)*width+frame*w+cx+x)*4);
        if(color!==null)image.pixels[(cy+y)*w+cx+x]=palettes[p].indexOf(color)+(sprite?1:0);
      }
    }
    images.push(image);
  }
  return {w,h,images,palettes:palettes.map(colors=>{
    const values=colors.map(color=>color.split(',').map(Number));
    if(sprite)values.unshift([0,0,0]); // GBC object color zero is transparent.
    while(values.length<4)values.push([0,0,0]);
    return values;
  })};
}
