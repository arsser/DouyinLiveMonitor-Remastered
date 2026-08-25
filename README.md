# DouyinLiveMonitor C 重写版

抖音直播弹幕抓取 + 直播源批量采集，纯 C 实现（Windows CLI + Win32 原生 GUI），并配套一组纯 HTTP 的 Node 采集脚本。

- **弹幕抓取**：纯 C（WinHTTP/Schannel + zlib），**X-Bogus 签名算法已纯 C 逆向移植**，无需 Node/JS 引擎。
- **直播源采集**：Node 脚本纯 HTTP 调用，`a_bogus` 签名由 BDMS SDK（`bdms.js`）在 Node `vm` 内直接生成，**无需浏览器**；登录态下关注/推荐接口更可**免签名**。

---

## 一、产物总览

| 产物 | 位置 | 说明 |
|------|------|------|
| `dlm.exe` | `build/` | 控制台版：连接直播间抓弹幕、增删主播、检测开播、取流、用 PotPlayer 播放 |
| `dlm_gui.exe` | `build/` | Win32 原生 GUI 版（无控制台窗口），功能同 CLI + 图形化 |
| `config.json` | `build/` | 运行时配置：`anchors`（房间号→昵称）+ `cookie`（登录态，敏感） |
| `iptv.m3u` | `build/` | IPTV 播放列表，`#EXTINF` 标题 = `昵称 (人数)人`，供 PotPlayer 等播放器 |
| `capture.mjs` | 根目录 | **主采集脚本**：`ttwid`/`cookie` 两种来源 + `iptv`/`oiptv` 输出，用指令切换 |
| `bdms.js` | `node_scraper/` | BDMS 安全 SDK（v1.0.1.19-fix.01），`a_bogus.mjs` 的运行时依赖 |
| `a_bogus.mjs` | `node_scraper/` | `a_bogus` 签名生成器（供 `capture.mjs` 的 ttwid 模式调用） |
| `bdms_shim.mjs` | `node_scraper/` | 在 Node `vm` 中运行 `bdms.js` 所需的浏览器 shim |
| `src/` `include/` | 根目录 | C 源码 / 头文件 |
| `test/` | 根目录 | 离线单元测试（X-Bogus 对拍等） |
| `tools/` | 根目录 | X-Bogus 逆向工具（Node 脚本，仅开发参考，非运行时依赖） |

---

## 二、目录结构

```
c_rewrite/
├── CMakeLists.txt                      # CMake 构建
├── capture.mjs                         # 采集入口（ttwid/cookie + iptv/oiptv）
├── README.md
├── include/                            # C 头文件（11 个）
├── src/                                # C 源码（11 个）
├── test/                               # C 单元测试（3 个）
├── tools/                              # X-Bogus 逆向工具（开发参考）
├── node_scraper/                       # 签名依赖（3 个）
│   ├── bdms.js                         # BDMS SDK
│   ├── a_bogus.mjs                     # a_bogus 签名
│   └── bdms_shim.mjs                   # 浏览器 shim
└── build/
    ├── dlm.exe / dlm_gui.exe           # 编译产物
    ├── config.json                     # 运行时配置（anchors + cookie）
    └── iptv.m3u                        # IPTV 播放列表
```

> 弹幕归档目录：运行时在 exe 同目录自动创建 `data/{主播名}（{ID}）/{日期}.jsonl`（即 `build/data/`）。

---

## 三、编译

需要 MinGW-w64 gcc + zlib（如 MSYS2 ucrt64 环境）。

```bash
cmake -S . -B build -G "Ninja"
cmake --build build
```

或用 gcc 手动编译（`src/` 下 11 个 `.c`；GUI 版追加 `-lgdi32 -mwindows`）。

---

## 四、使用

### 1. GUI（dlm_gui.exe）

双击运行，提供：

- **主播列表**：添加/删除主播；**搜索框**（输入昵称或房间号，滚动定位到匹配主播）。
- **一键检测**：全并发检测所有主播开播状态（每 50 个主播轮换一个匿名 ttwid）。
- **选中即检测**：点击列表中的主播自动检测，开播则自动连接抓弹幕。
- **播放**：取最佳清晰度用 PotPlayer 播放；右键「播放」按钮可多清晰度选择或复制直链。
- **Cookie**：配置登录 Cookie（礼物消息/完整流需要）。
- **消息过滤**：弹幕 / 礼物 / 点赞 / 进场 / 关注 / 粉丝团 复选框。
- **消息区**：彩色消息显示、自动滚动（浏览历史时不打断）。
- **状态栏**：观看人数 + 连接状态。

