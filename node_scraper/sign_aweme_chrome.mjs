// Sign a_bogus with a real Chrome page (bdms + webmssdk + sdk-glue).
// Node vm of bdms.js alone yields ~172-char tokens that www.douyin.com rejects (403).
// A live www.douyin.com tab yields ~184-char tokens that the same API accepts.
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { getWebSocket } from './websocket.mjs';

const DEFAULT_UA =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36';
const ORIGIN = 'https://www.douyin.com';
const CDP_PORT = Number(process.env.BDMS_CDP_PORT || 9229);
const SIGN_PORT = Number(process.env.BDMS_SIGN_PORT || 5557);
const PROFILE_DIR =
  process.env.BDMS_CHROME_PROFILE ||
  path.join(path.dirname(fileURLToPath(import.meta.url)), '.chrome-sign');

function chromePath() {
  const env = process.env.CHROME_PATH || process.env.BDMS_CHROME_PATH;
  if (env && existsSync(env)) return env;
  const candidates = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
  ];
  return candidates.find((p) => existsSync(p)) || '';
}

class CdpClient {
  constructor(url) {
    this.url = url;
    this.ws = null;
    this.id = 0;
    this.pending = new Map();
    this.sessionHandlers = new Map();
  }

  async connect() {
    this.ws = new (getWebSocket())(this.url);
    await new Promise((resolve, reject) => {
      this.ws.addEventListener('open', resolve, { once: true });
      this.ws.addEventListener('error', reject, { once: true });
    });
    this.ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(String(ev.data));
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        if (msg.error) reject(new Error(msg.error.message || JSON.stringify(msg.error)));
        else resolve(msg.result);
        return;
      }
    });
  }

  send(method, params = {}, sessionId) {
    const id = ++this.id;
    const payload = { id, method, params };
    if (sessionId) payload.sessionId = sessionId;
    this.ws.send(JSON.stringify(payload));
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`CDP timeout ${method}`));
      }, 60000);
      this.pending.set(id, {
        resolve: (v) => {
          clearTimeout(t);
          resolve(v);
        },
        reject: (e) => {
          clearTimeout(t);
          reject(e);
        },
      });
    });
  }

  close() {
    try {
      this.ws?.close();
    } catch {}
  }
}

function waitPort(port, timeoutMs = 20000) {
  const start = Date.now();
  return new Promise((resolve, reject) => {
    const tryOnce = () => {
      fetch(`http://127.0.0.1:${port}/json/version`)
        .then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status)))))
        .then(resolve)
        .catch((err) => {
          if (Date.now() - start > timeoutMs) reject(err);
          else setTimeout(tryOnce, 200);
        });
    };
    tryOnce();
  });
}

let _chromeProc = null;
let _cdp = null;
let _sessionId = null;
let _ready = null;

