import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');

// 在 _0x4145f8 内、return _0x389396 前注入 dump
const needle = "return _0x37237c += String[_0x46d631(0x1e2)](_0x30c511),";
if (code.split(needle).length - 1 !== 1) { console.error('needle 异常'); process.exit(1); }
code = code.replace(needle,
  "globalThis.__d4145 = { header: _0x30c511, rnd: _0x280d0b, buf: Array.from(_0x1054b9), rc4out: Array.from(_0x250194).map(c=>c.charCodeAt(0)), result: Array.from(_0x37237c).map(c=>c.charCodeAt(0)) };\n" + needle);

const ctx = vm.createContext({ __d4145: null, Math: Object.assign(Object.create(Math), { random: () => 0 }) });
vm.runInContext(code, ctx, { filename: 'sign.js' });
const get_sign = vm.runInContext('get_sign', ctx);

for (const md5 of ['b5f7f2b3e491718deb69195be3284b1b', '51b61920a08ead94f29e425711c5ba5c']) {
  const out = get_sign(md5);
  const d = vm.runInContext('__d4145', ctx);
  console.log('md5 =', md5, '=> X-Bogus =', out);
  console.log('  header =', d.header, ' rnd =', d.rnd);
  console.log('  buf    =', JSON.stringify(d.buf));
  console.log('  rc4out =', JSON.stringify(d.rc4out));
  console.log('  result =', JSON.stringify(d.result));
}