### 2. 命令行（dlm.exe）

```bat
dlm.exe <直播间ID> [--only chat,gift]   :: 连接直播间，打印并存档弹幕
dlm.exe list                            :: 列出已配置主播
dlm.exe add <ID> [名称]                 :: 添加主播（名称留空自动获取昵称）
dlm.exe del <ID>                        :: 删除主播
dlm.exe check <ID>                      :: 检测单个主播开播状态
dlm.exe check                           :: 一键检测所有主播
dlm.exe stream <ID>                     :: 打印多清晰度直播流直链
dlm.exe play <ID>                       :: 提取最佳清晰度并用 PotPlayer 播放
dlm.exe cookie <Cookie>                 :: 设置登录 Cookie
```

`--only` 类型：`chat, gift, like, member, social, stats, fansclub, emoji, room, roomstats, control`。

### 3. Node 采集脚本（capture.mjs，位于项目根目录）

格式：`node capture.mjs [ttwid|cookie] [iptv|oiptv] [N|max]`

```bat
:: 默认：cookie 抓所有关注 → 只写 config
node capture.mjs

:: cookie 抓关注 + 「全部」feed（合计 500）→ config + iptv
node capture.mjs cookie iptv 500

:: 省略第一项时第二项前移：cookie 抓所有关注 → config + iptv
node capture.mjs iptv

:: ttwid 匿名抓 8 分类全量 → config + iptv
node capture.mjs ttwid iptv max

:: ttwid 匿名抓 8 分类前 3000 → 只写 iptv（不改 config）
node capture.mjs ttwid oiptv 3000

:: 单独生成 a_bogus 签名
node node_scraper\a_bogus.mjs "<query>"
```

| 参数 | 取值 | 说明 |
|------|------|------|
| 第一项 | `ttwid` / `cookie` / 省略 | `ttwid`=匿名抓 8 分类；`cookie`=登录态抓关注+全部；省略=登录态抓所有关注（忽略第三项） |
| 第二项 | `iptv` / `oiptv` / 省略 | `iptv`=写 config + iptv；`oiptv`=只写 iptv；省略=只写 config |
| 第三项 | `N` / `max` / 省略 | 数量（省略默认 100）；`max`=最大捕获，仅第一项为 `ttwid` 时生效 |

> 说明：
> - `cookie`（及省略第一项）用登录态（live.douyin.com），免签名；`ttwid` 匿名（a_bogus + ttwid）。
> - 除 `oiptv` 外，都会**直接覆写** `build/config.json` 的 `anchors`（保留 cookie），不备份、不对比旧数据。
> - 「全部」feed 每页固定 20 条、按 `maxtime` 串行翻页，是服务端上限。

### 4. IPTV + PotPlayer

`iptv.m3u` 直接拖入 PotPlayer 即可作为直播列表播放。每条直链取**原画（origin）**，缺失时依次回退蓝光(FULL_HD1)→超清(HD1)→高清(SD2)→标清(SD1)。

> 直链有时效（几分钟～几小时），过期后重跑 `capture.mjs` 刷新。

---

## 五、Cookie 策略（重要）

| 用途 | 是否用登录 Cookie |
|------|------------------|
| `capture.mjs` 的 `cookie` 模式 / 省略第一项（关注列表 + 个性化「全部」feed） | ✅ 用（`live.douyin.com` feed/follow_top） |
| `capture.mjs` 的 `ttwid` 模式（8 分类采集） | ❌ 匿名（a_bogus + ttwid） |
| GUI/CLI 开播检测（enter 接口） | ❌ 匿名 ttwid（`enter_fetch` 忽略登录 cookie） |
| GUI/CLI 弹幕 WebSocket 连接 | ✅ 若已配置 cookie 则用（礼物消息/完整流） |

登录 Cookie 只存于 `build/config.json` 的 `cookie` 字段，属敏感信息，勿外泄。

---

## 六、技术要点

