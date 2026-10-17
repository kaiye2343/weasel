/* Rime Skin Studio frontend, intentionally independent of the input method. */
"use strict";
const P=SkinPackage,G=SkinGeometry,$=id=>document.getElementById(id);
let manifest=P.newManifest(),assets=new Map(),images=new Map(),variant=0,selected=-1,tab="appearance",mode="annotated",dirty=false,busy=false,geometry=null,view=null,drag=null,history=[],assetSerial=0;
const canvas=$("canvas"),ctx=canvas.getContext("2d");
let displayPreferences=G.preferences();
const samples={
 short:[["你好","nǐ hǎo"],["拟好","nǐ hǎo"],["泥蒿","ní hāo"],["你","nǐ"],["泥","ní"]],
 long:[["晴空与海岸","qíng kōng yǔ hǎi àn"],["所有未竟之事","suǒ yǒu wèi jìng zhī shì"],["今天","jīn tiān"],["此刻","cǐ kè"],["你好","nǐ hǎo"]],
 mixed:[["思念你",""],["三年","sān nián"],["索尼","suǒ ní"],["少年","shào nián"],["十年","shí nián"]],
 empty:[["说到底",""],["适当的",""],["手动挡",""],["时代的",""],["适当地",""]]
};
function current(){return manifest.variants[variant];}
function clone(v){return structuredClone(v);}
function status(text,error=false){$("status").textContent=text;$("status-dot").style.background=error?"#d87c74":"#7eae95";}
function mark(){dirty=true;status("尚未导出 · 更改仅在此网页内");}
function remember(){history.push(JSON.stringify({manifest,variant,selected}));if(history.length>30)history.shift();}
function change(fn,refresh=true){remember();fn();mark();if(refresh)render();else draw();}
function safe(action){return async(...args)=>{if(busy)return;busy=true;document.querySelector(".workspace").inert=true;$("new-project").disabled=true;$("export-project").disabled=true;$("open-project").disabled=true;try{await action(...args);}catch(e){status(e.message||String(e),true);}finally{busy=false;document.querySelector(".workspace").inert=false;$("new-project").disabled=false;$("export-project").disabled=false;$("open-project").disabled=false;}};}
function cssColor(argb){return "#"+argb.slice(3)+argb.slice(1,3);}
function download(bytes,name,type){const url=URL.createObjectURL(new Blob([bytes],{type})),a=document.createElement("a");a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),2000);}
function clearImages(map){for(const value of map.values())if(value.url)URL.revokeObjectURL(value.url);}
async function imageFrom(bytes){
 P.png(bytes);const url=URL.createObjectURL(new Blob([bytes],{type:"image/png"}));
 try{const image=new Image();image.src=url;await image.decode();if(image.naturalWidth>2048||image.naturalHeight>2048)throw Error("PNG 解码尺寸超限");return {image,url,width:image.naturalWidth,height:image.naturalHeight};}
 catch(e){URL.revokeObjectURL(url);throw Error("PNG 无法解码："+e.message);}
}
function references(v){return new Set([...v.layers.map(l=>l.path),...(v.style.frame?[v.style.frame.path]:[])]);}
async function decodeAssets(m,data,index,all=false){
 const needed=references(m.variants[index]),result=new Map();
 try{for(const [name,bytes]of data)if(/\.png$/i.test(name)&&(all||needed.has(name))){const decoded=await imageFrom(bytes);if(needed.has(name))result.set(name,decoded);else URL.revokeObjectURL(decoded.url);}return result;}
 catch(e){clearImages(result);throw e;}
}
async function activateVariant(index){const decoded=await decodeAssets(manifest,assets,index);clearImages(images);images=decoded;variant=index;selected=-1;render();}
function addButton(parent,text,action,cls=""){const b=document.createElement("button");b.type="button";b.textContent=text;b.className=cls;b.onclick=action;parent.append(b);return b;}
function section(parent,title){const s=document.createElement("section");s.className="prop-section";const h=document.createElement("h3");h.textContent=title;s.append(h);parent.append(s);return s;}
function info(parent,text){const p=document.createElement("p");p.textContent=text;parent.append(p);}
function field(parent,label,value,onchange,{type="number",min=0,max=256,options=null,full=false,step=1}={}){
 const l=document.createElement("label");l.className="field"+(full?" full":"");const title=document.createElement("span");title.textContent=label;l.append(title);
 const el=document.createElement(options?"select":"input");el.setAttribute("aria-label",label);
 if(options){for(const [v,t] of options){const o=document.createElement("option");o.value=v;o.textContent=t;el.append(o);}el.value=String(value);}
 else{el.type=type;if(type==="checkbox")el.checked=value;else{el.value=value;if(type==="number"){el.min=min;el.max=max;el.step=step;}}}
 el.onchange=()=>{let next=el.type==="checkbox"?el.checked:el.value;if(el.type==="text"&&(!next.trim()||next.length>(label==="字体"?128:1024))){status(label+" 不能为空或过长",true);el.value=value;return;}if(el.type==="number"){next=Number(next);if(!Number.isInteger(next)||next<min||next>max){el.value=value;status(label+" 参数超限",true);return;}}change(()=>onchange(next));};
 l.append(el);parent.append(l);return el;
}
function color(parent,label,key,s){
 const l=document.createElement("label");l.className="field";const t=document.createElement("span");t.textContent=label;l.append(t);const box=document.createElement("div");box.className="color-field";
 const input=document.createElement("input");input.type="color";input.value=s[key].slice(0,1)+s[key].slice(3);input.setAttribute("aria-label",label+"颜色");
 const opacity=document.createElement("input");opacity.type="number";opacity.min=0;opacity.max=100;opacity.value=Math.round(parseInt(s[key].slice(1,3),16)*100/255);opacity.setAttribute("aria-label",label+"不透明度");
 const apply=()=>{let alpha=Number(opacity.value);if(!Number.isFinite(alpha)||alpha<0||alpha>100){status("不透明度须为 0—100",true);return;}change(()=>s[key]="#"+Math.round(alpha*255/100).toString(16).padStart(2,"0").toUpperCase()+input.value.slice(1).toUpperCase());};
 input.onchange=apply;opacity.onchange=apply;box.append(input,opacity);l.append(box);parent.append(l);
}
function render(){
 if(manifest.annotationSupport==="none")mode="plain";
 $("project-name").textContent=manifest.name;$("variant-title").textContent=current().name;$("compatibility").textContent=manifest.annotationSupport==="none"?"仅无注音":"两种布局";
 const vl=$("variants");vl.replaceChildren();manifest.variants.forEach((v,i)=>{const b=addButton(vl,v.name,safe(async()=>activateVariant(i)),"variant"+(variant===i?" active":""));const dot=document.createElement("span");dot.className="dot";b.append(dot);});
 const list=$("layers");list.replaceChildren();
 current().layers.forEach((l,i)=>{const b=addButton(list,"",()=>{selected=i;tab="appearance";render();},"layer"+(selected===i?" selected":""));const thumb=document.createElement("span");thumb.className="layer-icon";thumb.textContent="▧";const span=document.createElement("span"),title=document.createElement("b"),desc=document.createElement("small");title.textContent=l.path.split("/").at(-1);desc.textContent=l.width+" × "+l.height+" · "+(l.z<0?"文字下方":"文字上方");span.append(title,desc);b.append(thumb,span);});
 $("layer-count").textContent=current().layers.length+" / 8";$("add-image").disabled=current().layers.length>=8;$("add-variant").disabled=manifest.variants.length>=32;$("select-frame").classList.toggle("selected",selected<0);
 for(const b of document.querySelectorAll("[data-mode]")){b.classList.toggle("active",b.dataset.mode===mode);b.disabled=b.dataset.mode==="annotated"&&manifest.annotationSupport==="none";}
 for(const b of document.querySelectorAll("[data-tab]"))b.classList.toggle("active",b.dataset.tab===tab);
 properties();draw();
}
function properties(){
 const parent=$("property-body");parent.replaceChildren();const v=current(),s=v.style;
 if(tab==="package"){
  let sec=section(parent,"皮肤信息");for(const [key,title] of [["name","名称"],["id","唯一标识"],["version","版本"],["author","作者"]])field(sec,title,manifest[key],x=>manifest[key]=x,{type:"text",full:true});
  sec=section(parent,"布局支持");
  field(sec,"能力",manifest.annotationSupport||"both",x=>{manifest.schemaVersion=2;manifest.minEngineVersion="1.1";manifest.annotationSupport=x;},{options:[["both","带注音与无注音"],["none","仅无注音"]],full:true});
  info(sec,"同一背景随行高伸缩；图片跟随锚点重排。仅无注音的皮肤不会被全局偏好强制开启注音。");
  info(sec,"格式："+manifest.schemaVersion+" · 最低皮肤引擎 "+manifest.minEngineVersion+"。导入旧包后，修改能力声明会升级格式。");
  sec=section(parent,"当前变体");field(sec,"名称",v.name,x=>v.name=x,{type:"text",full:true});field(sec,"标识",v.id,x=>v.id=x,{type:"text",full:true});
  if(manifest.variants.length>1)addButton(sec,"删除此变体",safe(async()=>{const decoded=await decodeAssets(manifest,assets,variant===0?1:0);remember();manifest.variants.splice(variant,1);clearImages(images);images=decoded;variant=0;selected=-1;mark();render();}),"danger");
  return;
 }
 if(selected>=0){
  const l=v.layers[selected];let sec=section(parent,"图片图层");info(sec,l.path);
  field(sec,"锚点",l.anchor,x=>l.anchor=x,{options:[["top_left","左上角"],["top_right","右上角"],["bottom_left","左下角"],["bottom_right","右下角"]]});
  for(const [key,title,lo,hi]of[["x","横向偏移",-512,512],["y","纵向偏移",-512,512],["width","容器宽度",1,512],["height","容器高度",1,512],["z","图层顺序",-100,100]])field(sec,title,l[key]||0,x=>l[key]=x,{min:lo,max:hi});
  info(sec,"偏移相对框体角点；负数可突出边界。图片等比缩放，居中靠底，容器不改变候选命中范围。z < 0 在文字下方。");
  sec=section(parent,"比例与操作");
  addButton(sec,"按素材比例调整高度",()=>change(()=>{const size=images.get(l.path);l.height=Math.min(512,Math.max(1,Math.round(l.width*size.height/size.width)));}));
  addButton(sec,"删除图层",()=>change(()=>{v.layers.splice(selected,1);selected=-1;}),"danger");
  return;
 }
 let sec=section(parent,"候选文字");
 field(sec,"候选字号",s.fontPoint,x=>s.fontPoint=x,{min:8,max:48});field(sec,"注音字号",s.commentFontPoint,x=>s.commentFontPoint=x,{min:8,max:32});
 field(sec,"注音位置",s.annotationPosition,x=>s.annotationPosition=x,{options:[["above","上方"],["beside","旁边"],["hidden","隐藏"]]});
 field(sec,"字体",s.fontFace,x=>s.fontFace=x,{type:"text",full:true});
 info(sec,"字号单位 pt。拼音始终使用候选已有注释；缺失时留空，同一行汉字共用基线。");
 color(sec,"候选文字","textColor",s);color(sec,"注音","commentColor",s);color(sec,"高亮背景","highlightColor",s);color(sec,"高亮文字","highlightTextColor",s);
 sec=section(parent,"框体与背景");
 color(sec,"背景","backgroundColor",s);color(sec,"边框","borderColor",s);
 field(sec,"边框",s.borderStyle,x=>s.borderStyle=x,{options:[["solid","实线"],["dash","虚线"],["none","无边框"]]});
 for(const [key,title,hi]of[["borderWidth","边框粗细",12],["borderInset","边框内缩",32],["cornerRadius","圆角",64],["shadowRadius","阴影半径",32]])field(sec,title,s[key],x=>s[key]=x,{max:hi});
 color(sec,"阴影","shadowColor",s);
 sec=section(parent,"九宫格背景");
 addButton(sec,s.frame?"替换背景 PNG":"选择背景 PNG",()=>pickImage("frame"));
 if(s.frame){info(sec,s.frame.path);for(const [i,name]of["左","上","右","下"].entries())field(sec,name+"切片",s.frame.slice[i],x=>s.frame.slice[i]=x,{max:2048});addButton(sec,"使用纯色背景",()=>change(()=>s.frame=null));}
 info(sec,"切片单位为原图像素，中心与边条随两种框高拉伸；四角保持形状。");
 sec=section(parent,"文字区与装饰留白");
 for(const [key,title]of[["paddingLeft","左留白"],["paddingRight","右留白"],["paddingTop","上留白"],["paddingBottom","下留白"],["candidateSpacing","候选间距"]])field(sec,title,s[key],x=>s[key]=x);
 sec=section(parent,"注音分隔线");const line=s.annotationSeparator;
 field(sec,"显示分隔线",line.enabled,x=>line.enabled=x,{type:"checkbox"});field(sec,"两端渐隐",line.fadeEnds,x=>line.fadeEnds=x,{type:"checkbox"});field(sec,"粗细",line.thickness,x=>line.thickness=x,{min:1,max:4});color(sec,"分隔线","color",line);
 info(sec,"仅在实际有注音时显示。整页没有注音时自动收起，部分缺失注音不影响候选基线。");
}
function rounded(x,y,w,h,r){ctx.beginPath();ctx.roundRect(x,y,w,h,Math.min(r,w/2,h/2));}
function nineSlice(image,w,h,slice){
 const [l,t,r,b]=slice,iw=image.width,ih=image.height;
 // Corners fit when content is smaller than source borders.
 const factor=Math.min(1,w/Math.max(1,l+r),h/Math.max(1,t+b)),dl=l*factor,dr=r*factor,dt=t*factor,db=b*factor;
 const sx=[0,l,iw-r,iw],sy=[0,t,ih-b,ih],dx=[0,dl,w-dr,w],dy=[0,dt,h-db,h];
 for(let y=0;y<3;y++)for(let x=0;x<3;x++)if(sx[x+1]>sx[x]&&sy[y+1]>sy[y]&&dx[x+1]>dx[x]&&dy[y+1]>dy[y])ctx.drawImage(image.image,sx[x],sy[y],sx[x+1]-sx[x],sy[y+1]-sy[y],dx[x],dy[y],dx[x+1]-dx[x],dy[y+1]-dy[y]);
}
function draw(clean=false){
 const profile=$("display-preview").value==="user"?displayPreferences:null;
 const s=G.effective(P.style(current().style),profile);const candidates=samples[$("sample").value].map(([text,comment])=>({text,comment}));
 const annotated=mode==="annotated"&&manifest.annotationSupport!=="none";
 const fontFace='"'+s.fontFace.replace(/["\\]/g,"")+'", sans-serif';
 const measure=(text,size)=>{ctx.font=size+"px "+fontFace;const m=ctx.measureText(text);return {width:Math.ceil(m.width),height:Math.ceil((m.fontBoundingBoxAscent||size)+(m.fontBoundingBoxDescent||0))};};
 geometry=G.layout(s,current().layers,candidates,annotated,measure,profile||{});
 const g=geometry,space=28,requested=Number($("zoom").value),available=$("stage").getBoundingClientRect();
 const scale=clean?Math.min(1,2048/(g.right-g.left+space*2),2048/(g.bottom-g.top+space*2)):Math.min(requested,(available.width-32)/(g.right-g.left+space*2),(available.height-100)/(g.bottom-g.top+space*2));
 const ratio=clean?1:window.devicePixelRatio||1,w=Math.ceil((g.right-g.left+space*2)*scale),h=Math.ceil((g.bottom-g.top+space*2)*scale);
 canvas.width=Math.max(1,Math.round(w*ratio));canvas.height=Math.max(1,Math.round(h*ratio));canvas.style.width=w+"px";canvas.style.height=h+"px";
 ctx.setTransform(scale*ratio,0,0,scale*ratio,(space-g.left)*scale*ratio,(space-g.top)*scale*ratio);
 view={scale,x:space-g.left,y:space-g.top};ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality="high";
 ctx.shadowColor=cssColor(s.shadowColor);ctx.shadowBlur=s.shadowRadius;ctx.shadowOffsetY=2;
 rounded(0,0,g.width,g.height,s.cornerRadius);ctx.fillStyle=cssColor(s.backgroundColor);ctx.fill();ctx.shadowBlur=0;ctx.shadowOffsetY=0;
 if(s.frame&&images.has(s.frame.path)){ctx.save();rounded(0,0,g.width,g.height,s.cornerRadius);ctx.clip();nineSlice(images.get(s.frame.path),g.width,g.height,s.frame.slice);ctx.restore();}
 if(s.borderStyle!=="none"&&s.borderWidth){ctx.strokeStyle=cssColor(s.borderColor);ctx.lineWidth=s.borderWidth;ctx.setLineDash(s.borderStyle==="dash"?[4,3]:[]);const inset=s.borderInset+s.borderWidth/2;rounded(inset,inset,Math.max(0,g.width-2*inset),Math.max(0,g.height-2*inset),s.cornerRadius);ctx.stroke();ctx.setLineDash([]);}
 const layers=g.layers.slice().sort((a,b)=>(a.z||0)-(b.z||0));
 const drawLayer=l=>{const image=images.get(l.path);if(!image)return;const fit=G.fit(l,image);ctx.drawImage(image.image,fit.x,fit.y,fit.width,fit.height);};
 layers.filter(l=>l.z<0).forEach(drawLayer);
 const high=Number($("highlight").value)-1;
 ctx.textBaseline="top";
 g.cells.forEach((c,i)=>{
  if(i===high){rounded(c.x-3,g.rowTop-2,c.width+6,c.height+4,5);ctx.fillStyle=cssColor(s.highlightColor);ctx.fill();}
  if(c.comment&&g.hasAnnotation){ctx.font=g.comment+"px "+fontFace;ctx.fillStyle=cssColor(s.commentColor);ctx.fillText(c.comment,c.commentX,c.commentY);}
  ctx.font=g.font+"px "+fontFace;ctx.fillStyle=cssColor(i===high?s.highlightTextColor:s.textColor);const tx=c.textX;ctx.fillText(c.label,tx,c.textY);ctx.fillText(c.text,tx+c.labelWidth+g.gap,c.textY);
 });
 if(g.hasAnnotation&&g.above&&s.annotationSeparator.enabled){
  const x1=g.cells[0]?.x||0,x2=g.cells.at(-1)?g.cells.at(-1).x+g.cells.at(-1).width:g.width;
  const line=s.annotationSeparator,color=cssColor(line.color);
  if(line.fadeEnds){const grad=ctx.createLinearGradient(x1,0,x2,0);grad.addColorStop(0,cssColor("#00"+line.color.slice(3)));grad.addColorStop(.2,color);grad.addColorStop(.8,color);grad.addColorStop(1,cssColor("#00"+line.color.slice(3)));ctx.fillStyle=grad;}else ctx.fillStyle=color;
  ctx.fillRect(x1,g.rowTop+g.commentHeight+Math.max(0,(g.gap-line.thickness)/2),x2-x1,line.thickness);
 }
 layers.filter(l=>l.z>=0).forEach(drawLayer);
 if(selected>=0&&!clean){const l=g.layers[selected];ctx.save();ctx.strokeStyle="#2784f0";ctx.lineWidth=1/scale;ctx.setLineDash([4/scale,3/scale]);ctx.strokeRect(l.left,l.top,l.width,l.height);ctx.setLineDash([]);ctx.fillStyle="#fff";for(const x of[l.left,l.left+l.width])for(const y of[l.top,l.top+l.height]){ctx.fillRect(x-3/scale,y-3/scale,6/scale,6/scale);ctx.strokeRect(x-3/scale,y-3/scale,6/scale,6/scale);}ctx.restore();}
 $("dimensions").textContent=Math.round(g.width)+" × "+Math.round(g.height)+" DIP"+(scale<requested-.01?" · 已适配画布":"");
}
function usedAssets(previewBytes){
 const result=new Map(),add=name=>{if(!assets.has(name))throw Error("缺少素材："+name);result.set(name,assets.get(name));};
 for(const v of manifest.variants){v.layers.forEach(l=>add(l.path));if(v.style.frame)add(v.style.frame.path);}
 // Always generate a fresh preview; avoid an imported preview colliding with a live image.
 let preview="preview.png";while(result.has(preview))preview="studio-"+preview;
 const m=clone(manifest);m.preview=preview;result.set(preview,previewBytes);return {manifest:m,assets:result};
}
async function previewPng(){draw(true);try{const blob=await new Promise(resolve=>canvas.toBlob(resolve,"image/png"));if(!blob)throw Error("无法生成预览");const bytes=new Uint8Array(await blob.arrayBuffer());P.png(bytes);return bytes;}finally{draw();}}
async function loadPackage(bytes){
 const next=await P.readZip(bytes),decoded=await decodeAssets(next.manifest,next.assets,0,true);
 // Commit only after complete validation/decode; failed import keeps the active document.
 clearImages(images);manifest=next.manifest;assets=next.assets;images=decoded;
 manifest.variants.forEach(v=>v.style=P.style(v.style));variant=0;selected=-1;history=[];dirty=false;mode=manifest.annotationSupport==="none"?"plain":"annotated";render();status("导入完成 · 未连接日常输入法");
}
let imagePurpose="layer";
function pickImage(purpose){imagePurpose=purpose;$("image-file").value="";$("image-file").click();}
$("image-file").onchange=safe(async()=>{const file=$("image-file").files[0];if(!file)return;if(file.size>P.MAX_ARCHIVE)throw Error("图片文件过大");const bytes=new Uint8Array(await file.arrayBuffer());P.png(bytes);
 if([...assets.values()].reduce((n,b)=>n+b.length,0)+bytes.length>100*1024*1024)throw Error("工作区素材超过 100 MiB，请导出后重新导入清理未使用素材");
 const v=current(),used=imagePurpose==="frame"?v.layers.map(l=>l.path):[...v.layers.map(l=>l.path),...(v.style.frame?[v.style.frame.path]:[])];
 const decodedBytes=used.reduce((n,name)=>{const size=P.png(assets.get(name));return n+size.width*size.height*4;},0),size=P.png(bytes);
 if(decodedBytes+size.width*size.height*4>64*1024*1024)throw Error("活动变体解码超过 64 MiB");
 const decoded=await imageFrom(bytes);
 const targetVariant=current(),purpose=imagePurpose;
 if(purpose==="layer"&&targetVariant.layers.length>=8){URL.revokeObjectURL(decoded.url);throw Error("每个变体最多八层");}
 let name;do{name="assets/image-"+(++assetSerial)+".png";}while(assets.has(name));
 remember();assets.set(name,bytes);images.set(name,decoded);
 if(purpose==="frame"){targetVariant.style.frame={path:name,slice:[0,0,0,0]};selected=-1;}
 else{const width=Math.min(140,decoded.width),height=Math.max(1,Math.round(width*decoded.height/decoded.width));targetVariant.layers.push({path:name,anchor:"bottom_right",x:8,y:-Math.min(512,height),width,height:Math.min(512,height),z:1});selected=targetVariant.layers.length-1;}
 mark();render();
});
$("open-project").onclick=()=>{$("package-file").value="";$("package-file").click();};
$("package-file").onchange=safe(async()=>{const file=$("package-file").files[0];if(!file)return;if(dirty&&!confirm("当前更改尚未导出，继续导入会替换画布。继续吗？"))return;if(file.size>P.MAX_ARCHIVE)throw Error("皮肤包超过 50 MiB");await loadPackage(new Uint8Array(await file.arrayBuffer()));});
$("export-project").onclick=safe(async()=>{const pack=usedAssets(await previewPng()),bytes=P.packageBytes(pack.manifest,pack.assets);download(bytes,manifest.id+"-"+manifest.version+".rimeskin","application/zip");dirty=false;status("已导出 · 请用独立原生预览验收");});
$("save-preview").onclick=safe(async()=>download(await previewPng(),"skin-preview.png","image/png"));
$("add-image").onclick=()=>pickImage("layer");
$("select-frame").onclick=()=>{selected=-1;tab="appearance";render();};
$("new-project").onclick=()=>{if(dirty&&!confirm("当前更改尚未导出，确定新建设计吗？"))return;clearImages(images);images=new Map();assets=new Map();manifest=P.newManifest();variant=0;selected=-1;history=[];mode="annotated";dirty=false;render();status("新设计 · 尚未导出");};
$("add-variant").onclick=()=>change(()=>{const v=clone(current()),base=v.id;let n=2;while(manifest.variants.some(x=>x.id===base+"-"+n))n++;v.id=(base.slice(0,56)+"-"+n);while(manifest.variants.some(x=>x.id===v.id))v.id="variant-"+(++n);v.name+=" 副本";manifest.variants.push(v);variant=manifest.variants.length-1;selected=-1;});
$("background-toggle").onclick=()=>$("stage").classList.toggle("dark");
for(const b of document.querySelectorAll("[data-tab]"))b.onclick=()=>{tab=b.dataset.tab;render();};
for(const b of document.querySelectorAll("[data-mode]"))b.onclick=()=>{mode=b.dataset.mode;draw();for(const x of document.querySelectorAll("[data-mode]"))x.classList.toggle("active",x===b);};
for(const id of["sample","highlight","zoom"])$(id).onchange=()=>draw();
$("help").onclick=()=>$("help-dialog").showModal();$("close-help").onclick=()=>$("help-dialog").close();
function point(e){const r=canvas.getBoundingClientRect();return {x:(e.clientX-r.left)/view.scale-view.x,y:(e.clientY-r.top)/view.scale-view.y};}
canvas.onpointerdown=e=>{
 if(busy||!view)return;const p=point(e),list=geometry.layers.slice().sort((a,b)=>(a.z||0)-(b.z||0)).reverse();
 const found=list.find(l=>p.x>=l.left&&p.x<=l.left+l.width&&p.y>=l.top&&p.y<=l.top+l.height);
 if(found){selected=found.index;tab="appearance";const l=current().layers[selected];remember();drag={id:e.pointerId,clientX:e.clientX,clientY:e.clientY,scale:view.scale,x:l.x||0,y:l.y||0};canvas.setPointerCapture(e.pointerId);render();}
 else{const index=G.hit(geometry,p.x,p.y);if(index>=0)$("highlight").value=String(index+1);selected=-1;render();}
};
canvas.onpointermove=e=>{if(!drag||e.pointerId!==drag.id)return;const l=current().layers[selected];l.x=Math.max(-512,Math.min(512,Math.round(drag.x+(e.clientX-drag.clientX)/drag.scale)));l.y=Math.max(-512,Math.min(512,Math.round(drag.y+(e.clientY-drag.clientY)/drag.scale)));mark();draw();};
const endDrag=e=>{if(drag&&drag.id===e.pointerId){drag=null;render();}};canvas.onpointerup=endDrag;canvas.onpointercancel=endDrag;
window.addEventListener("keydown",async e=>{if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==="z"&&!["INPUT","SELECT","TEXTAREA"].includes(document.activeElement.tagName)&&!busy&&history.length){e.preventDefault();const state=JSON.parse(history.at(-1));await safe(async()=>{const decoded=await decodeAssets(state.manifest,assets,state.variant);history.pop();clearImages(images);images=decoded;manifest=state.manifest;variant=state.variant;selected=state.selected;mark();render();})();}});
window.addEventListener("beforeunload",e=>{if(dirty){e.preventDefault();e.returnValue="";}});
new ResizeObserver(()=>draw()).observe($("stage"));
render();

$("display-preview").onchange=()=>draw();
$("load-display-preview").onclick=()=>{$("display-file").value="";$("display-file").click();};
$("display-file").onchange=safe(async()=>{const file=$("display-file").files[0];if(!file)return;if(file.size>65536)throw Error("显示偏好文件过大");const next=G.preferences(JSON.parse(await file.text()));displayPreferences=next;$("display-preview").value="user";draw();status("已载入个人显示偏好 · 不改写皮肤包");});
