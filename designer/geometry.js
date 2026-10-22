/* Approximate browser preview geometry in 96-DPI logical pixels. Native text metrics remain authoritative. */
(function(root){
"use strict";
function preferences(value={}){
 if(!value||typeof value!=="object"||Array.isArray(value)||Object.keys(value).some(k=>!["schemaVersion","annotationEnabled","annotationPosition","emptyAnnotationMode","fontMode","fontPoint","commentFontPoint","density","candidatePlacement","candidateOffsetX","candidateGap","avoidDecorations"].includes(k)))throw Error("显示偏好格式无效");
 const p={schemaVersion:2,annotationEnabled:true,annotationPosition:"above",emptyAnnotationMode:"collapse",fontMode:"user",fontPoint:12,commentFontPoint:10,density:"compact",candidatePlacement:"auto",candidateOffsetX:0,candidateGap:6,avoidDecorations:true,...value};
 if(p.schemaVersion===1){p.schemaVersion=2;p.fontMode="skin";p.density="skin";p.annotationEnabled=p.annotationPosition!=="hidden";if(!p.annotationEnabled)p.annotationPosition="above";}
 if(p.schemaVersion!==2||typeof p.annotationEnabled!=="boolean"||!["skin","above","beside"].includes(p.annotationPosition)||!["collapse","reserve"].includes(p.emptyAnnotationMode)||!["user","skin"].includes(p.fontMode)||!["compact","standard","skin"].includes(p.density)||!Number.isInteger(p.fontPoint)||p.fontPoint<8||p.fontPoint>48||!Number.isInteger(p.commentFontPoint)||p.commentFontPoint<8||p.commentFontPoint>32)throw Error("显示偏好格式无效");
 if(!["auto","below","above"].includes(p.candidatePlacement)||!Number.isInteger(p.candidateOffsetX)||p.candidateOffsetX < -256||p.candidateOffsetX>256||!Number.isInteger(p.candidateGap)||p.candidateGap<0||p.candidateGap>128||typeof p.avoidDecorations!=="boolean")throw Error("候选窗位置偏好无效");
 return p;
}
function effective(style,p){
 const s={...style};if(!p)return s;
 if(!p.annotationEnabled)s.annotationPosition="hidden";else if(p.annotationPosition!=="skin")s.annotationPosition=p.annotationPosition;
 if(p.fontMode==="user"){s.fontPoint=p.fontPoint;s.commentFontPoint=p.commentFontPoint;}
 if(p.density!=="skin")s.candidateSpacing=p.density==="compact"?8:12;
 return s;
}
function layout(style,layers,candidates,annotations,measure,options={}){
 const font=style.fontPoint*4/3,comment=style.commentFontPoint*4/3,gap=options.density==="standard"?3:2,inset=options.density==="standard"?6:4;
 const size=(text,f)=>{const m=measure(text,f);return typeof m==="number"?{width:m,height:f*1.3}:m;};
 const cells=candidates.map((c,i)=>({text:c.text,comment:annotations?(c.comment||""):"",label:String(i+1)+"."}));
 const above=style.annotationPosition==="above",has=annotations&&style.annotationPosition!=="hidden"&&(cells.some(c=>c.comment)||(above&&options.emptyAnnotationMode==="reserve"));
 const textHeight=Math.max(font*1.3,...cells.map(c=>size(c.text,font).height)),commentHeight=has?Math.max(comment*1.3,...cells.map(c=>size(c.comment||"ā",comment).height)):0;
 let x=style.paddingLeft+inset;const rowTop=style.paddingTop+inset;
 for(const c of cells){c.labelWidth=size(c.label,font).width;c.textWidth=size(c.text,font).width;c.commentWidth=has?size(c.comment,comment).width:0;const tw=c.labelWidth+gap+c.textWidth;c.width=above?Math.max(tw,c.commentWidth):tw+(c.commentWidth?gap+c.commentWidth:0);c.x=x;c.textX=x+(above?Math.floor((c.width-tw)/2):0);c.commentX=above?x+Math.floor((c.width-c.commentWidth)/2):x+tw+gap;c.textY=rowTop+(has&&above?commentHeight+gap:0);c.commentY=above?rowTop:rowTop+textHeight-commentHeight;c.height=above?textHeight+(has?commentHeight+gap:0):Math.max(textHeight,has?commentHeight:0);x+=c.width+style.candidateSpacing;}
 const width=x-(cells.length?style.candidateSpacing:0)+style.paddingRight+inset,height=rowTop+(above?textHeight+(has?commentHeight+gap:0):Math.max(textHeight,has?commentHeight:0))+style.paddingBottom+inset;
 let left=0,top=0,right=width,bottom=height;
 const rects=layers.map((l,i)=>{const r={...l,index:i,left:(l.anchor.endsWith("right")?width:0)+(l.x||0),top:(l.anchor.startsWith("bottom")?height:0)+(l.y||0)};left=Math.min(left,r.left);top=Math.min(top,r.top);right=Math.max(right,r.left+l.width);bottom=Math.max(bottom,r.top+l.height);return r;});
 return {width,height,left,top,right,bottom,cells,layers:rects,hasAnnotation:has,above,commentHeight,rowTop,font,comment,gap};
}
function fit(rect,image){const scale=Math.min(rect.width/image.width,rect.height/image.height),width=Math.floor(image.width*scale),height=Math.floor(image.height*scale);return {x:rect.left+(rect.width-width)/2,y:rect.top+rect.height-height,width,height};}
function hit(g,x,y){return g.cells.findIndex(c=>x>=c.x&&x<=c.x+c.width&&y>=g.rowTop&&y<=g.rowTop+c.height);}
const api={layout,fit,hit,preferences,effective};if(typeof module!=="undefined")module.exports=api;else root.SkinGeometry=api;
})(globalThis);