- **X-Bogus**：`src/xbogus.c` 纯 C 实现（byted_acrawler 1.0.0.53 的字节码 VM 逆向还原），与 `sign.js` 精确对拍一致，见 `tools/` 与 `test/test_xbogus.c`。
- **a_bogus**：由 BDMS SDK（`bdms.js`）生成；`a_bogus.mjs` 把 `bdms.js` 原样加载进 Node `vm`（配 `bdms_shim.mjs`），调用 `init()` 后通过被拦截的 `fetch` 取出签名后的 URL 里的 `a_bogus`。算法为 S1 base64（字母表与 X-Bogus 同源）+ SM3 哈希链（盐 `dhzx`），输入含 queryString、UA、设备指纹、pageId=7571、aid=6383、时间戳与随机数（非确定性）。
- **免签名**：登录态下 `/webcast/feed/`、`/webcast/feed/follow_top/` 实测不需要 `a_bogus`，`capture.mjs` 的 `cookie` 模式据此免去每页签名开销。
- **直链质量**：原画（嵌套 `stream_data` 的 `data.origin.main.flv`）优先，回退 `flv_pull_url` 的 FULL_HD1/HD1/SD2/SD1。

---

## 七、构建依赖（需自行获取）

仓库**不包含**以下两个从抖音网页提取的第三方文件（版权原因），克隆后需自行获取并放到对应位置：

| 文件 | 放置位置 | 用途 | 获取方法 |
|------|---------|------|---------|
| `bdms.js` | `node_scraper/bdms.js` | BDMS 安全 SDK（v1.0.1.19-fix.01），`capture.mjs` 的 `ttwid` 模式生成 `a_bogus` 必需 | 浏览器打开 `live.douyin.com` → F12 → Network → 过滤 `bdms` → 找到 `bdms.js` 资源 → 保存为该路径 |
| `sign_folded.js` | `tools/sign_folded.js` | byted_acrawler 1.0.0.53 签名器（X-Bogus 逆向参考，仅开发用） | 浏览器打开抖音网页 → F12 → Network → 过滤 `sign`/`acrawler` → 保存为 `sign_folded.js` |

> 缺少 `bdms.js` 时：`capture.mjs` 的 `cookie` 模式仍可用（登录态免签名）；`ttwid` 模式会报「bdms SDK 加载失败」。

---

## 声明

本项目仅用于**学习研究**（逆向工程、协议分析、原生 GUI 编程），严禁用于商业谋利、盗取个人信息、破坏系统或任何违法用途。

- 本项目与抖音 / 字节跳动**无任何关联**，所用接口与签名算法均为个人学习研究所得。
- 仓库**不包含**从抖音网页提取的第三方 SDK（`bdms.js`、`sign_folded.js`），请自行获取并遵守其原始许可与相关法律。
- 请勿将本项目用于大规模抓取、绕过付费或干扰平台正常运行。

MIT License © 2026 eplayn

---

## 八、作为 Douyin-Refine 本机签名器（迁新机器）

