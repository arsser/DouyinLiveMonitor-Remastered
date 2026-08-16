// 注入到 sign.js，把每个 VM 程序解析出的字节码/字符串表/密钥 dump 出来（按执行顺序）
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');

const needle = "return _0x3f9548['p'] = null,";
if (code.split(needle).length - 1 !== 1) {
  console.error('注入点数量异常');
  process.exit(1);
}
code = code.replace(
  needle,
  "globalThis.__all_vms.push({ bytecode: _0x26f45f.slice(), strings: _0x408609.slice(), key: _0x55d729, entry: _0x537efa }); return _0x3f9548['p'] = null,"
);

const ctx = vm.createContext({ __all_vms: [] });
vm.runInContext(code, ctx, { filename: 'sign.js' });

const get_sign = vm.runInContext('get_sign', ctx);
const md5 = createHash('md5').update('test-input').digest('hex');
const result = get_sign(md5);
console.log('X-Bogus =', result, '\n');

const all = vm.runInContext('__all_vms', ctx);
console.log('VM 程序数量 =', all.length);
all.forEach((p, i) => {
  console.log(`\n===== 程序 ${i}  entry=${p.entry}  key=${p.key}  strings数=${p.strings.length} ====`);
  console.log('bytecode:', JSON.stringify(p.bytecode));
  console.log('strings:', JSON.stringify(p.strings));
});