async function ensureChrome({ ua = DEFAULT_UA, headless = true } = {}) {
  const exe = chromePath();
  if (!exe) throw new Error('Chrome not found; set CHROME_PATH');
  mkdirSync(PROFILE_DIR, { recursive: true });
  try {
    const ver = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`);
    if (ver.ok) {
      const info = await ver.json();
      return info;
    }
  } catch {}

  const args = [
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${PROFILE_DIR}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-features=Translate,OptimizationHints,MediaRouter',
    '--disable-popup-blocking',
    '--window-size=1920,1080',
    '--lang=zh-CN',
    `--user-agent=${ua}`,
    'about:blank',
  ];
  if (headless) args.unshift('--headless=new');
  _chromeProc = spawn(exe, args, {
    stdio: ['ignore', 'ignore', 'pipe'],
    windowsHide: true,
  });
  return waitPort(CDP_PORT, 25000);
}

async function attachPage(info, ua) {
  _cdp = new CdpClient(info.webSocketDebuggerUrl);
  await _cdp.connect();
  const created = await _cdp.send('Target.createTarget', { url: 'about:blank' });
  const attached = await _cdp.send('Target.attachToTarget', {
    targetId: created.targetId,
    flatten: true,
  });
  _sessionId = attached.sessionId;
  const send = (method, params) => _cdp.send(method, params, _sessionId);
  await send('Page.enable');
  await send('Runtime.enable');
  await send('Network.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', {
    source: `
      Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
      Object.defineProperty(navigator, 'language', { get: () => 'zh-CN' });
      Object.defineProperty(navigator, 'languages', { get: () => ['zh-CN', 'zh', 'en-US'] });
    `,
  });
  await send('Emulation.setUserAgentOverride', {
    userAgent: ua,
    acceptLanguage: 'zh-CN,zh;q=0.9,en-US;q=0.8',
    platform: 'Win32',
  });
  await send('Page.navigate', { url: `${ORIGIN}/` });
  await send('Page.loadEventFired').catch(() => {});
  const deadline = Date.now() + 25000;
  while (Date.now() < deadline) {
    const r = await send('Runtime.evaluate', {
      expression: `Boolean(window.bdms && window.byted_acrawler)`,
      returnByValue: true,
    });
    if (r.result && r.result.value) return send;
    await new Promise((res) => setTimeout(res, 400));
  }
  throw new Error('bdms/webmssdk did not load on www.douyin.com');
}

async function getSender({ ua = DEFAULT_UA } = {}) {
  if (_sessionId && _cdp) {
    return (method, params) => _cdp.send(method, params, _sessionId);
  }
  if (!_ready) {
    _ready = (async () => {
      const info = await ensureChrome({ ua, headless: process.env.BDMS_HEADLESS !== '0' });
      return attachPage(info, ua);
    })();
  }
  return _ready;
}

export async function signWithChrome(queryString, { method = 'GET', ua = DEFAULT_UA, path = '/aweme/v1/web/collects/list/' } = {}) {
  const send = await getSender({ ua });
  const url = `${ORIGIN}${path}?${queryString}`;
  const r = await send('Runtime.evaluate', {
    expression: `(() => {
      const url = ${JSON.stringify(url)};
      const method = ${JSON.stringify(method)};
      return new Promise((resolve) => {
        const xhr = new XMLHttpRequest();
        xhr.open(method, url, true);
        xhr.onloadend = () => {
          const u = xhr.responseURL || '';
          const m = String(u).match(/[?&]a_bogus=([^&]+)/);
          resolve({
            bogus: m ? decodeURIComponent(m[1]) : '',
            status: xhr.status,
            url: u.slice(0, 300),
            head: String(xhr.responseText || '').slice(0, 160),
          });
        };
        xhr.onerror = () => resolve({ bogus: '', status: 0, url: '', head: 'xhr error' });
        try { xhr.send(); } catch (e) { resolve({ bogus: '', status: 0, url: '', head: String(e) }); }
      });
    })()`,
    awaitPromise: true,
    returnByValue: true,
  });
  const val = r.result && r.result.value;
  if (!val || !val.bogus) {
    throw new Error('chrome signer failed: ' + JSON.stringify(val || r));
  }
  return val;
}

function parseArgs(argv) {
  const out = { query: '', method: 'GET', ua: DEFAULT_UA, path: '/aweme/v1/web/collects/list/', serve: false };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--query') out.query = argv[++i] || '';
    else if (a === '--method') out.method = (argv[++i] || 'GET').toUpperCase();
    else if (a === '--ua') out.ua = argv[++i] || DEFAULT_UA;
    else if (a === '--path') out.path = argv[++i] || out.path;
    else if (a === '--serve') out.serve = true;
    else if (!a.startsWith('--') && !out.query) out.query = a;
  }
  return out;
}

function startServer() {
  const server = http.createServer(async (req, res) => {
    if (req.method === 'GET' && req.url === '/health') {
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ ok: true, cdp: Boolean(_sessionId) }));
      return;
    }
    if (req.method !== 'POST' || req.url !== '/sign') {
      res.writeHead(404);
      res.end('not found');
      return;
    }
    const chunks = [];
    for await (const c of req) chunks.push(c);
    let body = {};
    try {
      body = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
    } catch {
      res.writeHead(400);
      res.end('bad json');
      return;
    }
    try {
      const val = await signWithChrome(body.query || '', {
        method: body.method || 'GET',
        ua: body.ua || DEFAULT_UA,
        path: body.path || '/aweme/v1/web/collects/list/',
      });
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(JSON.stringify(val));
    } catch (e) {
      res.writeHead(500, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ error: String(e && e.message ? e.message : e) }));
    }
  });
  server.listen(SIGN_PORT, '127.0.0.1', () => {
    process.stderr.write(`bdms chrome signer http://127.0.0.1:${SIGN_PORT}/sign\n`);
  });
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const args = parseArgs(process.argv.slice(2));
  if (args.serve) {
    startServer();
    signWithChrome('aid=6383&device_platform=webapp', { ua: args.ua }).catch((e) => {
      process.stderr.write('warmup failed: ' + e + '\n');
    });
  } else {
    if (!args.query) {
      process.stderr.write('usage: node sign_aweme_chrome.mjs --query "<qs>" [--serve]\n');
      process.exit(2);
    }
    const val = await signWithChrome(args.query, { method: args.method, ua: args.ua, path: args.path });
    process.stdout.write(val.bogus);
  }
}
