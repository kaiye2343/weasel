/* Approximate browser preview geometry in 96-DPI logical pixels. Native text metrics remain authoritative. */
(function(root){
"use strict";
function layout(style,layers,candidates,annotations,measure){
 const font=style.fontPoint*4/3,comment=style.commentFontPoint*4/3,gap=2;
 const label=(i)=>String(i+1)+". ";
 const cells=candidates.map((c,i)=>({text:c.text,comment:annotations?(c.comment||""):"",label:label(i)}));
 const has=annotations&&style.annotationPosition!=="hidden"&&cells.some(c=>c.comment),above=style.annotationPosition==="above";
 let x=style.paddingLeft+4;const rowTop=style.paddingTop+4,commentHeight=has?comment*1.3:0,textHeight=font*1.4;
 for(const c of cells){c.labelWidth=measure(c.label,font);c.textWidth=measure(c.text,font);c.commentWidth=has?measure(c.comment,comment):0;const tw=c.labelWidth+c.textWidth;c.width=above?Math.max(tw,c.commentWidth):tw+(c.commentWidth?gap+c.commentWidth:0);c.x=x;c.textX=x+(above?(c.width-tw)/2:0);c.commentX=above?x+(c.width-c.commentWidth)/2:x+tw+gap;c.textY=rowTop+(has&&above?commentHeight+gap:0);c.commentY=rowTop;c.height=above?textHeight+(has?commentHeight+gap:0):Math.max(textHeight,has?commentHeight:0);x+=c.width+style.candidateSpacing;}
 const width=x-(cells.length?style.candidateSpacing:0)+style.paddingRight+4,height=rowTop+(above?textHeight+(has?commentHeight+gap:0):Math.max(textHeight,has?commentHeight:0))+style.paddingBottom+4;
 let left=0,top=0,right=width,bottom=height;
 const rects=layers.map((l,i)=>{const r={...l,index:i,left:(l.anchor.endsWith("right")?width:0)+(l.x||0),top:(l.anchor.startsWith("bottom")?height:0)+(l.y||0)};left=Math.min(left,r.left);top=Math.min(top,r.top);right=Math.max(right,r.left+l.width);bottom=Math.max(bottom,r.top+l.height);return r;});
 return {width,height,left,top,right,bottom,cells,layers:rects,hasAnnotation:has,above,commentHeight,rowTop,font,comment,gap};
}
function fit(rect,image){const scale=Math.min(rect.width/image.width,rect.height/image.height),width=Math.floor(image.width*scale),height=Math.floor(image.height*scale);return {x:rect.left+(rect.width-width)/2,y:rect.top+rect.height-height,width,height};}
function hit(g,x,y){return g.cells.findIndex(c=>x>=c.x&&x<=c.x+c.width&&y>=g.rowTop&&y<=g.rowTop+c.height);}
const api={layout,fit,hit};if(typeof module!=="undefined")module.exports=api;else root.SkinGeometry=api;
})(globalThis);
