// a_bogus.mjs —— 抖音直播网页版 a_bogus 签名（Node 直接调用）
//
// 原理：live.douyin.com 的 a_bogus 由 BDMS 安全 SDK（bdms.js，1.0.1.19-fix.01）生成
//   （注意：不是 webmssdk.js 的 byted_acrawler/X-Bogus，也不是 secsdk/verify-center）。
// 本文件把 bdms.js 原样加载进 Node vm（配浏览器 shim），调用其 init() 挂上 fetch 拦截，
// 再用被拦截的 fetch 发起一个虚拟 /webcast/feed/ 请求，从被签名后的 URL 里取出 a_bogus。
// 该 a_bogus 与真实浏览器生成的等价（均被服务端接受）。
//
// 算法（已逆向确认）：
//   a_bogus = S1_base64( 约129字节二进制 )
//   S1 字母表（与 X-Bogus 同源）：Dkdpgh4ZKsQB80/Mfvw36XI1R25-WUAlEi7NLboqYTOPuzmFjJnryx9HVGcaStCe
//   核心 SM3 哈希链（盐 "dhzx"）：
//     h1  = SM3(queryString + "dhzx")
//     h1' = SM3(hex(h1))
//     h2  = SM3("dhzx")
//     h2' = SM3(hex(h2))
//     payload = 由 h1'/h2'/时间戳/随机数/环境 构造（约111字节，含随机）
//     h3  = SM3( base64(payload) )
//     输出 = 头部 + 加密(payload+h3)  → S1 base64
//   输入 = (queryString, headers/body, userAgent, pageId=7571, aid=6383, version="1.0.1.19-fix.01")
//
// 用法：
//   import { a_bogus, verify_feed } from './a_bogus.mjs';
//   const sig = await a_bogus('aid=6383&app_name=douyin_web&...');   // 返回 a_bogus 值（已 URL 编码的 base64）
//   const url = 'https://live.douyin.com/webcast/feed/?' + query + '&a_bogus=' + sig;
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { makeBrowserShim } from './bdms_shim.mjs';

const BDMS_PATH = new URL('./bdms.js', import.meta.url);
const FEED_PATH = '/webcast/feed/';
const FEED_ORIGIN = 'https://live.douyin.com';

let _ready = null;   // Promise<signer>
let _lock = Promise.resolve();   // 串行化 sign（内部共享 lastUrl，不能并发）

function buildSigner() {
  const { window } = makeBrowserShim({ cookie: '' });
  const code = readFileSync(BDMS_PATH, 'utf8');

  // 捕获 bdms 签名后的真实 URL
  let lastUrl = '';
  window.fetch = async (input) => {
    lastUrl = typeof input === 'string' ? input : (input && input.url) || String(input);
    return new Response('{}', { status: 200, headers: { 'content-type': 'application/json' } });
  };

  const ctx = vm.createContext(window);
  vm.runInContext(code, ctx, { filename: 'bdms.js', timeout: 60000 });

  const bdms = window.bdms;
  if (!bdms || typeof bdms.init !== 'function') {
    throw new Error('bdms SDK 加载失败');
  }
  // init：拦截 ^/webcast/ 路径。init 内部个别子路径（msToken/beacon 上报）在无浏览器环境会抛错，属非关键，忽略即可。
  try {
    bdms.init({ aid: 6383, pageId: 7571, paths: ['^/webcast/'], boe: false, ddrt: 8.5, ic: 8.5 });
  } catch (e) {
    // 忽略非关键初始化错误（不影响 a_bogus 生成）
  }

  return async function sign(queryString) {
    lastUrl = '';
    const url = `${FEED_ORIGIN}${FEED_PATH}?${queryString}`;
    await window.fetch(url, { method: 'GET', headers: {} });
    await new Promise((r) => setTimeout(r, 30));
    const m = lastUrl.match(/[?&]a_bogus=([^&]+)/);
    if (!m) {
      throw new Error('未能生成 a_bogus（URL 未命中拦截路径或 SDK 未初始化成功）');
    }
    return decodeURIComponent(m[1]);
  };
}

export function a_bogus(queryString) {
  if (!_ready) {
    _ready = buildSigner();
  }
  const run = _ready;
  const p = _lock.then(() => run(queryString));
  _lock = p.then(() => {}, () => {});   // 无论成败都推进锁
  return p;
}

// 端到端验证：用生成的 a_bogus + 匿名 ttwid 请求 /webcast/feed/，返回 room 列表
export async function verify_feed(queryString, { ua } = {}) {
  const UA = ua || 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36';
  const sig = await a_bogus(queryString);
  const ttwidResp = await fetch('https://live.douyin.com/', { headers: { 'user-agent': UA } });
  const ttwid = (ttwidResp.headers.get('set-cookie') || '').match(/ttwid=([^;]+)/)?.[1] || '';
  const url = `${FEED_ORIGIN}${FEED_PATH}?${queryString}&a_bogus=${encodeURIComponent(sig)}`;
  const resp = await fetch(url, { headers: { 'user-agent': UA, referer: 'https://live.douyin.com/', cookie: `ttwid=${ttwid}` } });
  const text = await resp.text();
  let json = null;
  try { json = JSON.parse(text); } catch {}
  return { status: resp.status, status_code: json?.status_code, ok: json?.status_code === 0, json, text, a_bogus: sig, ttwid };
}

// CLI：node a_bogus.mjs "<query string>"
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const q = process.argv[2] || 'aid=6383&app_name=douyin_web&live_id=1&device_platform=web&language=zh-CN&cookie_enabled=true&browser_platform=Win32&web_rid=123456789';
  const sig = await a_bogus(q);
  console.log('a_bogus =', sig);
}
