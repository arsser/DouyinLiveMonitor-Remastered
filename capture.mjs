// capture.mjs —— 抖音直播主播采集统一入口（含 IPTV 导出）
//
// 用法：node capture.mjs [ttwid|cookie] [iptv|oiptv] [N|max]
//
// 第一项（认证/来源）：
//   ttwid   匿名（a_bogus + ttwid）→ 抓 8 个分类
//   cookie  登录 cookie → 抓 关注 + 「全部」feed
//   省略    默认：cookie 抓所有关注（此时忽略第三项）
//
// 第二项（输出）：
//   iptv    写 config + iptv.m3u
//   oiptv   只写 iptv.m3u（不改 config）
//   省略    默认：只写 config（不写 iptv）
//
// 第三项（数量）：
//   N       数量（省略默认 100）
//   max     最大捕获（仅第一项为 ttwid 时生效）
//
// 示例：
//   node capture.mjs                     cookie 抓所有关注 → 只写 config
//   node capture.mjs iptv                抓所有关注 → config + iptv（第一项省略，参数前移）
//   node capture.mjs cookie iptv 500     关注+全部（合计 500）→ config + iptv
//   node capture.mjs ttwid iptv max      分类全量 → config + iptv
//   node capture.mjs ttwid oiptv 3000    分类前 3000 → 只写 iptv
import { a_bogus } from './node_scraper/a_bogus.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const CONFIG = path.resolve(__dirname, 'build', 'config.json');
const M3U = path.resolve(__dirname, 'build', 'iptv.m3u');
const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36';
const CATS = [101, 102, 103, 104, 105, 106, 107, 108]; // 聊天 音乐 游戏 二次元 舞蹈 文化 生活 运动
const PAGE = 20;   // 服务端每页固定 20
const t0 = Date.now();

// ── 命令行解析（位置参数，允许省略前导项）──
const raw = process.argv.slice(2).map(s => (s || '').trim().toLowerCase());
let AUTH, OUT, CNT;
if (raw[0] === 'ttwid' || raw[0] === 'cookie') {
  AUTH = raw[0]; OUT = raw[1] || ''; CNT = raw[2] || '';
} else {
  AUTH = ''; OUT = raw[0] || ''; CNT = raw[1] || '';   // 第一项省略：后续参数前移
}

let source, n;   // source: follow | feed | category
if (AUTH === 'ttwid')      { source = 'category'; n = (CNT === 'max') ? Infinity : (parseInt(CNT, 10) || 100); }
else if (AUTH === 'cookie') { source = 'feed';     n = parseInt(CNT, 10) || 100; } // max 对 cookie 无效 → 100
else                        { source = 'follow';   n = Infinity; }                  // 默认：抓所有关注，忽略第三项

const outMode = OUT === 'iptv' ? 'both' : (OUT === 'oiptv' ? 'iptv' : 'config');

// ── 公共工具 ──
function baseParams() {
  const p = new URLSearchParams();
  p.set('aid', '6383'); p.set('app_name', 'douyin_web'); p.set('live_id', '1');
  p.set('device_platform', 'web'); p.set('language', 'zh-CN'); p.set('enter_from', 'link_share');
  p.set('cookie_enabled', 'true'); p.set('screen_width', '1920'); p.set('screen_height', '1080');
  p.set('browser_language', 'zh-CN'); p.set('browser_platform', 'Win32');
  p.set('browser_name', 'Chrome'); p.set('browser_version', '126.0.0.0');
  p.set('os_name', 'Windows'); p.set('os_version', '10');
  return p;
}

function bestStream(su) {
  if (!su) return null;
  try {
    const sd = su.live_core_sdk_data?.pull_data?.stream_data;
    if (sd) {
      const o = JSON.parse(sd)?.data?.origin?.main?.flv;
      if (o) return o;
    }
  } catch {}
  const flv = su.flv_pull_url || {};
  return flv.FULL_HD1 || flv.HD1 || flv.SD2 || flv.SD1 || null;
}

