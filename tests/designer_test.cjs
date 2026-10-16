"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),zlib=require("node:zlib");
const P=require("../designer/package.js"),G=require("../designer/geometry.js");
let passed=0;function check(name,fn){fn();passed++;console.log("PASS "+name);}
function reject(name,fn){check(name,()=>assert.throws(fn));}
async function rejectAsync(name,fn){await assert.rejects(fn);passed++;console.log("PASS "+name);}
const preview=new Uint8Array(fs.readFileSync(path.join(__dirname,"../skins/neutral/preview.png")));
const m=P.newManifest(),assets=new Map([["preview.png",preview]]);
(async()=>{
 check("valid generic package",()=>P.validate(m,assets));
 const zip=P.packageBytes(m,assets),back=await P.readZip(zip);check("store ZIP round trip",()=>assert.deepEqual(back.manifest,m));
 for(const name of["../bad.png","a/../b.png","C:/x.png","a\\b.png","/bad.png","NUL.png","a/COM1.png","a./b.png","a//b.png","a:b.png","a\u0000b.png","a?.png"])reject("unsafe path "+JSON.stringify(name),()=>P.path(name));
 for(const [name,edit]of[
 ["unknown manifest key",x=>x.script="run"],["unknown version",x=>x.schemaVersion=99],["missing v2 capability",x=>delete x.annotationSupport],
 ["bad capability",x=>x.annotationSupport="guess"],["old min engine",x=>x.minEngineVersion="1.0"],["unknown style",x=>x.variants[0].style.script="x"],
 ["null separator",x=>x.variants[0].style.annotationSeparator=null],["bad point",x=>x.variants[0].style.fontPoint=80],
 ["bad color",x=>x.variants[0].style.textColor="red"],["empty variants",x=>x.variants=[]],
 ["too many layers",x=>x.variants[0].layers=Array(9).fill({})],["missing resource",x=>x.preview="missing.png"],
 ["empty frame center",x=>x.variants[0].style.frame={path:"preview.png",slice:[2048,0,2048,0]}]
 ]){const x=structuredClone(m);edit(x);reject(name,()=>P.validate(x,assets));}
 const legacy=structuredClone(m);legacy.schemaVersion=1;legacy.minEngineVersion="1.0";delete legacy.annotationSupport;check("v1 remains supported",()=>P.validate(legacy,assets));
 const plain=structuredClone(m);plain.annotationSupport="none";check("plain-only capability",()=>P.validate(plain,assets));
 const corrupt=preview.slice();corrupt[29]^=1;reject("corrupt PNG CRC",()=>P.png(corrupt));
 const badZip=zip.slice();badZip[32]^=1;await rejectAsync("corrupt local name rejected",()=>P.readZip(badZip));
 await rejectAsync("ZIP manifest missing asset",()=>{const z=P.makeZip(new Map([["manifest.json",new TextEncoder().encode(JSON.stringify({...m,preview:"absent.png"}))]]));return P.readZip(z);});
 await rejectAsync("case-insensitive duplicate names",()=>P.readZip(P.makeZip(new Map([["manifest.json",new TextEncoder().encode(JSON.stringify(m))],["preview.png",preview],["Preview.png",preview]]))));
 await rejectAsync("script file rejected",()=>P.readZip(P.makeZip(new Map([["manifest.json",new TextEncoder().encode(JSON.stringify(m))],["preview.png",preview],["payload.js",new Uint8Array([1])]]))));
 await rejectAsync("ZIP CRC rejected",()=>{const b=zip.slice();b[42]^=1;return P.readZip(b);});
 const s=P.style({}),candidates=[{text:"你好",comment:"nǐ hǎo"},{text:"思念你",comment:""},{text:"岸",comment:"àn"}],measure=(text,point)=>text.length*point*.6;
 const above=G.layout(s,[],candidates,true,measure),no=G.layout(s,[],candidates,false,measure),missing=G.layout(s,[],candidates.map(c=>({...c,comment:""})),true,measure);
 check("mixed annotation stable baseline",()=>assert.equal(new Set(above.cells.map(c=>c.textY)).size,1));
 check("empty page collapses annotation",()=>{assert.equal(missing.height,no.height);assert.equal(missing.hasAnnotation,false);});
 check("annotation smaller than candidate",()=>assert.ok(above.comment<above.font));
 check("plain frame shorter",()=>assert.ok(above.height>no.height));
 const layers=[{path:"x.png",anchor:"bottom_right",x:10,y:-90,width:120,height:100,z:1},{path:"y.png",anchor:"top_left",x:-30,y:-50,width:10,height:20,z:-1}];
 const g=G.layout(s,layers,candidates,true,measure);
 check("anchor uses actual frame height",()=>assert.equal(g.layers[0].top,g.height-90));
 check("canvas includes decorations",()=>{assert.equal(g.left,-30);assert.equal(g.top,-50);assert.equal(g.right,g.width+130);});
 check("decoration is outside candidate hit region",()=>assert.equal(G.hit(g,g.width+20,0),-1));
 check("candidate hit agrees with cell",()=>assert.equal(G.hit(g,g.cells[1].x+1,g.rowTop+1),1));
 check("fit preserves ratio bottom alignment",()=>assert.deepEqual(G.fit({left:0,top:0,width:100,height:100},{width:200,height:100}),{x:0,y:50,width:100,height:50}));
 // Independent standard Deflate encoding exercises the browser/native archive compatibility.
 const deflateZip=require("./designer_zip_fixture.cjs");
 const compressed=deflateZip(new Map([["manifest.json",new TextEncoder().encode(JSON.stringify(m))],["preview.png",preview]]));
 const deflated=await P.readZip(compressed,(bytes,limit)=>{const output=zlib.inflateRawSync(bytes,{maxOutputLength:limit});return new Uint8Array(output);});
 check("Deflate package round trip",()=>assert.deepEqual(deflated.manifest,m));
 const beside=G.layout({...s,annotationPosition:"beside"},[],candidates,true,measure);
 check("legacy beside annotation occupies one row",()=>{assert.equal(beside.height,no.height);assert.ok(beside.cells[0].width>no.cells[0].width);});
 for(const folder of["studio-demo","studio-plain"])if(fs.existsSync(path.join(__dirname,"../skins",folder,"manifest.json"))){
  const dir=path.join(__dirname,"../skins",folder),manifest=JSON.parse(fs.readFileSync(path.join(dir,"manifest.json"),"utf8")),data=new Map(fs.readdirSync(dir).filter(n=>n.endsWith(".png")).map(n=>[n,new Uint8Array(fs.readFileSync(path.join(dir,n)))]));
  check("example "+folder+" validates",()=>P.validate(manifest,data));
 }
 console.log(passed+" designer tests passed.");
})().catch(e=>{console.error(e);process.exitCode=1;});
