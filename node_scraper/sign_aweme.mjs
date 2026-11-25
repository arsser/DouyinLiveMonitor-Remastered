// www.douyin.com a_bogus (pageId 6241, intercept ^/aweme/v1/)
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { makeBrowserShim } from './bdms_shim.mjs';
import { chromePath, signWithChrome, startSignServer } from './chrome_cdp_sign.mjs';

const ORIGIN = 'https://www.douyin.com';
const DEFAULT_UA =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36';
const PATHS = [
  '^/webcast/',
  '^/aweme/v1/',
  '^/aweme/v2/',
  '/douplus/',
  '/v1/message/send',
  '^/live/',
  '^/captcha/',
  '^/ecom/',
  '^/luna/pc',
];

function bdmsPath() {
  const www = new URL('./bdms_www.js', import.meta.url);
  const live = new URL('./bdms.js', import.meta.url);
  if (existsSync(fileURLToPath(www))) return www;
  return live;
}

let _ready = null;
let _lock = Promise.resolve();

function parseArgs(argv) {
  const out = { query: '', method: 'GET', ua: DEFAULT_UA, path: '/aweme/v1/web/collects/list/', serve: 0, jsonFile: '' };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--query') out.query = argv[++i] || '';
    else if (a === '--json-file') out.jsonFile = argv[++i] || '';
    else if (a === '--method') out.method = (argv[++i] || 'GET').toUpperCase();
    else if (a === '--ua') out.ua = argv[++i] || DEFAULT_UA;
    else if (a === '--path') out.path = argv[++i] || out.path;
    else if (a === '--serve') out.serve = Number(argv[++i] || 5557) || 5557;
    else if (!a.startsWith('--') && !out.query) out.query = a;
  }
  if (out.jsonFile) {
    const body = JSON.parse(readFileSync(out.jsonFile, 'utf8'));
    out.query = String(body.query || out.query || '');
    if (body.method) out.method = String(body.method).toUpperCase();
    if (body.ua) out.ua = String(body.ua);
    if (body.path) out.path = String(body.path);
  }
  return out;
}

function buildSigner({ ua, path }) {
  const { window } = makeBrowserShim({
    cookie: '',
    origin: ORIGIN,
    userAgent: ua,
    hardwareConcurrency: 12,
    deviceMemory: 32,
  });
  const code = readFileSync(bdmsPath(), 'utf8');
  let lastUrl = '';
  window.fetch = async (input) => {
    lastUrl = typeof input === 'string' ? input : (input && input.url) || String(input);
    return new Response('{}', { status: 200, headers: { 'content-type': 'application/json' } });
  };
  const XHR = window.XMLHttpRequest;
  const origOpen = XHR.prototype.open;
  const origSend = XHR.prototype.send;
  XHR.prototype.open = function (method, url, ...rest) {
    this._url = url;
    return origOpen.call(this, method, url, ...rest);
  };
  XHR.prototype.send = function (body) {
    lastUrl = this._url || lastUrl;
    return origSend.call(this, body);
  };

  const ctx = vm.createContext(window);
  vm.runInContext(code, ctx, { filename: 'bdms.js', timeout: 60000 });
  const bdms = window.bdms;
  if (!bdms || typeof bdms.init !== 'function') {
    throw new Error('bdms SDK 加载失败');
  }
  try {
    bdms.init({ aid: 6383, pageId: 6241, paths: PATHS, boe: false, ddrt: 8.5, ic: 8.5 });
  } catch {
    // ignore non-critical init errors
  }

  return async function sign(queryString, method = 'GET') {
    lastUrl = '';
    const url = `${ORIGIN}${path}?${queryString}`;
    const xhr = new window.XMLHttpRequest();
    xhr.bdmsInvokeList = [
      { args: [method, url, true], func() {} },
      { args: ['Accept', 'application/json, text/plain, */*'], func() {} },
      { args: ['bd-ticket-guard-web-version', 2], func() {} },
      { args: ['bd-ticket-guard-version', 2], func() {} },
      { args: ['bd-ticket-guard-iteration-version', 1], func() {} },
    ];
    xhr.open(method, url, true);
    xhr.send(null);
    await new Promise((r) => setTimeout(r, 50));
    let signed = lastUrl || xhr._url || '';
    let m = String(signed).match(/[?&]a_bogus=([^&]+)/);
    if (!m) {
      await window.fetch(url, { method, headers: {} });
      await new Promise((r) => setTimeout(r, 40));
      signed = lastUrl;
      m = String(signed).match(/[?&]a_bogus=([^&]+)/);
    }
    if (!m) {
      throw new Error('未能生成 a_bogus lastUrl=' + signed);
    }
    return decodeURIComponent(m[1]);
  };
}

async function vmBogus(queryString, { method = 'GET', ua = DEFAULT_UA, path = '/aweme/v1/web/collects/list/' } = {}) {
  const key = `${ua}|${path}`;
  if (!_ready || _ready.key !== key) {
    _ready = { key, run: buildSigner({ ua, path }) };
  }
  const run = _ready.run;
  const p = _lock.then(() => run(queryString, method));
  _lock = p.then(() => {}, () => {});
  return p;
}

export async function a_bogus(queryString, { method = 'GET', ua = DEFAULT_UA, path = '/aweme/v1/web/collects/list/' } = {}) {
  const backend = (process.env.A_BOGUS_BACKEND || 'chrome').toLowerCase();
  if (backend !== 'vm' && chromePath()) {
    try {
      return await signWithChrome({ query: queryString, method, path, ua });
    } catch (e) {
      if (backend === 'chrome') throw e;
    }
  }
  return vmBogus(queryString, { method, ua, path });
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = parseArgs(process.argv.slice(2));
  if (args.serve) {
    startSignServer(args.serve);
  } else if (!args.query) {
    console.error('usage: node sign_aweme.mjs --query "<qs>" [--method GET] [--path /aweme/v1/web/collects/list/]');
    console.error('   or: node sign_aweme.mjs --json-file <file.json>');
    console.error('   or: node sign_aweme.mjs --serve 5557');
    process.exit(2);
  } else {
    const sig = await a_bogus(args.query, { method: args.method, ua: args.ua, path: args.path });
    process.stdout.write(sig);
  }
}