// feed / follow_top 条目 → 统一房间结构
function pickFeed(list) {
  return (list || []).map(it => {
    const d = it.data || {};
    return {
      rid: String(it.web_rid || ''),
      nickname: d.owner?.nickname || '',
      user_count: d.user_count || 0,
      url: bestStream(d.stream_url),
    };
  }).filter(x => x.rid && x.nickname);
}

// 分类接口条目 → 统一房间结构
function pickCat(list) {
  return (list || []).map(e => ({
    rid: String(e.web_rid || ''),
    nickname: e.room?.owner?.nickname || '',
    user_count: e.room?.user_count || 0,
    url: bestStream(e.room?.stream_url),
  })).filter(x => x.rid && x.nickname);
}

function addAll(map, rooms) {
  let added = 0;
  for (const r of rooms) if (!map.has(r.rid)) { map.set(r.rid, r); added++; }
  return added;
}

async function callJson(apiPath, params, cookie) {
  const r = await fetch(`https://live.douyin.com${apiPath}?${params.toString()}`, {
    headers: { 'user-agent': UA, referer: 'https://live.douyin.com/', cookie },
  });
  return await r.json().catch(() => null);
}

async function getTtwid() {
  const r = await fetch('https://live.douyin.com/', { headers: { 'user-agent': UA } });
  return (r.headers.get('set-cookie') || '').match(/ttwid=([^;]+)/)?.[1] || '';
}

// ── 采集器：关注（cookie，翻页到底）──
async function collectFollow(map, cookie) {
  const fp = baseParams();
  fp.set('enter_source', 'homepage_pc_followtop');
  fp.set('need_pinned_info', '0');
  fp.set('source_key', 'web_homepage_follow_top');
  fp.set('webcast_version_code', '170400');
  fp.set('version_code', '170400');
  fp.set('need_map', '1');
  let sid = '0', mtime = '0', cnt = 0;
  for (let page = 1; page <= 200; page++) {
    fp.set('follow_session_id', sid);
    fp.set('maxtime', mtime);
    const j = await callJson('/webcast/feed/follow_top/', fp, cookie);
    const items = pickFeed(j?.data);
    addAll(map, items);
    cnt += items.length;
    const ex = j?.extra || {};
    if (!ex.has_more) { console.log(`  关注：${cnt} 个（${page} 页，已到底）`); break; }
    sid = String(ex.follow_session_id ?? sid);
    mtime = String(ex.max_time ?? mtime);
  }
  return cnt;
}

// ── 采集器：「全部」feed（cookie，maxtime 翻页）──
async function collectFeed(map, cookie, need) {
  if (need <= 0) return 0;
  const rp = baseParams();
  rp.set('channel', 'channel_pc_web');
  rp.set('request_tag_from', 'web');
  rp.set('need_map', '1');
  rp.set('liveid', '1');
  rp.set('is_draw', '1');
  rp.set('inner_from_drawer', '0');
  rp.set('custom_count', '50');
  rp.set('action', 'load_more');
  rp.set('action_type', 'loadmore');
  rp.set('enter_source', 'web_homepage_hot_web_live_card');
  rp.set('source_key', 'web_homepage_hot_web_live_card');
  rp.set('is_ssr', 'true');
  let mt = '0', added = 0, emptyRun = 0;
  for (let page = 1; page <= 1000 && added < need; page++) {
    rp.set('maxtime', mt);
    const j = await callJson('/webcast/feed/', rp, cookie);
    const items = pickFeed(j?.data);
    const a = addAll(map, items);
    added += a;
    const ex = j?.extra || {};
    if (page % 5 === 0 || added >= need || !ex.has_more) {
      console.log(`  「全部」第${page}页 累计 ${added}/${need}`);
    }
    if (added >= need) break;
    emptyRun = a === 0 ? emptyRun + 1 : 0;
    if (emptyRun >= 3) break;
    if (!ex.has_more) break;
    mt = String(ex.max_time ?? mt);
  }
  return added;
}

// ── 采集器：分类（匿名 a_bogus + ttwid）──
function buildCatQuery(partition, offset) {
  const p = baseParams();
  p.set('count', String(PAGE));
  p.set('offset', String(offset));
  p.set('partition', String(partition));
  p.set('partition_type', '4');
  p.set('req_from', '2');
  return p.toString();
}

