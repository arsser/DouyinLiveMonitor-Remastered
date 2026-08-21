import { createRequire } from 'node:module';

export function getWebSocket() {
  if (typeof globalThis.WebSocket === 'function') return globalThis.WebSocket;
  const require = createRequire(import.meta.url);
  try {
    const mod = require('ws');
    const WS = mod.WebSocket || mod;
    if (typeof WS === 'function') return WS;
  } catch {
    // Node < 22 has no global WebSocket; fall through.
  }
  throw new Error(
    `WebSocket is not defined (Node ${process.versions.node}). 请在 DouyinLiveMonitor-Remastered/node_scraper 执行 npm install ws，然后重启 5557`,
  );
}
