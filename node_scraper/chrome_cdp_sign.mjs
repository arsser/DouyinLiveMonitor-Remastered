// Real-Chrome a_bogus: load www.douyin.com BDMS/webmssdk and read XHR.responseURL.
import { spawn } from 'node:child_process';
import { mkdirSync, existsSync, readFileSync, writeFileSync, unlinkSync } from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { getWebSocket } from './websocket.mjs';

export const DEFAULT_UA =
  'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36';
export const ORIGIN = 'https://www.douyin.com';
export const HOME_URL = `${ORIGIN}/user/self`;
const DEBUG_PORT = Number(process.env.A_BOGUS_CDP_PORT || 9229);
const USER_DATA = process.env.A_BOGUS_CHROME_PROFILE || path.join(os.tmpdir(), 'douyin-abogus-chrome');

let _child = null;
let _cdp = null;
let _sessionId = null;
let _ready = null;

export function chromePath() {
  if (process.env.CHROME_PATH && existsSync(process.env.CHROME_PATH)) return process.env.CHROME_PATH;
  const candidates = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
  ];
  return candidates.find((p) => existsSync(p)) || '';
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function fetchJson(url, timeoutMs = 3000) {
  const ac = new AbortController();
  const t = setTimeout(() => ac.abort(), timeoutMs);
  try {
    const resp = await fetch(url, { signal: ac.signal });
    if (!resp.ok) throw new Error(`HTTP ${resp.status} ${url}`);
    return await resp.json();
  } finally {
    clearTimeout(t);
  }
}

async function waitJson(url, timeoutMs = 20000) {
  const deadline = Date.now() + timeoutMs;
  let last = '';
  while (Date.now() < deadline) {
    try {
      return await fetchJson(url, 1500);
    } catch (e) {
      last = String(e && e.message ? e.message : e);
      await sleep(250);
    }
  }
  throw new Error(`CDP endpoint not ready: ${url} (${last})`);
}

function createCdp(wsUrl) {
  const WebSocket = getWebSocket();
  const ws = new WebSocket(wsUrl);
  let nextId = 0;
  const pending = new Map();
  const lastAweme = { cookieLen: 0, hasSid: false, headerKeys: [], url: '' };
  let cookieHeader = '';
  ws.addEventListener('message', (ev) => {
    let msg;
    try {
      msg = JSON.parse(String(ev.data));
    } catch {
      return;
    }
    if (msg.method === 'Fetch.requestPaused' && msg.params) {
      const p = msg.params;
      const headers = Object.entries(p.request.headers || {}).map(([name, value]) => ({ name, value }));
      if (cookieHeader) {
        const filtered = headers.filter((h) => h.name.toLowerCase() !== 'cookie');
        filtered.push({ name: 'Cookie', value: cookieHeader });
        lastAweme.cookieLen = cookieHeader.length;
        lastAweme.hasSid = /(?:^|;\s*)sessionid=/.test(cookieHeader);
        lastAweme.headerKeys = filtered.map((h) => h.name).slice(0, 30);
        lastAweme.url = String(p.request.url || '').slice(0, 220);
        const id = ++nextId;
        ws.send(JSON.stringify({
          id,
          method: 'Fetch.continueRequest',
          params: { requestId: p.requestId, headers: filtered },
          sessionId: msg.sessionId,
        }));
        return;
      }
      const id = ++nextId;
      ws.send(JSON.stringify({
        id,
        method: 'Fetch.continueRequest',
        params: { requestId: p.requestId },
        sessionId: msg.sessionId,
      }));
      return;
    }
    if (msg.method === 'Network.requestWillBeSent' && msg.params && msg.params.request) {
      const req = msg.params.request;
      const u = String(req.url || '');
      if (u.includes('/aweme/')) {
        const headers = req.headers || {};
        const cookie = headers.Cookie || headers.cookie || '';
        if (cookie) {
          lastAweme.cookieLen = cookie.length;
          lastAweme.hasSid = /(?:^|;\s*)sessionid=/.test(cookie);
        }
        lastAweme.headerKeys = Object.keys(headers).slice(0, 30);
        lastAweme.url = u.slice(0, 220);
      }
    }
    if (msg.id == null) return;
    const job = pending.get(msg.id);
    if (!job) return;
    pending.delete(msg.id);
    if (msg.error) job.reject(new Error(`${job.method}: ${JSON.stringify(msg.error)}`));
    else job.resolve(msg.result);
  });
  const opened = new Promise((resolve, reject) => {
    ws.addEventListener('open', resolve, { once: true });
    ws.addEventListener('error', (e) => reject(e), { once: true });
  });
  async function send(method, params = {}, sessionId) {
    await opened;
    const id = ++nextId;
    const payload = { id, method, params };
    if (sessionId) payload.sessionId = sessionId;
    ws.send(JSON.stringify(payload));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (pending.has(id)) {
          pending.delete(id);
          reject(new Error(`CDP timeout ${method}`));
        }
      }, 45000);
      pending.set(id, {
        method,
        resolve: (v) => {
          clearTimeout(timer);
          resolve(v);
        },
        reject: (e) => {
          clearTimeout(timer);
          reject(e);
        },
      });
    });
  }
  return { ws, send, opened, lastAweme, setCookieHeader: (v) => { cookieHeader = String(v || ''); } };
}

