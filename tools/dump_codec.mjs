import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');
const needle = "var _0x55de18 = function (_0x33a277) {";
code = code.replace(needle,
  "globalThis.__codec = { alpha: _0x100715, enc: _0x1f510e, dec: _0x37750d };\n" + needle);

const ctx = vm.createContext({ __codec: null });
vm.runInContext(code, ctx, { filename: 'sign.js' });

const out = vm.runInContext(`
(() => {
  const r = { encDigit: [], encHex: [], decDigit: [], decHex: [] };
  for (let i = 48; i <= 57; i++) { r.encDigit.push([String.fromCharCode(i), __codec.enc[i]]); r.decDigit.push([String.fromCharCode(i), __codec.dec[i]]); }
  for (let i = 97; i <= 102; i++) { r.encHex.push([String.fromCharCode(i), __codec.enc[i]]); r.decHex.push([String.fromCharCode(i), __codec.dec[i]]); }
  r.alpha = __codec.alpha;
  return r;
})()
`, ctx);
console.log('alpha =', JSON.stringify(out.alpha));
console.log('enc 数字0-9:', JSON.stringify(out.encDigit));
console.log('enc 字母a-f:', JSON.stringify(out.encHex));
console.log('dec 数字0-9:', JSON.stringify(out.decDigit));
console.log('dec 字母a-f:', JSON.stringify(out.decHex));
