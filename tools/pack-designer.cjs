"use strict";
const fs=require("node:fs"),path=require("node:path"),P=require("../designer/package.js");
const root=path.resolve(__dirname,".."),out=path.resolve(process.argv[2]||path.join(root,"output/archives"));fs.mkdirSync(out,{recursive:true});
const entries=new Map();for(const name of["index.html","style.css","package.js","geometry.js","app.js","README.md"])entries.set(name,new Uint8Array(fs.readFileSync(path.join(root,"designer",name))));
for(const folder of["studio-demo","studio-plain"]){
 const dir=path.join(root,"skins",folder),manifest=JSON.parse(fs.readFileSync(path.join(dir,"manifest.json"),"utf8")),assets=new Map(fs.readdirSync(dir).filter(n=>n.endsWith(".png")).map(n=>[n,new Uint8Array(fs.readFileSync(path.join(dir,n)))]));
 const pack=P.packageBytes(manifest,assets),name=folder+"-"+manifest.version+".rimeskin";
 fs.writeFileSync(path.join(out,name),pack);entries.set("samples/"+name,pack);
}
entries.set("skin-format.md",new Uint8Array(fs.readFileSync(path.join(root,"docs/skin-format.md"))));
entries.set("rimeskin.schema.json",new Uint8Array(fs.readFileSync(path.join(root,"docs/rimeskin.schema.json"))));
fs.writeFileSync(path.join(out,"rime-skin-studio.zip"),P.makeZip(entries));
// Included in the second, complete installer build under SettingsCenter/Designer.
const installed=path.join(out,"tools/Designer");fs.mkdirSync(installed,{recursive:true});
for(const [name,bytes]of entries){const target=path.join(installed,...name.split("/"));fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);}
console.log("Packaged offline designer and validated sample skins: "+out);
