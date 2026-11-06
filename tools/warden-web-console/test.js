#!/usr/bin/env node
'use strict';
const assert=require('node:assert/strict');
const http=require('node:http');
const net=require('node:net');
const crypto=require('node:crypto');
const {spawn}=require('node:child_process');
const path=require('node:path');

const PORT=20000+Math.floor(Math.random()*20000);
const PASSWORD='only-a-local-test-password';
const INGEST='test-ingest-token-0000000000000001';
const ORIGIN=`http://localhost:${PORT}`;
const server=spawn(process.execPath,[path.join(__dirname,'server.js')],{
  env:{...process.env,WARDEN_WEB_PORT:String(PORT),WARDEN_WEB_PASSWORD:PASSWORD,
    WARDEN_INGEST_TOKEN:INGEST,WARDEN_WEB_ORIGIN:ORIGIN},
  stdio:['ignore','pipe','pipe']
});
let errors='';server.stderr.on('data',c=>errors+=c);
function req(method,url,body,headers={}){
 return new Promise((resolve,reject)=>{
  const r=http.request({hostname:'127.0.0.1',port:PORT,path:url,method,headers},res=>{
   const b=[];res.on('data',c=>b.push(c));res.on('end',()=>resolve({code:res.statusCode,headers:res.headers,body:Buffer.concat(b)}));
  });r.on('error',reject);if(body)r.write(body);r.end();
 });
}
async function untilReady(){
 for(let i=0;i<60;i++){try{return await req('GET','/');}catch{await new Promise(r=>setTimeout(r,50));}}
 throw Error('Server failed to start: '+errors);
}
function upgrade(token,origin=ORIGIN){
 return new Promise((resolve,reject)=>{
  const sock=net.connect(PORT,'127.0.0.1');let buf=Buffer.alloc(0);
  sock.on('error',reject);
  sock.on('connect',()=>{
   const key=crypto.randomBytes(16).toString('base64');
   sock.write('GET /ws HTTP/1.1\r\nHost: localhost:'+PORT+'\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: '+key+'\r\nOrigin: '+origin+'\r\nSec-WebSocket-Protocol: warden.v1, warden.auth.'+token+'\r\n\r\n');
  });
  sock.on('data',d=>{buf=Buffer.concat([buf,d]);const k=buf.indexOf('\r\n\r\n');if(k>=0){sock.removeAllListeners('data');resolve({sock,response:buf.subarray(0,k).toString(),trailing:buf.subarray(k+4)});}});
  setTimeout(()=>reject(Error('upgrade timeout')),3000).unref();
 });
}
function maskedText(obj){
 const payload=Buffer.from(JSON.stringify(obj));
 const mask=crypto.randomBytes(4);
 const head=payload.length<126?Buffer.from([0x81,0x80|payload.length])
   :Buffer.from([0x81,0xfe,payload.length>>8,payload.length&255]);
 const data=Buffer.from(payload);
 for(let i=0;i<data.length;i++)data[i]^=mask[i%4];
 return Buffer.concat([head,mask,data]);
}
const pause=(ms)=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 try{
  const home=await untilReady();assert.equal(home.code,200);
  assert.match(home.body.toString(),/<canvas/);
  const noOrigin=await req('POST','/api/login',JSON.stringify({password:PASSWORD}),{'Content-Type':'application/json'});
  assert.equal(noOrigin.code,403);
  const bad=await req('POST','/api/login',JSON.stringify({password:'wrong'}),{'Origin':ORIGIN});
  assert.equal(bad.code,401);
  const good=await req('POST','/api/login',JSON.stringify({password:PASSWORD}),{'Origin':ORIGIN});
  assert.equal(good.code,200);
  const token=JSON.parse(good.body.toString()).token;
  assert.match(token,/^[a-f0-9]{64}$/);
  assert.equal((await req('POST','/api/frame',Buffer.alloc(320*200*4),{'Content-Length':320*200*4})).code,401);
  assert.equal((await req('POST','/api/frame',Buffer.alloc(10),{'Content-Length':10,'X-Warden-Ingest-Token':INGEST})).code,413);
  const image=Buffer.alloc(320*200*4,42);
  assert.equal((await req('POST','/api/frame',image,{'Content-Length':image.length,'X-Warden-Ingest-Token':INGEST})).code,200);
  const ws=await upgrade(token);assert.match(ws.response,/101 Switching Protocols/);
  assert.match(ws.response,/Sec-WebSocket-Protocol: warden.v1/);
  ws.sock.write(maskedText({kind:'key',code:'ArrowUp',down:true}));
  await pause(40);
  const accepted=await req('GET','/api/input',null,{'X-Warden-Ingest-Token':INGEST});
  assert.equal(accepted.code,200);
  assert.deepEqual(JSON.parse(accepted.body.toString()).events,[{kind:'key',code:'ArrowUp',down:true}]);
  ws.sock.write(maskedText({kind:'key',code:'UnknownOSCommand',down:true}));
  await pause(40);
  const rejected=await req('GET','/api/input',null,{'X-Warden-Ingest-Token':INGEST});
  assert.deepEqual(JSON.parse(rejected.body.toString()).events,[]);
  ws.sock.destroy();
  const replay=await upgrade(token);assert.match(replay.response,/403 Forbidden/);replay.sock.destroy();
  const invalid=await upgrade('0'.repeat(64),'https://attacker.invalid');
  assert.match(invalid.response,/403 Forbidden/);invalid.sock.destroy();
  const input=await req('GET','/api/input',null,{'X-Warden-Ingest-Token':INGEST});
  assert.equal(input.code,200);assert.deepEqual(JSON.parse(input.body.toString()).events,[]);
  console.log('PASS: login/origin, token one-use, framebuffer size, ingest auth, WebSocket upgrade, masked key input filtering and polling');
 }catch(e){console.error('FAIL:',e);process.exitCode=1;}
 finally{server.kill('SIGTERM');}
})();
