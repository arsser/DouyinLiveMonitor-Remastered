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
