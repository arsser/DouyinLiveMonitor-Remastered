// 反汇编 sign.js 的 VM 字节码程序，还原 X-Bogus 算法
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
let code = readFileSync(signPath, 'utf8');
const needle = "return _0x3f9548['p'] = null,";
code = code.replace(needle, "globalThis.__all_vms.push({ bytecode: _0x26f45f.slice(), strings: _0x408609.slice(), key: _0x55d729, entry: _0x537efa }); return _0x3f9548['p'] = null,");
const ctx = vm.createContext({ __all_vms: [] });
vm.runInContext(code, ctx, { filename: 'sign.js' });
const get_sign = vm.runInContext('get_sign', ctx);
get_sign(createHash('md5').update('test-input').digest('hex'));
const progs = vm.runInContext('__all_vms', ctx);

// opcode -> { name, size(operand bytes), kind }
const OPS = {};
const def = (n, name, size, kind) => { OPS[n] = { name, size, kind }; };
def(0, 'PUSH_TRUE', 0);
def(1, 'PUSH_FALSE', 0);
def(2, 'PUSH_NULL', 0);
def(3, 'PUSH_I8', 1, 'i8');
def(4, 'PUSH_I16', 2, 'i16');
def(5, 'PUSH_I32', 4, 'i32');
def(6, 'PUSH_NUMSTR', 2, 'str');
def(7, 'PUSH_STR', 2, 'str');
def(8, 'PUSH_UNDEF', 0);
def(9, 'PUSH_UNDEF', 0);
def(10, 'PUSH_UNDEF', 0);
def(11, 'PUSH_THIS', 0);
def(12, 'ARR', 2, 'u16');
def(13, 'OBJ', 0);
def(14, 'SET_PROP', 2, 'str');
def(15, 'SET_PROP', 2, 'str');
def(16, 'SET_PROP', 2, 'str');
def(17, 'GET_SCOPE', 2, 'sc');
def(18, 'GET_PROP', 2, 'str');
def(19, 'GET_PROP_DYN', 0);
def(20, 'SET_SCOPE', 2, 'sc');
def(21, 'SET_PROP2', 2, 'str');
def(22, 'SET_PROP_DYN', 0);
def(23, 'PUSH_SCOPE_IDX', 2, 'sc');
def(24, 'ADD', 0);
def(25, 'SUB', 0);
def(26, 'MUL', 0);
def(27, 'DIV', 0);
def(28, 'MOD', 0);
def(29, 'NEG', 0);
def(30, 'POSTINC', 0);
def(31, 'POSTINC', 0);
def(32, 'POSTINC', 0);
def(33, 'POSTINC', 0);
def(34, 'POSTINC', 0);
def(35, 'EQ', 0);
def(36, 'NEQ', 0);
def(37, 'SEQ', 0);
def(38, 'SNEQ', 0);
def(39, 'LT', 0);
def(40, 'LT', 0);
def(41, 'GT', 0);
def(42, 'GTE', 0);
def(43, 'SHL', 0);
def(44, 'SHR', 0);
def(45, 'USHR', 0);
def(46, 'AND', 0);
def(47, 'OR', 0);
def(48, 'XOR', 0);
def(49, 'XOR', 0);
def(50, 'NOT', 0);
def(51, 'JMP_FALSE', 2, 'jmp');
def(52, 'JMP_TRUE', 2, 'jmp');
def(53, 'SET_PROP3', 0);
def(54, 'IN', 0);
def(55, 'INSTANCEOF', 0);
def(57, 'DELETE', 0);
def(58, 'TYPEOF', 0);
def(59, 'FUNC', 1, 'u8');
def(60, 'FUNC2', 1, 'u8');
def(61, 'TRY_END', 2, 'jmp');
def(62, 'TRY_BEGIN', 2, 'jmp');
def(63, 'TRY_BEGIN', 2, 'jmp');
def(64, 'THROW', 0);
def(65, 'TRY_POP', 0);
def(66, 'RETURN', 0);
def(67, 'CALL', 1, 'u8');
def(68, 'CONSTRUCT', 1, 'u8');
def(69, 'JMP', 2, 'jmp');
def(71, 'JMP_FALSE_POP', 2, 'jmp');
def(72, 'JMP_SEQ_POP', 2, 'jmp');
def(73, 'POP', 0);
def(74, 'DUP', 0);

function disasm(p, idx) {
  const b = p.bytecode, s = p.strings;
  console.log(`\n===== 程序 ${idx}  (bytecode长度=${b.length}) =====`);
  console.log(`strings: ${JSON.stringify(s)}\n`);
  let pc = 0;
  while (pc < b.length) {
    const start = pc;
    const op = b[pc++];
    const info = OPS[op];
    if (!info) { console.log(`${start}: <未知操作码 ${op}>`); break; }
    let extra = '';
    const operands = [];
    if (info.kind === 'str') {
      const v = (b[pc] << 8) + b[pc + 1]; pc += 2;
      operands.push(v);
      extra = `"${s[v] ?? '?'}"`;
    } else if (info.kind === 'i8') {
      const v = b[pc++]; operands.push(v);
      const sv = (v << 24) >> 24;
      extra = `${v} (${sv})`;
    } else if (info.kind === 'i16') {
      const v = ((b[pc] << 8) + b[pc + 1]); pc += 2; operands.push(v);
      const sv = (v << 16) >> 16;
      extra = `${v} (${sv})`;
    } else if (info.kind === 'i32') {
      const v = ((b[pc] << 24) + (b[pc + 1] << 16) + (b[pc + 2] << 8) + b[pc + 3]) >>> 0; pc += 4; operands.push(v);
      extra = `${v}`;
    } else if (info.kind === 'u8') {
      const v = b[pc++]; operands.push(v); extra = `${v}`;
    } else if (info.kind === 'u16') {
      const v = (b[pc] << 8) + b[pc + 1]; pc += 2; operands.push(v); extra = `${v}`;
    } else if (info.kind === 'sc') {
      const d = b[pc++], i = b[pc++]; operands.push(d, i);
      extra = `depth=${d} idx=${i}`;
    } else if (info.kind === 'jmp') {
      const raw = (b[pc] << 8) + b[pc + 1]; pc += 2;
      const off = (raw << 16) >> 16;
      operands.push(raw);
      extra = `→ ${pc + off}`;
    }
    console.log(`${String(start).padStart(4)}: ${info.name.padEnd(14)} ${extra}`);
  }
}

progs.forEach((p, i) => disasm(p, i));