本仓库的 `node_scraper/sign_aweme.mjs` 给 [Douyin-Refine](https://github.com/arsser/Douyin-Refine) 当本机 **5557** 签名器：打开专用 Chrome（CDP **9229**），用页面里的 `a_bogus` 拉收藏。转写/精炼仍在生产看板，不在这台 Windows 上。

下面是把 **Douyin-Refine 本机客户端** 迁到新机器的步骤。生产服务器（例如 `http://10.40.29.50:8090`）不用动。

### 1. 三个仓库必须是兄妹目录

启动脚本按相对路径找邻居，**三个文件夹名不要改**（盘符和父目录可以改）：

```text
某个父目录\
  Douyin-Refine\
  TikTokDownloader\
  DouyinLiveMonitor-Remastered\     ← 本仓库
```

- 下载器路径可用 Douyin-Refine `.env` 的 `TIKTOK_DOWNLOADER_HOST_DIR` 覆盖。
- 签名器路径写死为 `../DouyinLiveMonitor-Remastered/node_scraper/sign_aweme.mjs`，没有环境变量可改。

### 2. 新机器要装的软件

- Windows + Git
- Python 3.12（加入 PATH）
- Node 20+（无全局 `WebSocket` 时依赖下面的 `npm install`）
- Google Chrome（专用窗口；不要用 `--disable-blink-features=AutomationControlled`，新版 Chrome 会弹黄条挡页面）
- 能访问生产看板的网络（VPN）
- **上传模式**（本机只拉收藏、下视频、上传）不需要 ffmpeg、Obsidian Vault
- **本机完整流水线**才需要 ffmpeg，以及 `.env` 里的 API Key 与 `DOUYIN_PUBLISH_HOST_DIR`

### 3. 克隆与依赖

```powershell
cd <父目录>
git clone <Douyin-Refine 远程> Douyin-Refine
git clone <TikTokDownloader 远程> TikTokDownloader
git clone <本仓库远程> DouyinLiveMonitor-Remastered

cd DouyinLiveMonitor-Remastered\node_scraper
npm install

cd ..\..\TikTokDownloader
pip install -r requirements.txt

cd ..\Douyin-Refine
pip install -r requirements-desktop.txt
copy .env.example .env
```

`node_scraper` 至少要装 `ws`（见 `package.json`）。`bdms.js` 需自行放到 `node_scraper/bdms.js`（见上文「构建依赖」）；收藏拉取走 Chrome 实签，没有 `bdms_www.js` 也能跑。

把旧机器上尚未提交的本仓库改动一并带过来（例如去掉 AutomationControlled、CDP timeout 重试），否则 clone 会回到旧行为。

### 4. 从旧机器拷哪些文件

Douyin-Refine 的 `data/` **不进 git**，按需拷贝：

| 文件 | 是否拷贝 |
|------|----------|
| `Douyin-Refine/.env` | 要。改掉本机绝对路径（`DOUYIN_PUBLISH_HOST_DIR`、`TIKTOK_VOLUME_DIR` 等） |
| `Douyin-Refine/data/desktop.json` | 要。模式、`serverUrl`、ingest 令牌、定时 |
| `Douyin-Refine/data/tiktok-downloader/douyin.cookie` | 可选。登录在专用 Chrome 里，新机器扫码即可 |
| `Douyin-Refine/data/transcripts/pending_queue.jsonl` | 上传模式一般不拷。启动时用生产队列覆盖本机 |
| 专用 Chrome 用户目录 | 不要拷。profile 在系统临时目录（默认 `%TEMP%\douyin-abogus-chrome`），新机器重新扫码 |

`data/desktop.json` 的 `serverUrl` 必须是生产看板，例如 `http://10.40.29.50:8090`，不要填 `127.0.0.1:8091`（那是本机控制页）。ingest 令牌与生产 `.env` 的 `DOUYIN_INGEST_TOKEN` 一致。

### 5. 新机器第一次跑

1. 双击 `Douyin-Refine\start-douyin-refine.bat`  
   顺序：侧车 `5555` / `5556` / `5557` → 本机看板 `8090` → 桌面控制页 `8091`。
2. 控制页确认 `serverUrl` 与 ingest 令牌。
3. 点「打开专用 Chrome」，在 `https://www.douyin.com/user/self` 扫码登录。登录探测看 CDP 里的 `sessionid` 等 HttpOnly Cookie，不要只看页面标题。
4. 手动跑一次拉取，确认能进队、能上传到生产。

占用端口：`5555`、`5556`、`5557`、`8090`、`8091`，专用 Chrome CDP `9229`。被占用先关掉旧进程。

签名器日志：`Douyin-Refine/data/tiktok-downloader/host-5557.log`。

若 Chrome 顶上出现「不受支持的命令行标记：`--disable-blink-features=AutomationControlled`」，关掉黄条后拉取往往能恢复；正确做法是启动参数里不要带这条（`chrome_cdp_sign.mjs` 已去掉），然后**完全退出专用 Chrome 并重启 5557**。

`CDP timeout Runtime.evaluate` 不是 Cookie 过期，而是页面卡住（验证码、登录弹层、XHR 挂死）。关掉弹层后重试；必要时重启 5557。不要为此去改 `douyin.cookie`。

### 6. 旧机器务必停掉

两台同时开桌面会抢收藏、抢心跳，飞书会乱报在线/过期。新机器跑通后，旧机器不要再开 `start-douyin-refine.bat` 和控制页。飞书「本机心跳过期」只表示生产看板一段时间没收到 `POST /api/heartbeat`，窗口开着不等于在线。

### 7. 生产看板若也要迁

那是另一条线：新 Linux 机按 Douyin-Refine 的 `deploy/docs/00-部署总览.md` 部署 Docker + systemd，拷 `deploy/production/.deploy.env`、`data/`、Vault git，再把桌面 `serverUrl` 改成新 IP。只迁本机客户端时不必做这一步。
