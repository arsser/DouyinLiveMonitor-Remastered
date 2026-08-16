// 生成 X-Bogus 对照真值：在“裸 V8 上下文”里跑 sign.js（等价 MiniRacer 环境，无 Node 全局），
// 复刻 liveMan.py 的 generateSignature 输入链。
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const signPath = fileURLToPath(new URL('../../sign.js', import.meta.url));
const code = readFileSync(signPath, 'utf8');

// 裸上下文：不注入 document/window/navigator/process/require，由 sign.js 自己打桩（等价 MiniRacer）
const ctx = vm.createContext({});
vm.runInContext(code, ctx, { filename: 'sign.js' });

const get_sign = vm.runInContext('get_sign', ctx);
if (typeof get_sign !== 'function') {
  console.error('get_sign 未定义');
  process.exit(1);
}

// 与 liveMan.py 完全一致的 wss（room_id 用样例值）
const wss =
  'wss://webcast100-ws-web-lq.douyin.com/webcast/im/push/v2/?app_name=douyin_web' +
  '&version_code=180800&webcast_sdk_version=1.0.14-beta.0' +
  '&update_version_code=1.0.14-beta.0&compress=gzip&device_platform=web&cookie_enabled=true' +
  '&screen_width=1536&screen_height=864&browser_language=zh-CN&browser_platform=Win32' +
  '&browser_name=Mozilla' +
  '&browser_version=5.0%20(Windows%20NT%2010.0;%20Win64;%20x64)%20AppleWebKit/537.36%20(KHTML,' +
  '%20like%20Gecko)%20Chrome/126.0.0.0%20Safari/537.36' +
  '&browser_online=true&tz_name=Asia/Shanghai' +
  '&cursor=d-1_u-1_fh-7392091211001140287_t-1721106114633_r-1' +
  '&internal_ext=internal_src:dim|wss_push_room_id:123456789|wss_push_did:7319483754668557238' +
  '|first_req_ms:1721106114541|fetch_time:1721106114633|seq:1|wss_info:0-1721106114633-0-0|' +
  'wrds_v:7392094459690748497' +
  '&host=https://live.douyin.com&aid=6383&live_id=1&did_rule=3&endpoint=live_pc&support_wrds=1' +
  '&user_unique_id=7319483754668557238&im_path=/webcast/im/fetch/&identity=audience' +
  '&need_persist_msg_count=15&insert_task_id=&live_reason=&room_id=123456789&heartbeatDuration=0';

// 复刻 liveMan.py：query.split('&')，每项 split('=')[-1] 作为值（保留 URL 编码、取最后一段）
const params =
  'live_id,aid,version_code,webcast_sdk_version,room_id,sub_room_id,sub_channel_id,did_rule,user_unique_id,device_platform,device_type,ac,identity'.split(',');
const wssQuery = wss.split('?')[1];
const wssMaps = {};
for (const pair of wssQuery.split('&')) {
  const parts = pair.split('=');
  wssMaps[parts[0]] = parts[parts.length - 1];
}
const param = params.map((p) => `${p}=${wssMaps[p] ?? ''}`).join(',');
const md5 = createHash('md5').update(param, 'utf8').digest('hex');

const xbogus1 = get_sign(md5);
const xbogus2 = get_sign(md5);
console.log('param   =', param);
console.log('md5     =', md5);
console.log('X-Bogus =', xbogus1);
console.log('再次调用 =', xbogus2);
console.log('确定性   =', xbogus1 === xbogus2 ? '是（同输入同输出）' : '否（含随机/时间）');

// 输出几组随机输入的对照，供 C 端回归
const extra = [];
for (let i = 0; i < 8; i++) {
  const m = createHash('md5').update(`seed-${i}`).digest('hex');
  extra.push([m, get_sign(m)]);
}
console.log('\n-- 附加真值 (md5 xbogus) --');
for (const [m, x] of extra) console.log(`${m} ${x}`);