function patchJsonFile(file, mutator) {
  if (!existsSync(file)) return;
  try {
    const data = JSON.parse(readFileSync(file, 'utf8'));
    if (!data || typeof data !== 'object') return;
    mutator(data);
    writeFileSync(file, JSON.stringify(data));
  } catch {
    // profile may be mid-write; leave it
  }
}

function markProfileClean() {
  mkdirSync(path.join(USER_DATA, 'Default'), { recursive: true });
  patchJsonFile(path.join(USER_DATA, 'Default', 'Preferences'), (data) => {
    data.profile = data.profile && typeof data.profile === 'object' ? data.profile : {};
    data.profile.exit_type = 'Normal';
    data.profile.exited_cleanly = true;
  });
  patchJsonFile(path.join(USER_DATA, 'Local State'), (data) => {
    data.exited_cleanly = true;
  });
  for (const name of ['SingletonLock', 'SingletonCookie', 'SingletonSocket']) {
    const lock = path.join(USER_DATA, name);
    try {
      if (existsSync(lock)) unlinkSync(lock);
    } catch {
      // still held by a live Chrome; spawn will reuse or fail clearly
    }
  }
}

function spawnChrome(ua) {
  const exe = chromePath();
  if (!exe) throw new Error('Chrome/Edge not found; set CHROME_PATH');
  mkdirSync(USER_DATA, { recursive: true });
  markProfileClean();
  const args = [
    `--remote-debugging-port=${DEBUG_PORT}`,
    '--remote-debugging-address=127.0.0.1',
    `--user-data-dir=${USER_DATA}`,
    '--disable-gpu',
    '--no-first-run',
    '--no-default-browser-check',
    '--hide-crash-restore-bubble',
    '--disable-session-crashed-bubble',
    '--disable-extensions',
    '--disable-popup-blocking',
    '--disable-background-networking',
    '--disable-sync',
    '--disable-translate',
    '--disable-component-update',
    '--disable-blink-features=AutomationControlled',
    '--window-size=1920,1080',
    '--window-position=0,0',
    `--user-agent=${ua}`,
    '--lang=zh-CN',
    HOME_URL,
  ];
  const child = spawn(exe, args, {
    stdio: 'ignore',
    detached: true,
    windowsHide: false,
  });
  child.unref();
  child.on('exit', () => {
    if (_child === child) {
      _child = null;
      _cdp = null;
      _sessionId = null;
      _ready = null;
    }
  });
  return child;
}

const STEALTH = `
Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
`;

