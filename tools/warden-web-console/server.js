#!/usr/bin/env node
'use strict';
// Warden browser console prototype. Host-side only; does not boot Warden.
const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const HOST = process.env.WARDEN_WEB_HOST || '127.0.0.1';
const PORT = Number(process.env.WARDEN_WEB_PORT || 8787);
const ORIGIN = process.env.WARDEN_WEB_ORIGIN || `http://localhost:${PORT}`;
const PASSWORD = process.env.WARDEN_WEB_PASSWORD || '';
const INGEST = process.env.WARDEN_INGEST_TOKEN || '';
if (PASSWORD.length < 12 || INGEST.length < 24 || !Number.isInteger(PORT) || PORT < 1 || PORT > 65535) {
  console.error('Set WARDEN_WEB_PASSWORD (12+ chars) and WARDEN_INGEST_TOKEN (24+ chars), and a valid port.');
  process.exit(2);
}
const WIDTH = 320, HEIGHT = 200, FRAME_BYTES = WIDTH * HEIGHT * 4;
const salt = crypto.randomBytes(32);
const passwordHash = crypto.scryptSync(PASSWORD, salt, 64);
const sessions = new Map();
const attempts = new Map();
const clients = new Set();
const inputs = [];
let currentFrame = Buffer.alloc(FRAME_BYTES);
const expiryMs = 20 * 60_000;
const hexToken = () => crypto.randomBytes(32).toString('hex');
const same = (a, b) => {
  const x = Buffer.from(String(a)), y = Buffer.from(String(b));
  return x.length === y.length && crypto.timingSafeEqual(x, y);
};
const secureHeaders = {
  'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer', 'X-Frame-Options': 'DENY',
  'Content-Security-Policy': "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self' ws: wss:; img-src 'self'; base-uri 'none'; frame-ancestors 'none'"
};
function reply(res, status, data, type='application/json') {
  res.writeHead(status, { ...secureHeaders, 'Content-Type': type });
  res.end(typeof data === 'string' || Buffer.isBuffer(data) ? data : JSON.stringify(data));
}
function body(req, max) {
  return new Promise((resolve, reject) => {
    const pieces = []; let n = 0;
    req.on('data', c => { n += c.length; if (n > max) { reject(new Error('too large')); req.destroy(); } else pieces.push(c); });
    req.on('end', () => resolve(Buffer.concat(pieces)));
    req.on('error', reject);
  });
}
function allowedOrigin(req) { return req.headers.origin === ORIGIN; }
function validIngest(req) { return same(req.headers['x-warden-ingest-token'] || '', INGEST); }
function websocketSend(socket, opcode, data) {
  if (!socket.writable || socket.writableLength > 1024 * 1024) { socket.destroy(); return; }
  const b = Buffer.isBuffer(data) ? data : Buffer.from(data);
  const h = b.length < 126 ? Buffer.from([0x80 | opcode, b.length]) : b.length <= 65535
    ? Buffer.from([0x80 | opcode, 126, b.length >> 8, b.length & 255])
    : Buffer.from([0x80 | opcode, 127, 0, 0, 0, 0, b.length >>> 24, (b.length >>> 16) & 255, (b.length >>> 8) & 255, b.length & 255]);
  socket.write(Buffer.concat([h, b]));
}
function acceptInput(raw, socket) {
  let v;
  try { v = JSON.parse(raw.toString('utf8')); } catch { return socket.destroy(); }
  const key = v && v.kind === 'key' && typeof v.code === 'string'
    && /^(ArrowUp|ArrowDown|ArrowLeft|ArrowRight|Space|Enter|Escape|Key[A-Z]|Digit[0-9])$/.test(v.code)
    && typeof v.down === 'boolean';
  const pointer = v && v.kind === 'pointer' && Number.isInteger(v.x) && Number.isInteger(v.y)
    && v.x >= 0 && v.x < WIDTH && v.y >= 0 && v.y < HEIGHT
    && Number.isInteger(v.buttons) && v.buttons >= 0 && v.buttons <= 7;
  if (!key && !pointer) return socket.destroy();
  if (inputs.length >= 256) inputs.shift();
  inputs.push(key ? {kind:'key',code:v.code,down:v.down} : {kind:'pointer',x:v.x,y:v.y,buttons:v.buttons});
}
function receiveWs(socket, incoming) {
  socket.wsBuffer = Buffer.concat([socket.wsBuffer || Buffer.alloc(0), incoming]);
  if (socket.wsBuffer.length > 8192) return socket.destroy();
  while (socket.wsBuffer.length >= 2) {
    const b = socket.wsBuffer, op = b[0] & 15, fin = !!(b[0] & 0x80);
    if (!fin || (b[0] & 0x70) || !(b[1] & 0x80)) return socket.destroy();
    let len = b[1] & 127, head = 2;
    if (len === 126) { if (b.length < 4) return; len = b.readUInt16BE(2); head = 4; }
    if (len === 127 || len > 4096) return socket.destroy();
    if (b.length < head + 4 + len) return;
    const mask = b.subarray(head, head+4);
    const payload = Buffer.from(b.subarray(head+4, head+4+len));
    for (let i=0; i<len; i++) payload[i] ^= mask[i%4];
    socket.wsBuffer = b.subarray(head+4+len);
    if (op === 8) return socket.end();
    if (op === 9) websocketSend(socket, 10, payload);
    else if (op === 1) {
      const now = Date.now();
      if (!socket.inputWindow || now - socket.inputWindow > 1000) { socket.inputWindow = now; socket.inputCount = 0; }
      if (++socket.inputCount > 120) return socket.destroy();
      acceptInput(payload, socket);
    } else if (op !== 10) return socket.destroy();
  }
}
const page = fs.readFileSync(path.join(__dirname,'index.html'));
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url || '/', 'http://localhost');
  if (req.method === 'GET' && url.pathname === '/') return reply(res, 200, page, 'text/html; charset=utf-8');
  if (req.method === 'POST' && url.pathname === '/api/login') {
    if (!allowedOrigin(req)) return reply(res,403,{error:'origin'});
    const ip=req.socket.remoteAddress || 'unknown', now=Date.now();
    const state=attempts.get(ip) || {start:now,count:0};
    if(now-state.start>600000) {state.start=now;state.count=0;}
    if(state.count>=5) return reply(res,429,{error:'rate limited'});
    let parsed; try { parsed=JSON.parse((await body(req,2048)).toString('utf8')); } catch { return reply(res,400,{error:'invalid request'}); }
    const candidate = typeof parsed.password === 'string' && parsed.password.length < 1024
      ? crypto.scryptSync(parsed.password,salt,64) : Buffer.alloc(64);
    if (!crypto.timingSafeEqual(candidate,passwordHash)) {state.count++;attempts.set(ip,state);return reply(res,401,{error:'unauthorized'});}
    attempts.delete(ip);
    const token=hexToken(); sessions.set(token,now+expiryMs);
    return reply(res,200,{token,expires_in:expiryMs/1000});
  }
  if (url.pathname === '/api/frame' && req.method === 'POST') {
    if (!validIngest(req)) return reply(res,401,{error:'unauthorized'});
    if (Number(req.headers['content-length']) !== FRAME_BYTES) return reply(res,413,{error:'frame must be 320x200 RGBA'});
    let frame; try { frame=await body(req,FRAME_BYTES); } catch {return;}
    if (frame.length !== FRAME_BYTES) return reply(res,400,{error:'short frame'});
    currentFrame=frame;
    for (const sock of clients) websocketSend(sock,2,frame);
    return reply(res,200,{ok:true,viewers:clients.size});
  }
  if (url.pathname === '/api/input' && req.method === 'GET') {
    if (!validIngest(req)) return reply(res,401,{error:'unauthorized'});
    return reply(res,200,{events:inputs.splice(0,256)});
  }
  return reply(res,404,{error:'not found'});
});
server.on('upgrade',(req,socket) => {
  const url = new URL(req.url || '/', 'http://localhost');
  const protocols = String(req.headers['sec-websocket-protocol'] || '').split(',').map(s=>s.trim());
  const auth=protocols.find(s=>/^warden\.auth\.[0-9a-f]{64}$/.test(s));
  const token=auth && auth.slice('warden.auth.'.length);
  const expiry=token && sessions.get(token);
  // One-use token; no credentials in URLs, referrers or browser storage.
  if (token) sessions.delete(token);
  const key=req.headers['sec-websocket-key'];
  if (url.pathname !== '/ws' || !allowedOrigin(req) || !expiry || expiry<Date.now() ||
      clients.size>=4 || !protocols.includes('warden.v1') ||
      req.headers.upgrade?.toLowerCase() !== 'websocket' ||
      req.headers['sec-websocket-version'] !== '13' ||
      typeof key !== 'string' || !/^[A-Za-z0-9+/]{22}==$/.test(key)) {
    socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
    return;
  }
  const accept=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\nSec-WebSocket-Protocol: warden.v1\r\n\r\n');
  clients.add(socket);
  socket.on('data',chunk=>receiveWs(socket,chunk));
  socket.on('close',()=>clients.delete(socket));
  socket.on('error',()=>clients.delete(socket));
  websocketSend(socket,2,currentFrame);
});
server.listen(PORT,HOST,()=>console.log(`Warden web console listening at http://${HOST}:${PORT}; browser origin ${ORIGIN}; host-side only`));
