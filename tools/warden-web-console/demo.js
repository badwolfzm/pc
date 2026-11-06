#!/usr/bin/env node
'use strict';
// Synthetic source to test the web bridge. NOT Warden/QEMU gameplay.
const http=require('node:http');
const TOKEN=process.env.WARDEN_INGEST_TOKEN || '';
const HOST=process.env.WARDEN_WEB_HOST || '127.0.0.1';
const PORT=Number(process.env.WARDEN_WEB_PORT || 8787);
if(TOKEN.length<24) {console.error('Set WARDEN_INGEST_TOKEN first');process.exit(2);}
const W=320,H=200,frame=Buffer.alloc(W*H*4);let n=0;
function request(method,url,data,cb){
 const req=http.request({host:HOST,port:PORT,path:url,method,headers:{
  'X-Warden-Ingest-Token':TOKEN,
  ...(data?{'Content-Length':data.length,'Content-Type':'application/octet-stream'}:{})}},res=>{
    let s='';res.on('data',c=>s+=c);res.on('end',()=>cb(res.statusCode,s));
  });
 req.on('error',e=>console.error(e.message));
 if(data)req.write(data);req.end();
}
setInterval(()=>{
 for(let y=0;y<H;y++)for(let x=0;x<W;x++){
  const i=(y*W+x)*4;
  frame[i]=(x+n)%256;frame[i+1]=(y+n)%256;frame[i+2]=(x^y^n)%256;frame[i+3]=255;
 }
 request('POST','/api/frame',frame,(code)=>{if(code!==200)console.error('Frame rejected',code);});
 request('GET','/api/input',null,(code,data)=>{
  if(code===200){try{const events=JSON.parse(data).events;if(events.length)console.log('Browser input:',events);}catch{}}
 });
 n=(n+2)%256;
},100);
console.log('Synthetic test frames at 10 fps (not the Warden guest). Stop with Ctrl-C.');
