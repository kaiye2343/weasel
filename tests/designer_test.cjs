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
 check("editor highlight selection keeps its own boundary contract",()=>{const c=g.cells[0];assert.equal(G.hit(g,c.x+c.width,g.rowTop+1),0);assert.equal(G.hit(g,c.x+1,g.rowTop+c.height),0);});
 check("fit preserves ratio bottom alignment",()=>assert.deepEqual(G.fit({left:0,top:0,width:100,height:100},{width:200,height:100}),{x:0,y:50,width:100,height:50}));
 // Independent standard Deflate encoding exercises the browser/native archive compatibility.
 const deflateZip=require("./designer_zip_fixture.cjs");
 const compressed=deflateZip(new Map([["manifest.json",new TextEncoder().encode(JSON.stringify(m))],["preview.png",preview]]));
 const deflated=await P.readZip(compressed,(bytes,limit)=>{const output=zlib.inflateRawSync(bytes,{maxOutputLength:limit});return new Uint8Array(output);});
 check("Deflate package round trip",()=>assert.deepEqual(deflated.manifest,m));
 const beside=G.layout({...s,annotationPosition:"beside"},[],candidates,true,measure);
 check("legacy beside annotation occupies one row",()=>{assert.equal(beside.height,no.height);assert.ok(beside.cells[0].width>no.cells[0].width);});
 const pref=G.preferences(),originalStyle=JSON.stringify(s),effective=G.effective(s,pref);
 check("actual preview defaults inherit source style",()=>{assert.deepEqual(effective,s);assert.equal(pref.candidatePlacement,"native");});
 check("old explicit preview overrides remain active",()=>{const p=G.preferences({annotationPosition:"above",fontMode:"user",fontPoint:18,commentFontPoint:11,density:"compact",candidatePlacement:"auto"});const x=G.effective(s,p);assert.equal(x.fontPoint,18);assert.equal(x.commentFontPoint,11);assert.equal(x.candidateSpacing,8);assert.equal(x.annotationPosition,"above");assert.equal(p.candidatePlacement,"auto");});
 check("partial preview preferences inherit omitted fields",()=>assert.deepEqual(G.effective(s,G.preferences({annotationPosition:"beside"})),{...s,annotationPosition:"beside"}));
 check("preview preferences never mutate exported skin style",()=>assert.equal(JSON.stringify(s),originalStyle));
 check("follow skin uses original font and density",()=>{const x=G.effective(s,G.preferences({fontMode:"skin",density:"skin",annotationPosition:"skin"}));assert.equal(x.fontPoint,s.fontPoint);assert.equal(x.candidateSpacing,s.candidateSpacing);});
 check("legacy hidden display preferences remain hidden",()=>assert.equal(G.effective(s,G.preferences({schemaVersion:1,annotationPosition:"hidden"})).annotationPosition,"hidden"));
 check("reserved empty annotation retains row height",()=>assert.ok(G.layout(s,[],candidates.map(c=>({...c,comment:""})),true,measure,{emptyAnnotationMode:"reserve"}).height>missing.height));
 reject("invalid preview preferences",()=>G.preferences({fontPoint:200}));
 reject("unknown preview preferences",()=>G.preferences({script:"run"}));
 check("current display export accepts placement without changing skin geometry",()=>{const p=G.preferences({candidatePlacement:"above",candidateOffsetX:-20,candidateGap:18,avoidDecorations:false});assert.equal(p.candidateOffsetX,-20);assert.deepEqual(G.effective(s,p),G.effective(s,G.preferences()));});
 reject("invalid placement export rejected",()=>G.preferences({candidateGap:-1}));
 const fitted=G.layout({...s,shadowRadius:32},layers,candidates,true,measure);
 check("image anchor is independent of shadow canvas",()=>assert.deepEqual(fitted.layers.map(l=>[l.left-fitted.width,l.top-fitted.height]),g.layers.map(l=>[l.left-g.width,l.top-g.height])));
 for(const folder of["studio-demo","studio-plain"])if(fs.existsSync(path.join(__dirname,"../skins",folder,"manifest.json"))){
  const dir=path.join(__dirname,"../skins",folder),manifest=JSON.parse(fs.readFileSync(path.join(dir,"manifest.json"),"utf8")),data=new Map(fs.readdirSync(dir).filter(n=>n.endsWith(".png")).map(n=>[n,new Uint8Array(fs.readFileSync(path.join(dir,n)))]));
  check("example "+folder+" validates",()=>P.validate(manifest,data));
 }
 console.log(passed+" designer tests passed.");
})().catch(e=>{console.error(e);process.exitCode=1;});