async function fetchPartition(partition, offset, ttwid) {
  const query = buildCatQuery(partition, offset);
  const sig = await a_bogus(query);
  const url = `https://live.douyin.com/webcast/web/partition/detail/room/v2/?${query}&a_bogus=${encodeURIComponent(sig)}`;
  const r = await fetch(url, { headers: { 'user-agent': UA, referer: 'https://live.douyin.com/', cookie: `ttwid=${ttwid}` } });
  return await r.json().catch(() => null);
}

async function processCategory(partition, ttwid) {
  const out = [];
  let offset = 0;
  for (let g = 0; g < 200; g++) {
    const j = await fetchPartition(partition, offset, ttwid);
    if (!j || j.status_code !== 0) break;
    const list = j.data?.data || [];
    if (!list.length) break;
    out.push(...pickCat(list));
    const next = j.data?.offset;
    if (!next || next <= offset) break;
    offset = next;
  }
  return out;
}

async function collectCategories() {
  console.log(`  遍历 ${CATS.length} 个分类（匿名，纯 HTTP）...`);
  const ttwid = await getTtwid();
  const perCat = await Promise.all(CATS.map(c => processCategory(c, ttwid)));
  const map = new Map();
  for (let i = 0; i < CATS.length; i++) {
    console.log(`    分类 ${CATS[i]} → ${perCat[i].length} 个`);
    for (const r of perCat[i]) if (!map.has(r.rid)) map.set(r.rid, r);
  }
  return map;
}

// ── 输出 ──
function writeConfig(rooms, cookie) {
  const anchors = {};
  for (const r of rooms) anchors[r.rid] = r.nickname || r.rid;
  fs.writeFileSync(CONFIG, JSON.stringify({ anchors, cookie: cookie || '' }, null, 2), 'utf8');
}

function writeM3u(rooms) {
  const withUrl = rooms.filter(r => r.url);
  const lines = ['#EXTM3U'];
  for (const r of withUrl) {
    lines.push(`#EXTINF:-1 group-title="抖音直播",${r.nickname} (${r.user_count}人)`);
    lines.push(r.url);
  }
  fs.writeFileSync(M3U, lines.join('\n') + '\n', 'utf8');
  return withUrl.length;
}

function needCookie(cookie) {
  if (!cookie) { console.error('未配置登录 cookie（build\\config.json 里 cookie 为空）'); process.exit(1); }
}

// ── 主流程 ──
(async () => {
  const cfg = JSON.parse(fs.readFileSync(CONFIG, 'utf8'));
  const cookie = cfg.cookie || '';
  const map = new Map();

  let summary;
  if (source === 'follow') {
    needCookie(cookie);
    const cnt = await collectFollow(map, cookie);
    summary = `只关注：${cnt} 个`;
  } else if (source === 'feed') {
    needCookie(cookie);
    const followed = await collectFollow(map, cookie);
    const need = Math.max(0, n - followed);
    const feedAdded = await collectFeed(map, cookie, need);
    summary = `关注 ${followed} + 「全部」${feedAdded} → 去重 ${map.size} 个（目标 ${n}）`;
  } else {
    const cmap = await collectCategories();
    for (const [k, v] of cmap) map.set(k, v);
    summary = `分类去重 ${map.size} 个`;
  }

  let rooms = [...map.values()].sort((a, b) => b.user_count - a.user_count);
  if (source === 'category' && n !== Infinity) rooms = rooms.slice(0, n);
  console.log(summary);

  if (outMode !== 'iptv') writeConfig(rooms, cfg.cookie || '');
  let m3uN = 0;
  if (outMode !== 'config') m3uN = writeM3u(rooms);

  const parts = [];
  if (outMode !== 'iptv') parts.push(`anchors ${rooms.length} 个`);
  if (outMode !== 'config') parts.push(`iptv ${m3uN} 条`);
  console.log(`完成：${parts.join('，')}（${((Date.now() - t0) / 1000).toFixed(1)}s，流地址有时效）`);
})().catch(e => { console.error('失败：', e && e.message); process.exit(1); });
