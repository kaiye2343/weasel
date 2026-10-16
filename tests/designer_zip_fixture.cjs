"use strict";
const zlib=require("node:zlib"),P=require("../designer/package.js");
 function deflateZip(entries){
  const original=P.makeZip(entries),view=new DataView(original.buffer),e=original.length-22,cd=view.getUint32(e+16,true),parts=[],central=[];let cp=cd,offset=0;
  for(const [name,bytes]of entries){
   const nl=view.getUint16(cp+28,true),local=view.getUint32(cp+42,true),header=original.slice(local,local+30+nl),h=new DataView(header.buffer),payload=zlib.deflateRawSync(bytes);
   h.setUint16(8,8,true);h.setUint32(18,payload.length,true);
   const record=original.slice(cp,cp+46+nl),r=new DataView(record.buffer);r.setUint16(10,8,true);r.setUint32(20,payload.length,true);r.setUint32(42,offset,true);
   parts.push(header,payload);central.push(record);offset+=header.length+payload.length;cp+=record.length;
  }
  const end=original.slice(e),ev=new DataView(end.buffer);ev.setUint32(16,offset,true);return new Uint8Array(Buffer.concat([...parts,...central,end]));
 }

module.exports=deflateZip;
