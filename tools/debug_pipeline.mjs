// 注入 dump，观察 _0x1633f2 的输入与中间状态
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');

const needle = '_0x4145f8(_0xeefda2, _0x1ffaa7);';
if (code.split(needle).length - 1 !== 1) { console.error('注入点数量异常:', code.split(needle).length - 1); process.exit(1); }
code = code.replace(needle,
  'globalThis.__dbg = { header: _0xeefda2, bytes: Array.from(_0x1ffaa7), bogusIndex: _0x6caf[\'bogusIndex\'], envcode: _0x6caf[\'envcode\'], ubcode: _0xe06992[\'ubcode\'], initialized: _0x462335[\'initialized\'], key: _0x48f290, initFlag: _0x504655 }; _0x4145f8(_0xeefda2, _0x1ffaa7);');

const ctx = vm.createContext({ __dbg: null });
vm.runInContext(code, ctx, { filename: 'sign.js' });
const get_sign = vm.runInContext('get_sign', ctx);

const md5 = createHash('md5').update('test-input').digest('hex');
const out = get_sign(md5);
const dbg = vm.runInContext('__dbg', ctx);
console.log('md5   =', md5);
console.log('X-Bogus =', out);
console.log('header =', dbg.header, '(0x' + dbg.header.toString(16) + ')');
console.log('bytes  =', JSON.stringify(dbg.bytes));
console.log('bogusIndex =', dbg.bogusIndex, ' envcode =', dbg.envcode, ' ubcode =', dbg.ubcode);
console.log('initialized =', dbg.initialized, ' key =', dbg.key, ' initFlag =', dbg.initFlag);
