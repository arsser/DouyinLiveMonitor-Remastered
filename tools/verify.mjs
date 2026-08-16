// 用干净 JS 重实现 X-Bogus，与 sign.js（Math.random 固定为 0）精确对拍
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
const code = readFileSync(signPath, 'utf8');

const S1 = 'Dkdpgh4ZKsQB80/Mfvw36XI1R25-WUAlEi7NLboqYTOPuzmFjJnryx9HVGcaStCe=';

function md5Hex(str) { return createHash('md5').update(str, 'utf8').digest('hex'); }
function md5BytesOfHex(hex) { return createHash('md5').update(Buffer.from(hex, 'hex')).digest(); }

function hexEncode(str) {
  let out = '';
  for (let i = 0; i < str.length; i++) out += str.charCodeAt(i).toString(16).padStart(2, '0');
  return out;
}
function hexDecode(str) {
  return Buffer.from(str, 'hex');
}

function rc4(key, data) {
  const S = new Uint8Array(256);
  for (let i = 0; i < 256; i++) S[i] = i;
  let j = 0;
  for (let i = 0; i < 256; i++) {
    j = (j + S[i] + key.charCodeAt(i % key.length)) & 255;
    [S[i], S[j]] = [S[j], S[i]];
  }
  let i = 0; j = 0;
  const out = new Uint8Array(data.length);
  for (let k = 0; k < data.length; k++) {
    i = (i + 1) & 255;
    j = (j + S[i]) & 255;
    [S[i], S[j]] = [S[j], S[i]];
    out[k] = data.charCodeAt(k) ^ S[(S[i] + S[j]) & 255];
  }
  return String.fromCharCode(...out);
}

function base64_s1(bytesStr) {
  let out = '';
  const len = bytesStr.length;
  for (let i = 0; i < len; i += 3) {
    const b0 = bytesStr.charCodeAt(i);
    const b1 = i + 1 < len ? bytesStr.charCodeAt(i + 1) : 0;
    const b2 = i + 2 < len ? bytesStr.charCodeAt(i + 2) : 0;
    const n = (b0 << 16) | (b1 << 8) | b2;
    out += S1[(n >> 18) & 63] + S1[(n >> 12) & 63] + S1[(n >> 6) & 63] + S1[n & 63];
  }
  return out;
}

// 我的重实现（随机数固定为 0）
function myXbogus(md5hex) {
  const rand = () => 0;   // 固定
  const key = 1;          // kWebsocket
  const initialized = 0;
  const t = 0;

  // header
  const header = (key << 6) | (initialized << 5) | ((1 & Math.floor(100 * rand())) << 4);

  // 9-byte buffer
  const bogusIndex = 1, envcode = 1, ubcode = 14;
  const bytes = new Uint8Array(9);
  bytes[0] = (t << 6) | (bogusIndex & 63);
  bytes[1] = (envcode >> 8) & 255;
  bytes[2] = envcode & 255;
  bytes[3] = ubcode;

  // t1 = decode(MD5(decode(MD5(''))))  ;  t2 = decode(MD5(decode(md5)))
  const t1 = hexDecode(md5Hex(Buffer.from(md5Hex(''), 'hex')));
  bytes[4] = t1[14]; bytes[5] = t1[15];
  const t2 = hexDecode(md5Hex(Buffer.from(md5hex, 'hex')));
  bytes[6] = t2[14]; bytes[7] = t2[15];
  bytes[8] = Math.floor(255 * rand()) & 255;

  // checksum
  let checksum = 0;
  for (let i = 0; i < 9; i++) checksum ^= bytes[i];
  const buf = new Uint8Array(10);
  for (let i = 0; i < 9; i++) buf[i] = bytes[i];
  buf[9] = checksum;

  const rnd = Math.floor(255 * rand()) & 255;
  const enc = rc4(String.fromCharCode(rnd), String.fromCharCode(...buf));
  const raw = String.fromCharCode(header) + String.fromCharCode(rnd) + enc;
  return base64_s1(raw);
}

const inputs = ['b5f7f2b3e491718deb69195be3284b1b', '51b61920a08ead94f29e425711c5ba5c', '00000000000000000000000000000000', 'ffffffffffffffffffffffffffffffff', '0123456789abcdef0123456789abcdef'];
let ok = true;
for (const md5 of inputs) {
  // 每个输入新建 context（等价 Python 每次新建 MiniRacer，bogusIndex 恒为 1）
  const ctx = vm.createContext({ Math: Object.assign(Object.create(Math), { random: () => 0 }) });
  vm.runInContext(code, ctx, { filename: 'sign.js' });
  const get_sign = vm.runInContext('get_sign', ctx);
  const js = get_sign(md5);
  const my = myXbogus(md5);
  const pass = js === my;
  ok = ok && pass;
  console.log(`${pass ? 'PASS' : 'FAIL'}  md5=${md5}\n  js=${js}\n  my=${my}`);
}
console.log('\n总结:', ok ? '全部一致 ✅' : '存在不一致 ❌');
