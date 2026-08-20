// 通用浏览器 shim：供在 Node vm 中运行 bdms.js 使用
// 返回 { window, ctx }，window 即 vm 的 globalThis

const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36';

function stubFn() { return undefined; }

export function makeBrowserShim({
  cookie = '',
  origin = 'https://live.douyin.com',
  userAgent = UA,
  hardwareConcurrency = 12,
  deviceMemory = 32,
} = {}) {
  const parsed = new URL(origin);
  const host = parsed.hostname;
  // ---- canvas 2d context stub（指纹相关，返回固定字符串）----
  function makeCtx2d() {
    const state = { fillStyle: '#000', font: '10px sans-serif', textBaseline: 'alphabetic', globalCompositeOperation: 'source-over' };
    return new Proxy({}, {
      get(t, k) {
        if (k in state) return state[k];
        if (typeof k === 'string') {
          switch (k) {
            case 'fillRect': case 'fillText': case 'beginPath': case 'arc': case 'closePath': case 'fill':
            case 'moveTo': case 'lineTo': case 'stroke': case 'rect': case 'clearRect': case 'save': case 'restore':
            case 'translate': case 'scale': case 'rotate': case 'setTransform': case 'transform': case 'clip':
            case 'putImageData': case 'drawImage': case 'strokeText': case 'quadraticCurveTo': case 'bezierCurveTo':
              return () => {};
            case 'measureText': return () => ({ width: 42 });
            case 'getImageData': return () => ({ data: new Uint8ClampedArray(4), width: 1, height: 1 });
            case 'createLinearGradient': case 'createRadialGradient': return () => ({ addColorStop() {} });
            case 'isPointInPath': return () => false;
            default: return undefined;
          }
        }
        return undefined;
      },
      set(t, k, v) { state[k] = v; return true; },
    });
  }
  function makeCanvas() {
    return {
      width: 300, height: 150, style: {},
      getContext(type) { return type === '2d' ? makeCtx2d() : null; },
      toDataURL() { return 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=='; },
      setAttribute() {}, appendChild() {},
    };
  }

  // ---- storage ----
  function makeStorage() {
    const m = {};
    return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); }, removeItem: (k) => { delete m[k]; }, key: (i) => Object.keys(m)[i] ?? null, clear: () => { for (const k in m) delete m[k]; }, get length() { return Object.keys(m).length; } };
  }

  // ---- XMLHttpRequest ----
  function makeXMLHttpRequestClass() {
    function XHR() {
      this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = null;
      this.responseType = ''; this.timeout = 0; this.withCredentials = false; this.onreadystatechange = null;
      this.onload = null; this.onerror = null; this.onloadend = null; this.upload = {};
      this._headers = {}; this._method = 'GET'; this._url = '';
    }
    XHR.UNSENT = 0; XHR.OPENED = 1; XHR.HEADERS_RECEIVED = 2; XHR.LOADING = 3; XHR.DONE = 4;
    XHR.prototype.open = function (method, url, async = true) { this._method = method; this._url = url; this.readyState = 1; if (this.onreadystatechange) this.onreadystatechange(); };
    XHR.prototype.setRequestHeader = function (k, v) { this._headers[k] = v; };
    XHR.prototype.getResponseHeader = function (k) { return this._respHeaders ? this._respHeaders[k.toLowerCase()] ?? null : null; };
    XHR.prototype.getAllResponseHeaders = function () { return ''; };
    XHR.prototype.send = function (body) { this._body = body; this.readyState = 4; this.status = 200; this.responseText = ''; this.response = ''; if (this.onreadystatechange) this.onreadystatechange(); if (this.onload) this.onload(); if (this.onloadend) this.onloadend(); };
    XHR.prototype.addEventListener = function () {};
    XHR.prototype.removeEventListener = function () {};
    XHR.prototype.abort = function () {};
    return XHR;
  }

  // ---- 构建 window（即 vm global）----
  const window = {};

  const navigator = {
    userAgent, webdriver: false, platform: 'Win32', language: 'zh-CN', languages: ['zh-CN'],
    hardwareConcurrency, deviceMemory, maxTouchPoints: 0, cookieEnabled: true,
    appVersion: userAgent.slice(8), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', productSub: '20030107',
    vendor: 'Google Inc.', vendorSub: '', onLine: true, doNotTrack: null, pdfViewerEnabled: true,
    plugins: { length: 0, refresh() {} }, mimeTypes: { length: 0 },
    sendBeacon: () => false,
    geolocation: { getCurrentPosition() {}, watchPosition() {} },
    connection: { effectiveType: '4g', rtt: 50, downlink: 10, saveData: false },
    mediaDevices: { enumerateDevices: () => Promise.resolve([]), getUserMedia: () => Promise.reject(new Error('stub')) },
    requestMediaKeySystemAccess: () => Promise.reject(new Error('stub')),
    userAgentData: { brands: [{ brand: 'Chromium', version: '126' }], mobile: false, platform: 'Windows', getHighEntropyValues: () => Promise.resolve({}) },
  };

  const location = {
    href: `${origin}/`, protocol: 'https:', host, hostname: host,
    port: '', pathname: '/', search: '', hash: '', origin,
    assign() {}, replace() {}, reload() {}, ancestorOrigins: { length: 0 },
  };

  const document = {
    cookie, referrer: `${origin}/`, title: '', domain: host,
    location, readyState: 'complete', characterSet: 'UTF-8', charset: 'UTF-8', compatMode: 'CSS1Compat',
    documentMode: undefined, hidden: false, visibilityState: 'visible', defaultView: window,
    currentScript: null,
    createElement: (tag) => (tag === 'canvas' ? makeCanvas() : { style: {}, getContext: () => null, setAttribute() {}, appendChild() {}, src: '', href: '' }),
    createTextNode: () => ({}),
    getElementById: () => null,
    getElementsByTagName: () => [],
    getElementsByName: () => [],
    getElementsByClassName: () => [],
    querySelector: () => null,
    querySelectorAll: () => [],
    addEventListener() {}, removeEventListener() {}, dispatchEvent: () => false,
    createEvent: () => ({ initEvent() {} }),
    documentElement: { style: {}, getAttribute: () => null, tagName: 'HTML' },
    head: { appendChild() {}, getElementsByTagName: () => [] },
    body: { appendChild() {}, style: {}, tagName: 'BODY' },
    all: { length: 0 }, images: { length: 0 }, layers: [],
    write() {}, writeln() {}, execCommand: () => false,
    hasFocus: () => true,
  };
  document.location = location;

  const screen = {
    width: 1920, height: 1080, availWidth: 1920, availHeight: 1040, colorDepth: 24, pixelDepth: 24,
    screenX: 0, screenY: 0, availLeft: 0, availTop: 0,
    orientation: { type: 'landscape-primary', angle: 0 },
  };

  const performance = {
    now: () => Date.now() - 1000, timing: { navigationStart: Date.now() - 1000, domContentLoadedEventEnd: Date.now() - 500, loadEventEnd: Date.now() - 100 },
    navigation: { type: 0, redirectCount: 0 },
    getEntriesByType: () => [], mark() {}, measure() {}, timeOrigin: Date.now() - 1000,
  };

  const crypto = {
    getRandomValues: (arr) => { for (let i = 0; i < arr.length; i++) arr[i] = Math.floor(Math.random() * 256); return arr; },
    randomUUID: () => 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16); }),
    subtle: undefined,
  };

  const localStorage = makeStorage();
  const sessionStorage = makeStorage();

  const raf = (cb) => setTimeout(() => cb(Date.now()), 16);

  // 各种 DOM/Web API 构造器桩（防 ReferenceError）
  function stubCtor() { return {}; }
  const Image = function () { return { style: {}, src: '', onload: null, onerror: null, width: 0, height: 0, setAttribute() {} }; };
  const Audio = function () { return { play() {}, pause() {} }; };
  const Worker = stubCtor;
  const WebSocket = stubCtor;
  const Blob = function (parts, opts) { return { size: 0, type: opts && opts.type || '' }; };
  const FileReader = function () { this.readAsDataURL = () => {}; this.readAsText = () => {}; };
  const FormData = function () { this.append = () => {}; };
  const MutationObserver = function (cb) { this.observe = () => {}; this.disconnect = () => {}; };
  const IntersectionObserver = function (cb) { this.observe = () => {}; this.disconnect = () => {}; };
  const ResizeObserver = function (cb) { this.observe = () => {}; this.disconnect = () => {}; };
  const OfflineAudioContext = function () { return { createOscillator: () => ({}), createDynamicsCompressor: () => ({}), destination: {}, sampleRate: 44100, startRendering: () => Promise.resolve({ getChannelData: () => new Float32Array(1) }), resume: () => Promise.resolve() }; };
  const matchMedia = () => ({ matches: false, media: '', addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} });
  const indexedDB = { open: () => ({ onupgradeneeded: null, onsuccess: null, onerror: null, result: { createObjectStore: () => ({ put() {} }) } }) };
  const Notification = function () {};
  Notification.requestPermission = () => Promise.resolve('denied');
  const ImageData = function () { return { data: new Uint8ClampedArray(4), width: 1, height: 1 }; };
  const CustomEvent = function (type, init) { return { type, detail: init && init.detail }; };
  const Event = function (type) { return { type }; };
  const MessageChannel = function () { return { port1: { postMessage() {}, onmessage: null }, port2: { postMessage() {}, onmessage: null } }; };
  const BroadcastChannel = function (name) { this.name = name; this.postMessage = () => {}; };
  const getComputedStyle = () => ({});
  const requestIdleCallback = (cb) => setTimeout(() => cb({ didTimeout: false, timeRemaining: () => 50 }), 50);
  const cancelIdleCallback = clearTimeout;

  // 浏览器全局，全部挂到 window 上（window === globalThis）
  Object.assign(window, {
    window, self: null, globalThis: null, top: null, parent: null, frames: null,
    navigator, document, location, screen, performance, crypto,
    localStorage, sessionStorage,
    URL, URLSearchParams,
    atob: (s) => Buffer.from(s, 'base64').toString('latin1'),
    btoa: (s) => Buffer.from(s, 'latin1').toString('base64'),
    TextEncoder, TextDecoder,
    AbortController, AbortSignal,
    Request: (globalThis.Request) || stubFn,
    Response: (globalThis.Response) || stubFn,
    Headers: (globalThis.Headers) || stubFn,
    fetch: undefined, // 由外部注入真实 fetch
    XMLHttpRequest: makeXMLHttpRequestClass(),
    EventSource: stubFn,
    requestAnimationFrame: raf, cancelAnimationFrame: clearTimeout, webkitRequestAnimationFrame: raf,
    setTimeout, clearTimeout, setInterval, clearInterval, queueMicrotask,
    history: { length: 1, state: null, pushState() {}, replaceState() {} },
    // DOM/Web API 构造器桩
    Image, Audio, Worker, WebSocket, Blob, FileReader, FormData,
    MutationObserver, IntersectionObserver, ResizeObserver, OfflineAudioContext, webkitOfflineAudioContext: OfflineAudioContext,
    matchMedia, indexedDB, Notification, ImageData, CustomEvent, Event, MessageChannel, BroadcastChannel,
    getComputedStyle, requestIdleCallback, cancelIdleCallback,
    HTMLImageElement: Image, HTMLCanvasElement: function () { return makeCanvas(); },
    // 标准内置对象
    Object, Array, String, Number, Boolean, Math, Date, JSON, RegExp, Error, TypeError, RangeError, ReferenceError, SyntaxError, EvalError, URIError,
    Promise, Function, Symbol, Map, Set, WeakMap, WeakSet, Proxy, Reflect,
    parseInt, parseFloat, isNaN, isFinite, encodeURIComponent, decodeURIComponent, encodeURI, decodeURI, escape, unescape,
    Uint8Array, Uint8ClampedArray, Uint16Array, Uint32Array, Int8Array, Int16Array, Int32Array, Float32Array, Float64Array,
    ArrayBuffer, SharedArrayBuffer, DataView, BigInt, BigInt64Array, BigUint64Array,
    console,
  });
  window.window = window; window.self = window; window.globalThis = window; window.top = window; window.parent = window;
  window.document.defaultView = window;
  return { window, navigator, document, location, localStorage, sessionStorage, crypto };
}
