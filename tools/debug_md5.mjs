// dump _0x5dd467('') 及中间数组，判断 MD5 与 codec 的精确语义
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');

const needle = "var _0x136ce1 = _0x42e709['decode'](_0x5dd467(_0x42e709['decode'](_0x3a4737)));";
if (code.split(needle).length - 1 !== 1) { console.error('注入点异常'); process.exit(1); }
code = code.replace(needle,
  needle + "\nglobalThis.__dbg = { md5_empty: _0x21ca02, enc: _0x42e709['encode'](_0x21ca02), t1: Array.from(_0x13f487), t2: Array.from(_0x136ce1), dec_md5: Array.from(_0x42e709['decode'](_0x3a4737)) };");

const ctx = vm.createContext({ __dbg: null, Math: Object.assign(Object.create(Math), { random: () => 0 }) });
vm.runInContext(code, ctx, { filename: 'sign.js' });
const get_sign = vm.runInContext('get_sign', ctx);
const md5 = 'b5f7f2b3e491718deb69195be3284b1b';
get_sign(md5);
const d = vm.runInContext('__dbg', ctx);
console.log('md5_empty =', JSON.stringify(d.md5_empty));
console.log('enc(md5_empty) =', JSON.stringify(d.enc));
console.log('t1 (13f487) =', JSON.stringify(d.t1));
console.log('t2 (136ce1) =', JSON.stringify(d.t2));
console.log('dec_md5 (hexdec of md5) =', JSON.stringify(d.dec_md5));
