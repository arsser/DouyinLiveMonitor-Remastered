// 把 sign.js 中所有“纯十六进制算术表达式”折叠成十进制数字，产出可读的 sign_folded.js
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
const outPath = fileURLToPath(new URL('./sign_folded.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');

// 匹配由多个十六进制字面量 + 运算符组成的算术表达式（要求前后不是标识符字符，避免误伤 _0xNNN）
const re = /(?<![0-9A-Za-z_$])[-+]?0x[0-9a-fA-F]+(?:\s*[-+*\/]\s*[-+]?0x[0-9a-fA-F]+)+(?![0-9A-Za-z_$])/g;

let n = 0;
const folded = code.replace(re, (m) => {
  try {
    const v = eval(m);
    n++;
    return String(v);
  } catch {
    return m;
  }
});

console.log('折叠表达式数量 =', n);
writeFileSync(outPath, folded, 'utf8');
console.log('已写出:', outPath);