function landingUrl(raw) {
  const u = String(raw || '').trim();
  if (!u || u === ORIGIN || u === `${ORIGIN}/` || /\/(?:jingxuan|recommend)(?:[/?#]|$)/i.test(u)) {
    return HOME_URL;
  }
  return u;
}

function shouldRetarget(url) {
  const u = String(url || '');
  if (!u || !/douyin\.com/i.test(u)) return true;
  return landingUrl(u) === HOME_URL && !/\/user\/self(?:[/?#]|$)/i.test(u);
}

function isDeadCdpError(err) {
  const s = String(err && err.message ? err.message : err);
  return (
    s.includes('-32000') ||
    s.includes('"-3200') ||
    /cannot find context/i.test(s) ||
    /context was destroyed/i.test(s) ||
    /session with given id not found/i.test(s) ||
    /target closed/i.test(s) ||
    /inspected target navigated/i.test(s)
  );
}

function dropSession() {
  try {
    _cdp?.ws?.close?.();
  } catch {
    // ignore
  }
  _cdp = null;
  _sessionId = null;
  _ready = null;
}

async function waitBdms(cdp, sessionId) {
  await sleep(800);
  return cdp.send(
    'Runtime.evaluate',
    {
      expression: `new Promise((resolve, reject) => {
        const t0 = Date.now();
        const iv = setInterval(() => {
          if (window.bdms && typeof window.bdms.init === 'function') {
            clearInterval(iv);
            resolve('bdms');
          } else if (Date.now() - t0 > 28000) {
            clearInterval(iv);
            reject(new Error('bdms not ready href=' + location.href));
          }
        }, 200);
      })`,
      awaitPromise: true,
    },
    sessionId,
  );
}

async function pingSession(cdp, sessionId) {
  await cdp.send('Runtime.evaluate', { expression: '1+1', returnByValue: true }, sessionId);
}

async function readSessionCookies(cdp, sessionId) {
  try {
    await cdp.send('Network.enable', {}, sessionId);
  } catch {
    // domain may already be on
  }
  let cookies = [];
  try {
    const all = await cdp.send('Network.getAllCookies', {}, sessionId);
    cookies = (all.cookies || []).filter((c) => String(c.domain || '').includes('douyin'));
  } catch {
    try {
      const r = await cdp.send('Network.getCookies', { urls: ['https://www.douyin.com/'] }, sessionId);
      cookies = r.cookies || [];
    } catch {
      cookies = [];
    }
  }
  const names = new Set(cookies.map((c) => c.name));
  return {
    cookieCount: cookies.length,
    cookieNames: [...names].slice(0, 40),
    hasSid:
      names.has('sessionid') ||
      names.has('sessionid_ss') ||
      names.has('sid_guard') ||
      names.has('sid_tt') ||
      names.has('sid_ucp_v1'),
    hasUid: names.has('uid_tt') || names.has('uid_tt_ss'),
  };
}

function sessionLoggedIn(state) {
  if (state.hasSid || state.hasUid || state.loggedIn) return true;
  return String(state.title || '').includes('的抖音');
}

async function readLoginState(cdp, sessionId) {
  let page = {};
  try {
    const result = await cdp.send(
      'Runtime.evaluate',
      {
        expression: `JSON.stringify({ href: location.href, title: document.title })`,
        returnByValue: true,
      },
      sessionId,
    );
    page = JSON.parse(result.result && result.result.value ? result.result.value : '{}');
  } catch {
    page = {};
  }
  const cookies = await readSessionCookies(cdp, sessionId);
  const state = {
    href: page.href || '',
    title: page.title || '',
    cookieCount: cookies.cookieCount,
    cookieNames: cookies.cookieNames || [],
    hasSid: cookies.hasSid,
    hasUid: cookies.hasUid,
  };
  state.loggedIn = sessionLoggedIn(state);
  return state;
}

function assertLoggedIn(state) {
  if (sessionLoggedIn(state)) return;
  throw new Error('抖音未登录：请在专用 Chrome 窗口扫码，登录后再拉取收藏');
}

async function attachPage(cdp, ua) {
  const listed = await cdp.send('Target.getTargets').catch(() => ({ targetInfos: [] }));
  const pages = (listed.targetInfos || []).filter(
    (t) => t.type === 'page' && /douyin\.com/i.test(String(t.url || '')),
  );
  let targetId = pages[0] && pages[0].targetId;
  if (!targetId) {
    const created = await cdp.send('Target.createTarget', { url: 'about:blank' });
    targetId = created.targetId;
  }
  const attached = await cdp.send('Target.attachToTarget', { targetId, flatten: true });
  const sessionId = attached.sessionId;
  await cdp.send('Page.enable', {}, sessionId);
  await cdp.send('Runtime.enable', {}, sessionId);
  await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source: STEALTH }, sessionId);
  await cdp.send(
    'Emulation.setUserAgentOverride',
    {
      userAgent: ua,
      acceptLanguage: 'zh-CN,zh;q=0.9,en;q=0.8',
      platform: 'Win32',
      userAgentMetadata: {
        brands: [
          { brand: 'Chromium', version: '139' },
          { brand: 'Not=A?Brand', version: '24' },
          { brand: 'Google Chrome', version: '139' },
        ],
        fullVersion: '139.0.0.0',
        platform: 'Windows',
        platformVersion: '10.0.0',
        architecture: 'x86',
        model: '',
        mobile: false,
        bitness: '64',
        wow64: false,
      },
    },
    sessionId,
  );
  const currentUrl = (pages.find((t) => t.targetId === targetId) || {}).url || '';
  if (shouldRetarget(currentUrl)) {
    await cdp.send('Page.navigate', { url: HOME_URL }, sessionId);
  }
  await waitBdms(cdp, sessionId);
  return sessionId;
}

export async function ensureChrome(ua = DEFAULT_UA) {
  if (_cdp && _sessionId) {
    try {
      await pingSession(_cdp, _sessionId);
      return { cdp: _cdp, sessionId: _sessionId };
    } catch {
      dropSession();
    }
  }
  if (_ready) return _ready;
  _ready = (async () => {
    let version;
    try {
      version = await fetchJson(`http://127.0.0.1:${DEBUG_PORT}/json/version`, 800);
    } catch {
      _child = spawnChrome(ua);
      version = await waitJson(`http://127.0.0.1:${DEBUG_PORT}/json/version`, 25000);
    }
    const wsUrl = version.webSocketDebuggerUrl;
    if (!wsUrl) throw new Error('no webSocketDebuggerUrl');
    const cdp = createCdp(wsUrl);
    await cdp.opened;
    const sessionId = await attachPage(cdp, ua);
    _cdp = cdp;
    _sessionId = sessionId;
    return { cdp, sessionId };
  })();
  try {
    return await _ready;
  } catch (e) {
    _ready = null;
    throw e;
  }
}

async function attachExisting(cdp) {
  const listed = await cdp.send('Target.getTargets').catch(() => ({ targetInfos: [] }));
  const pages = (listed.targetInfos || []).filter((t) => t.type === 'page');
  const douyin = pages.filter((t) => /douyin\.com/i.test(String(t.url || '')));
  const target = douyin[0] || pages[0];
  if (!target) throw new Error('no page target');
  const attached = await cdp.send('Target.attachToTarget', { targetId: target.targetId, flatten: true });
  const sessionId = attached.sessionId;
  await cdp.send('Page.enable', {}, sessionId).catch(() => {});
  await cdp.send('Runtime.enable', {}, sessionId).catch(() => {});
  await cdp.send('Network.enable', {}, sessionId).catch(() => {});
  return sessionId;
}

export async function ensureChromeProbe() {
  if (_cdp && _sessionId) {
    try {
      await pingSession(_cdp, _sessionId);
      return { cdp: _cdp, sessionId: _sessionId };
    } catch {
      dropSession();
    }
  }
  const version = await fetchJson(`http://127.0.0.1:${DEBUG_PORT}/json/version`, 800);
  const wsUrl = version.webSocketDebuggerUrl;
  if (!wsUrl) throw new Error('no webSocketDebuggerUrl');
  const cdp = createCdp(wsUrl);
  await cdp.opened;
  const sessionId = await attachExisting(cdp);
  _cdp = cdp;
  _sessionId = sessionId;
  return { cdp, sessionId };
}

export async function signWithChrome({
  query,
  method = 'GET',
  path = '/aweme/v1/web/collects/list/',
  ua = DEFAULT_UA,
} = {}) {
  if (!query) throw new Error('empty query');
  const run = async () => {
    const { cdp, sessionId } = await ensureChrome(ua);
    const url = `${ORIGIN}${path}?${query}`;
    const result = await cdp.send(
      'Runtime.evaluate',
      {
        expression: `(async () => {
        const url = ${JSON.stringify(url)};
        const method = ${JSON.stringify(method)};
        const xhr = new XMLHttpRequest();
        xhr.open(method, url, true);
        await new Promise((resolve) => {
          xhr.onloadend = resolve;
          xhr.onerror = resolve;
          try { xhr.send(method === 'POST' ? 'count=10&cursor=0' : null); }
          catch { resolve(); }
        });
        const signed = xhr.responseURL || '';
        const m = String(signed).match(/[?&]a_bogus=([^&]+)/);
        if (!m) {
          throw new Error('no a_bogus status=' + xhr.status + ' url=' + signed.slice(0, 180));
        }
        return decodeURIComponent(m[1]);
      })()`,
        awaitPromise: true,
        returnByValue: true,
      },
      sessionId,
    );
    if (result.exceptionDetails) {
      const d = result.exceptionDetails.exception || result.exceptionDetails;
      throw new Error(d.description || d.value || JSON.stringify(d));
    }
    const sig = result.result && result.result.value;
    if (!sig) throw new Error('empty a_bogus from Chrome');
    return sig;
  };
  try {
    return await run();
  } catch (e) {
    if (!isDeadCdpError(e)) throw e;
    dropSession();
    return await run();
  }
}

function parseCookieHeader(cookie) {
  const out = [];
  for (const part of String(cookie || '').split(';')) {
    const s = part.trim();
    if (!s) continue;
    const eq = s.indexOf('=');
    if (eq <= 0) continue;
    out.push({ name: s.slice(0, eq), value: s.slice(eq + 1) });
  }
  return out;
}

async function applyCookies(cdp, sessionId, cookie) {
  await cdp.send('Network.enable', {}, sessionId);
  const items = parseCookieHeader(cookie);
  const expires = Math.floor(Date.now() / 1000) + 86400 * 30;
  let ok = 0;
  for (const { name, value } of items) {
    try {
      const r = await cdp.send(
        'Network.setCookie',
        {
          name,
          value,
          domain: '.douyin.com',
          path: '/',
          secure: true,
          sameSite: 'Lax',
          expires,
        },
        sessionId,
      );
      if (!r || r.success !== false) ok += 1;
    } catch {
      // skip
    }
  }
  let sid = false;
  let n = ok;
  try {
    const all = await cdp.send('Network.getAllCookies', {}, sessionId);
    const names = (all.cookies || [])
      .filter((c) => String(c.domain || '').includes('douyin'))
      .map((c) => c.name);
    n = names.length;
    sid = names.includes('sessionid') || names.includes('sid_guard') || names.includes('sid_tt');
  } catch {
    // keep ok count
  }
  return { cookieCount: n, hasSession: sid };
}

export async function fetchWithChrome({
  query,
  method = 'GET',
  path = '/aweme/v1/web/collects/list/',
  ua = DEFAULT_UA,
  cookie = '',
  body = '',
} = {}) {
  if (!query) throw new Error('empty query');
  const run = async () => {
  const { cdp, sessionId } = await ensureChrome(ua);
  assertLoggedIn(await readLoginState(cdp, sessionId));
  let cookieInfo = { cookieCount: 0, hasSession: false };
  if (cookie) {
    cookieInfo = await applyCookies(cdp, sessionId, cookie);
    cdp.setCookieHeader(cookie);
    try {
      await cdp.send(
        'Fetch.enable',
        { patterns: [{ urlPattern: '*://*.douyin.com/aweme/*', requestStage: 'Request' }, { urlPattern: '*://www.douyin.com/aweme/*', requestStage: 'Request' }] },
        sessionId,
      );
    } catch {
      // Fetch domain may be unavailable
    }
  } else {
    cdp.setCookieHeader('');
    try {
      await cdp.send('Fetch.disable', {}, sessionId);
    } catch {
      // ignore
    }
  }
  cdp.lastAweme.cookieLen = 0;
  cdp.lastAweme.hasSid = false;
  const url = `${ORIGIN}${path}?${query}`;
  const sendBody = method === 'POST' ? (body || 'count=10&cursor=0') : '';
  const result = await cdp.send(
    'Runtime.evaluate',
    {
      expression: `(async () => {
        const url = ${JSON.stringify(url)};
        const method = ${JSON.stringify(method)};
        const sendBody = ${JSON.stringify(sendBody)};
        const captured = [];
        const orig = XMLHttpRequest.prototype.setRequestHeader;
        XMLHttpRequest.prototype.setRequestHeader = function (k, v) {
          captured.push(String(k));
          return orig.apply(this, arguments);
        };
        const xhr = new XMLHttpRequest();
        xhr.withCredentials = true;
        xhr.open(method, url, true);
        if (method === 'POST') {
          xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        }
        await new Promise((resolve) => {
          xhr.onloadend = resolve;
          xhr.onerror = resolve;
          try { xhr.send(method === 'POST' ? sendBody : null); }
          catch { resolve(); }
        });
        XMLHttpRequest.prototype.setRequestHeader = orig;
        const signed = xhr.responseURL || '';
        const m = String(signed).match(/[?&]a_bogus=([^&]+)/);
        const text = String(xhr.responseText || '');
        let jsonStatus = null;
        let jsonMsg = '';
        let listKey = '';
        let listLen = 0;
        try {
          const data = JSON.parse(text);
          jsonStatus = data.status_code;
          jsonMsg = data.status_msg || '';
          for (const k of ['collects_list', 'aweme_list']) {
            if (Array.isArray(data[k])) { listKey = k; listLen = data[k].length; break; }
          }
        } catch {}
        return {
          httpStatus: xhr.status,
          jsonStatus,
          jsonMsg,
          listKey,
          listLen,
          aBogusLen: m ? decodeURIComponent(m[1]).length : 0,
          headerNames: captured.slice(0, 40),
          bodyHead: text.slice(0, 220),
          body: text,
        };
      })()`,
      awaitPromise: true,
      returnByValue: true,
    },
    sessionId,
  );
  if (result.exceptionDetails) {
    const d = result.exceptionDetails.exception || result.exceptionDetails;
    throw new Error(d.description || d.value || JSON.stringify(d));
  }
  const out = {
    ...(result.result && result.result.value),
    ...cookieInfo,
    sentCookieLen: cdp.lastAweme.cookieLen,
    sentHasSid: cdp.lastAweme.hasSid,
    sentHeaderKeys: cdp.lastAweme.headerKeys,
  };
  if (typeof out.body === 'string') {
    try {
      out.payload = JSON.parse(out.body);
    } catch {
      out.payload = null;
    }
    delete out.body;
  }
  return out;
  };
  try {
    return await run();
  } catch (e) {
    if (!isDeadCdpError(e)) throw e;
    dropSession();
    return await run();
  }
}

export function startSignServer(port = 5557) {
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url || '/', 'http://127.0.0.1');
    const ok = (code, body, type = 'text/plain; charset=utf-8') => {
      res.writeHead(code, { 'content-type': type, 'cache-control': 'no-store' });
      res.end(body);
    };
    if (req.method === 'GET' && url.pathname === '/health') {
      return ok(200, JSON.stringify({ ok: true, chrome: Boolean(_sessionId && _cdp) }), 'application/json');
    }
    if (req.method === 'GET' && url.pathname === '/page') {
      const spawn = url.searchParams.get('spawn') !== '0';
      const readPage = async (cdp, sessionId) => {
        const state = await readLoginState(cdp, sessionId);
        return JSON.stringify(state);
      };
      try {
        if (!spawn) {
          try {
            const { cdp, sessionId } = await ensureChromeProbe();
            const raw = await readPage(cdp, sessionId);
            const parsed = JSON.parse(raw);
            parsed.ok = true;
            parsed.chrome = true;
            return ok(200, JSON.stringify(parsed), 'application/json');
          } catch {
            return ok(200, JSON.stringify({ ok: false, chrome: false, href: '', title: '' }), 'application/json');
          }
        }
        const { cdp, sessionId } = await ensureChrome();
        const goto = landingUrl(url.searchParams.get('goto') || HOME_URL);
        if (url.searchParams.get('clear') === '1') {
          try {
            await cdp.send('Network.enable', {}, sessionId);
            await cdp.send('Network.clearBrowserCookies', {}, sessionId);
          } catch {
            // ignore
          }
        }
        if (goto.startsWith('https://www.douyin.com')) {
          await cdp.send('Page.navigate', { url: goto }, sessionId);
          await new Promise((r) => setTimeout(r, 2500));
        }
        const raw = await readPage(cdp, sessionId);
        const parsed = JSON.parse(raw);
        parsed.ok = true;
        parsed.chrome = true;
        return ok(200, JSON.stringify(parsed), 'application/json');
      } catch (e) {
        return ok(500, String(e && e.message ? e.message : e));
      }
    }
    if (req.method !== 'POST' || (url.pathname !== '/sign' && url.pathname !== '/fetch')) {
      return ok(404, 'not found');
    }
    const chunks = [];
    for await (const c of req) chunks.push(c);
    let body = {};
    try {
      body = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
    } catch {
      return ok(400, 'invalid json');
    }
    try {
      if (url.pathname === '/fetch') {
        const out = await fetchWithChrome({
          query: body.query || '',
          method: body.method || 'GET',
          path: body.path || '/aweme/v1/web/collects/list/',
          ua: body.ua || DEFAULT_UA,
          cookie: body.cookie || '',
          body: body.body || '',
        });
        return ok(200, JSON.stringify(out), 'application/json');
      }
      const sig = await signWithChrome({
        query: body.query || '',
        method: body.method || 'GET',
        path: body.path || '/aweme/v1/web/collects/list/',
        ua: body.ua || DEFAULT_UA,
      });
      return ok(200, sig);
    } catch (e) {
      process.stderr.write(String(e && e.stack ? e.stack : e) + '\n');
      return ok(500, String(e && e.message ? e.message : e));
    }
  });
  server.listen(port, '127.0.0.1', () => {
    process.stderr.write(`a_bogus chrome signer http://127.0.0.1:${port}/sign\n`);
  });
  return server;
}
