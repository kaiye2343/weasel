/* Offline rimeskin IO. No network, eval, or filesystem access. */
(function (root) {
"use strict";
const MAX_ARCHIVE=50*1024*1024, MAX_EXPANDED=100*1024*1024, MAX_DECODED=64*1024*1024;
const enc=new TextEncoder(), dec=new TextDecoder("utf-8",{fatal:true});
function fail(message){throw new Error(message);}
const crcTable=Uint32Array.from({length:256},(_,n)=>{let c=n;for(let k=0;k<8;k++)c=(c>>>1)^((c&1)?0xedb88320:0);return c>>>0;});
function crc32(bytes){let crc=0xffffffff;for(const b of bytes)crc=(crc>>>8)^crcTable[(crc^b)&255];return (~crc)>>>0;}
function path(name){
 if(typeof name!=="string"||!name||name.length>240||/[\\:*?"<>|\x00-\x1f]/.test(name)||name.startsWith("/")||name.split("/").some(s=>!s||s==="."||s===".."||/[ .]$/.test(s)||/^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])($|\.)/i.test(s)))fail("不安全的资源路径："+name);
 return name;
}
function integer(value,low,high,label){if(!Number.isInteger(value)||value<low||value>high)fail(label+" 超出 "+low+"—"+high+" 范围");}
function object(v,label){if(!v||typeof v!=="object"||Array.isArray(v))fail(label+" 必须是对象");}
function keys(v,allowed,label){object(v,label);for(const k of Object.keys(v))if(!allowed.includes(k))fail(label+" 不支持字段："+k);}
function text(v,max,label){if(typeof v!=="string"||!v.trim()||v.length>max)fail(label+" 不能为空或过长");}
function png(bytes){
 if(!(bytes instanceof Uint8Array)||bytes.length<33)fail("PNG 数据不完整");
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 if(![137,80,78,71,13,10,26,10].every((b,i)=>bytes[i]===b))fail("仅支持静态 PNG");
 let pos=8,width=0,height=0,hasData=false,ended=false,first=true;
 while(pos<bytes.length){
  if(pos+12>bytes.length)fail("PNG 块不完整");
  const n=v.getUint32(pos),end=pos+12+n;if(end>bytes.length)fail("PNG 块越界");
  const type=String.fromCharCode(...bytes.subarray(pos+4,pos+8));
  if(first&&type!=="IHDR")fail("PNG 缺少 IHDR");
  if(type==="IHDR"){if(!first||n!==13)fail("PNG IHDR 无效");width=v.getUint32(pos+8);height=v.getUint32(pos+12);integer(width,1,2048,"图片宽度");integer(height,1,2048,"图片高度");}
  if(type==="acTL")fail("首版仅支持静态 PNG，请导出静态图片");
  if(crc32(bytes.subarray(pos+4,pos+8+n))!==v.getUint32(pos+8+n))fail("PNG CRC 校验失败");
  if(type==="IDAT")hasData=true;
  pos=end;first=false;
  if(type==="IEND"){if(n!==0||pos!==bytes.length)fail("PNG 结束块无效");ended=true;break;}
 }
 if(!ended||!hasData)fail("PNG 缺少图像数据");return {width,height};
}
const defaults={fontFace:"Microsoft YaHei UI",fontPoint:12,commentFontPoint:10,annotationPosition:"above",paddingLeft:8,paddingRight:8,paddingTop:4,paddingBottom:4,candidateSpacing:8,cornerRadius:8,shadowRadius:6,shadowColor:"#22101820",backgroundColor:"#FFF8FBFF",textColor:"#FF223448",commentColor:"#FF71869B",highlightColor:"#FFE0EDF9",highlightTextColor:"#FF1768AF",borderColor:"#FFC7D9E9",borderWidth:1,borderStyle:"solid",borderInset:0,annotationSeparator:{enabled:true,color:"#33000000",thickness:1,fadeEnds:true},frame:null};
const styleKeys=Object.keys(defaults);
function style(value){
 keys(value,styleKeys,"样式");if(value.annotationSeparator!==undefined)object(value.annotationSeparator,"分隔线");const s={...defaults,...value,annotationSeparator:{...defaults.annotationSeparator,...value.annotationSeparator}};
 for(const [k,hi] of Object.entries({fontPoint:48,commentFontPoint:32,paddingLeft:256,paddingRight:256,paddingTop:256,paddingBottom:256,candidateSpacing:256,cornerRadius:64,shadowRadius:32,borderWidth:12,borderInset:32}))integer(s[k],k.endsWith("Point")?8:0,hi,k);
 text(s.fontFace,128,"字体");
 if(!["above","beside","hidden"].includes(s.annotationPosition)||!["none","solid","dash"].includes(s.borderStyle))fail("不支持的注音位置或边框");
 for(const k of styleKeys.filter(k=>k.endsWith("Color")))if(!/^#[0-9a-f]{8}$/i.test(s[k]))fail("颜色须为 #AARRGGBB");
 keys(s.annotationSeparator,["enabled","color","thickness","fadeEnds"],"分隔线");
 if(typeof s.annotationSeparator.enabled!=="boolean"||typeof s.annotationSeparator.fadeEnds!=="boolean"||!/^#[0-9a-f]{8}$/i.test(s.annotationSeparator.color))fail("分隔线无效");
 integer(s.annotationSeparator.thickness,1,4,"分隔线粗细");
 if(s.frame!==null){keys(s.frame,["path","slice"],"九宫格");path(s.frame.path);if(!Array.isArray(s.frame.slice)||s.frame.slice.length!==4)fail("九宫格需要四个切片");s.frame.slice.forEach(n=>integer(n,0,2048,"切片"));}
 return s;
}
function validate(m,assets){
 keys(m,["schemaVersion","id","name","author","version","minEngineVersion","preview","variants","annotationSupport"],"清单");
 if(m.schemaVersion===1){if(m.minEngineVersion!=="1.0"||m.annotationSupport!==undefined)fail("v1 不能声明注音能力");}
 else if(m.schemaVersion===2){if(m.minEngineVersion!=="1.1"||!["none","both"].includes(m.annotationSupport))fail("v2 需要注音能力和引擎 1.1");}
 else fail("不支持此皮肤格式版本");
 const id=v=>typeof v==="string"&&/^[a-z0-9][a-z0-9._-]{0,63}$/.test(v)&&!v.includes("..");
 if(!id(m.id)||typeof m.version!=="string"||!/^\d+\.\d+\.\d+$/.test(m.version))fail("皮肤标识或版本无效");
 text(m.name,1024,"名称");text(m.author,1024,"作者");
 function resource(name){path(name);if(!/\.png$/i.test(name)||!assets.has(name))fail("缺少 PNG："+name);return png(assets.get(name));}
 resource(m.preview);
 if(!Array.isArray(m.variants)||m.variants.length<1||m.variants.length>32)fail("变体数量须为 1—32");
 const ids=new Set();
 for(const v of m.variants){
  keys(v,["id","name","style","layers"],"变体");text(v.name,1024,"变体名称");
  if(!id(v.id)||ids.has(v.id))fail("变体标识无效或重复");ids.add(v.id);
  const s=style(v.style);let decoded=0;
  if(!Array.isArray(v.layers)||v.layers.length>8)fail("每个变体最多 8 层");
  for(const l of v.layers){
   keys(l,["path","anchor","x","y","width","height","z"],"图层");
   if(!["top_left","top_right","bottom_left","bottom_right"].includes(l.anchor))fail("图片锚点无效");
   integer(l.x??0,-512,512,"横向偏移");integer(l.y??0,-512,512,"纵向偏移");integer(l.width,1,512,"图层宽度");integer(l.height,1,512,"图层高度");integer(l.z??0,-100,100,"图层顺序");
   const size=resource(l.path);decoded+=size.width*size.height*4;
  }
  if(s.frame){const size=resource(s.frame.path),t=s.frame.slice;if(t[0]+t[2]>=size.width||t[1]+t[3]>=size.height)fail("九宫格中心必须非空");decoded+=size.width*size.height*4;}
  if(decoded>MAX_DECODED)fail("活动变体解码内存超过 64 MiB");
 }
 const manifest=enc.encode(JSON.stringify(m));if(manifest.length>1024*1024)fail("清单超过 1 MiB");
 let total=manifest.length;for(const [name,b] of assets){path(name);if(!/\.(png|json)$/i.test(name))fail("包内仅允许 PNG 和 JSON");if(name==="manifest.json")fail("资源不能覆盖清单");total+=b.length;}
 if(assets.size+1>256||total>MAX_EXPANDED)fail("皮肤资源规模超限");
 return m;
}
function makeZip(entries){
 if(entries.size>256)fail("文件过多");
 const chunks=[],central=[];let offset=0,total=0;
 for(const [name,bytes] of entries){
  path(name);const n=enc.encode(name),crc=crc32(bytes),head=new Uint8Array(30+n.length),h=new DataView(head.buffer);
  h.setUint32(0,0x04034b50,true);h.setUint16(4,20,true);h.setUint16(6,0x800,true);h.setUint16(12,33,true);h.setUint32(14,crc,true);h.setUint32(18,bytes.length,true);h.setUint32(22,bytes.length,true);h.setUint16(26,n.length,true);head.set(n,30);
  const c=new Uint8Array(46+n.length),cv=new DataView(c.buffer);
  cv.setUint32(0,0x02014b50,true);cv.setUint16(4,20,true);cv.setUint16(6,20,true);cv.setUint16(8,0x800,true);cv.setUint16(14,33,true);cv.setUint32(16,crc,true);cv.setUint32(20,bytes.length,true);cv.setUint32(24,bytes.length,true);cv.setUint16(28,n.length,true);cv.setUint32(42,offset,true);c.set(n,46);
  chunks.push(head,bytes);central.push(c);offset+=head.length+bytes.length;total+=bytes.length;
 }
 const cdSize=central.reduce((n,c)=>n+c.length,0),end=new Uint8Array(22),e=new DataView(end.buffer);
 e.setUint32(0,0x06054b50,true);e.setUint16(8,entries.size,true);e.setUint16(10,entries.size,true);e.setUint32(12,cdSize,true);e.setUint32(16,offset,true);
 const size=offset+cdSize+22;if(size>MAX_ARCHIVE||total>MAX_EXPANDED)fail("导出包超过 50 MiB 或展开预算");
 const out=new Uint8Array(size);let p=0;for(const c of [...chunks,...central,end]){out.set(c,p);p+=c.length;}return out;
}
async function inflate(bytes,limit){
 if(typeof DecompressionStream==="undefined")fail("此浏览器不能解压，请使用新版 Edge 或 Chrome");
 const reader=new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw")).getReader(),chunks=[];let size=0;
 try{while(true){const r=await reader.read();if(r.done)break;size+=r.value.length;if(size>limit)fail("解压内容超过声明长度");chunks.push(r.value);}}finally{await reader.cancel().catch(()=>{});}
 const out=new Uint8Array(size);let p=0;for(const c of chunks){out.set(c,p);p+=c.length;}return out;
}
async function readZip(bytes,inflateRaw=inflate){
 if(bytes.length>MAX_ARCHIVE||bytes.length<22)fail("压缩包大小无效");
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);let end=-1;
 for(let p=bytes.length-22;p>=Math.max(0,bytes.length-65557);p--)if(v.getUint32(p,true)===0x06054b50&&p+22+v.getUint16(p+20,true)===bytes.length){end=p;break;}
 if(end<0)fail("ZIP 结束记录无效");
 const count=v.getUint16(end+10,true),cdEnd=v.getUint32(end+16,true)+v.getUint32(end+12,true);
 if(v.getUint16(end+4,true)||v.getUint16(end+6,true)||v.getUint16(end+8,true)!==count||count>256||cdEnd!==end)fail("不支持分卷、ZIP64 或过多文件");
 const entries=new Map(),names=new Set();let p=v.getUint32(end+16,true),expanded=0;
 for(let i=0;i<count;i++){
  if(p+46>end||v.getUint32(p,true)!==0x02014b50)fail("ZIP 目录无效");
  const flags=v.getUint16(p+8,true),method=v.getUint16(p+10,true),crc=v.getUint32(p+16,true),packed=v.getUint32(p+20,true),size=v.getUint32(p+24,true),nl=v.getUint16(p+28,true),xl=v.getUint16(p+30,true),cl=v.getUint16(p+32,true),local=v.getUint32(p+42,true);
  if(p+46+nl+xl+cl>end||local+30>v.getUint32(end+16,true))fail("ZIP 条目越界");
  const name=dec.decode(bytes.subarray(p+46,p+46+nl)),dir=name.endsWith("/");path(dir?name.slice(0,-1):name);
  const normalized=(dir?name.slice(0,-1):name).toLowerCase();
  if(names.has(normalized)||flags&1||![0,8].includes(method)||((v.getUint32(p+38,true)>>>16)&0xf000)===0xa000||v.getUint16(p+34,true)!==0)fail("包含重复路径、加密、链接或不支持的格式");names.add(normalized);
  if(!dir&&!/\.(png|json)$/i.test(name))fail("皮肤只能包含 PNG 和 JSON");
  expanded+=size;if(expanded>MAX_EXPANDED||name==="manifest.json"&&size>1024*1024)fail("展开资源或清单过大");
  if(v.getUint32(local,true)!==0x04034b50||v.getUint16(local+8,true)!==method||v.getUint16(local+6,true)!==flags)fail("ZIP 本地记录不匹配");
  const ln=v.getUint16(local+26,true),lx=v.getUint16(local+28,true),start=local+30+ln+lx;
  if(start+packed>v.getUint32(end+16,true)||dec.decode(bytes.subarray(local+30,local+30+ln))!==name)fail("ZIP 数据越界或路径不匹配");
  if(!(flags&8)&&(v.getUint32(local+14,true)!==crc||v.getUint32(local+18,true)!==packed||v.getUint32(local+22,true)!==size))fail("ZIP 长度不一致");
  if(!dir){
   const payload=bytes.subarray(start,start+packed),data=method===0?payload.slice():await inflateRaw(payload,size);
   if(data.length!==size||crc32(data)!==crc)fail("ZIP 长度或 CRC 校验失败");entries.set(name,data);
  }else if(size)fail("ZIP 目录包含数据");
  p+=46+nl+xl+cl;
 }
 if(p!==cdEnd||!entries.has("manifest.json"))fail("包根目录缺少清单或目录异常");
 const manifest=JSON.parse(dec.decode(entries.get("manifest.json")));entries.delete("manifest.json");validate(manifest,entries);
 return {manifest,assets:entries};
}
function packageBytes(manifest,assets){validate(manifest,assets);return makeZip(new Map([["manifest.json",enc.encode(JSON.stringify(manifest,null,2)+"\n")],...assets]));}
function newManifest(){return {schemaVersion:2,id:"my-skin",name:"我的皮肤",author:"本地作者",version:"1.0.0",minEngineVersion:"1.1",annotationSupport:"both",preview:"preview.png",variants:[{id:"light",name:"晴空",style:structuredClone(defaults),layers:[]}]};}
const api={crc32,path,png,style,validate,makeZip,readZip,packageBytes,newManifest,defaults,MAX_ARCHIVE};
if(typeof module!=="undefined")module.exports=api;else root.SkinPackage=api;
})(globalThis);
