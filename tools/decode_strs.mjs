// 解码 sign.js 中 w_0x25f3(0xNNN) 的字符串（_0x5612de 与 w_0x25f3 是同一个解码器）
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const code = readFileSync(fileURLToPath(new URL('../../sign.js', import.meta.url)), 'utf8');
const ctx = vm.createContext({});
vm.runInContext(code, ctx, { filename: 'sign.js' });

const idxs = [0x289, 0x380, 0x382, 0x326, 0x2b7, 0x1cf, 0x18e, 0x1df, 0x1fc, 0x333, 0x3ad, 0x315, 0x2c5, 0x2a5, 0x36e, 0x1e2, 0x259, 0x2a7, 0x207, 0x20c, 0x267, 0x344, 0x249, 0x25f, 0x238];
for (const i of idxs) {
  const s = vm.runInContext(`w_0x25f3(${i})`, ctx);
  console.log(`0x${i.toString(16)} = ${JSON.stringify(s)}`);
}
