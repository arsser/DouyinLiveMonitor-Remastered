// 对比 JS MD5 与 Node MD5 对字符串/字节的哈希，找出字节编码差异
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

let code = readFileSync(fileURLToPath(new URL('../../sign.js', import.meta.url)), 'utf8');
// 在 _0x1633f2 定义前（此时 _0x5dd467 已定义）暴露为全局
code = code.replace('function _0x1633f2(_0x48f290, _0x504655, _0x4fa808, _0x1a5c57, _0x3a4737) {',
  'globalThis.__md5 = _0x5dd467;\n        function _0x1633f2(_0x48f290, _0x504655, _0x4fa808, _0x1a5c57, _0x3a4737) {');
const ctx = vm.createContext({});
vm.runInContext(code, ctx, { filename: 'sign.js' });
const md5 = vm.runInContext('__md5', ctx);

function dump(label, val) {
  console.log(label, '=', JSON.stringify(val));
}

// 1. MD5('') 
dump("JS md5('')", md5(''));
console.log("Node md5('') =", createHash('md5').update('', 'utf8').digest('hex'));

// 2. MD5('abc')
dump("JS md5('abc')", md5('abc'));
console.log("Node md5('abc') =", createHash('md5').update('abc', 'utf8').digest('hex'));

// 3. 关键：encode(MD5('')) 的字符串，JS MD5 vs Node MD5
const mde = 'd41d8cd98f00b204e9800998ecf8427e';
// hex encode
let enc = '';
for (const ch of mde) enc += ch.charCodeAt(0).toString(16).padStart(2, '0');
console.log('enc =', enc);
dump("JS md5(enc)", md5(enc));
console.log("Node md5(enc) =", createHash('md5').update(enc, 'utf8').digest('hex'));

// 4. 关键：JS MD5 对字节数组
const mdb = Buffer.from('b5f7f2b3e491718deb69195be3284b1b', 'hex');
dump("JS md5(bytes16)", md5(mdb));
console.log("Node md5(bytes16) =", createHash('md5').update(mdb).digest('hex'));

// 5. 看 JS md5 返回类型（string 还是 Uint8Array）
const r = vm.runInContext('typeof __md5("abc")', ctx);
console.log('typeof md5("abc") =', r);
const r2 = vm.runInContext('__md5("abc") instanceof Uint8Array', ctx);
console.log('md5("abc") instanceof Uint8Array =', r2);
