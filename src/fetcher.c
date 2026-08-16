/* 直播间弹幕抓取：WinHTTP WebSocket 实现（对应 liveMan.py） */
#include "fetcher.h"
#include "gzip.h"
#include "http.h"
#include "md5.h"
#include "protobuf.h"
#include "xbogus.h"
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t *UA = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                           L"(KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";

static wchar_t *u8w(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* 生成随机 msToken（[A-Za-z0-9_-] 182 位） */
static void gen_ms_token(char *out, size_t outlen) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    size_t n = outlen - 1;
    if (n > 182) n = 182;
    for (size_t i = 0; i < n; i++) out[i] = chars[rand() % (sizeof(chars) - 1)];
    out[n] = '\0';
}

/* 构建 PushFrame 序列化字节（hb/ack 帧）。返回写入长度；空间不足返回 0。 */
static size_t build_pushframe(uint8_t *buf, size_t cap, uint64_t log_id,
                              const char *payload_type, const uint8_t *payload, size_t payload_len) {
    size_t n = 0;
    uint64_t v;

    if (log_id) {
        if (n + 1 + 10 > cap) return 0; /* field tag + 最长 10 字节 varint */
        buf[n++] = (uint8_t)((2 << 3) | 0); /* field 2, varint */
        v = log_id;
        while (v >= 0x80) { buf[n++] = (uint8_t)((v & 0x7f) | 0x80); v >>= 7; }
        buf[n++] = (uint8_t)v;
    }
    size_t ptlen = strlen(payload_type);
    if (ptlen > 0x7f || n + 2 + ptlen > cap) return 0;
    buf[n++] = (uint8_t)((7 << 3) | 2); /* field 7, len-delimited */
    buf[n++] = (uint8_t)ptlen;
    memcpy(buf + n, payload_type, ptlen); n += ptlen;
    if (payload && payload_len) {
        if (n + 1 + 10 + payload_len > cap) return 0;
        buf[n++] = (uint8_t)((8 << 3) | 2); /* field 8, len-delimited */
        v = payload_len;
        while (v >= 0x80) { buf[n++] = (uint8_t)((v & 0x7f) | 0x80); v >>= 7; }
        buf[n++] = (uint8_t)v;
        memcpy(buf + n, payload, payload_len); n += payload_len;
    }
    return n;
}

/* 在 HTML 中查找 roomId 后紧跟的数字序列（容错多种转义/引号形式） */
static const char *find_room_id_digits(const char *body) {
    const char *p = body;
    while ((p = strstr(p, "roomId")) != NULL) {
        const char *q = p + 6;
        for (int i = 0; i < 10 && *q; i++) {
            if (*q >= '0' && *q <= '9') return q;
            q++;
        }
        p += 6;
    }
    return NULL;
}

/* 获取 room_id：GET live.douyin.com/{rid} 后从 HTML 抠 roomId */
static int get_room_id(const char *live_id, const char *ttwid, char *room_id, size_t room_id_len) {
    char url[512];
    snprintf(url, sizeof(url), "https://live.douyin.com/%s", live_id);
    char mst[256];
    gen_ms_token(mst, sizeof(mst));
    char ck[512];
    snprintf(ck, sizeof(ck), "ttwid=%s; msToken=%s; __ac_nonce=0123407cc00a9e438deb4", ttwid, mst);

    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    if (http_get(url, ck, NULL, &resp) != 0) {
        fprintf(stderr, "room 页 HTTP 请求失败\n");
        return -1;
    }

    fprintf(stderr, "[debug] room 页 status=%d body_len=%zu\n", resp.status_code, resp.body_len);

    const char *p = find_room_id_digits(resp.body ? resp.body : "");
    if (!p) {
        fprintf(stderr, "[debug] 未找到 roomId，响应前 300 字节:\n");
        if (resp.body) {
            for (int i = 0; i < 300 && resp.body[i]; i++) {
                fputc(resp.body[i] >= 0x20 && resp.body[i] < 0x7f ? resp.body[i] : '.', stderr);
            }
            fputc('\n', stderr);
        } else {
            fprintf(stderr, "(body 为空)\n");
        }
        http_response_free(&resp);
        return -1;
    }
    size_t i = 0;
    while (p[i] >= '0' && p[i] <= '9' && i < room_id_len - 1) { room_id[i] = p[i]; i++; }
    room_id[i] = '\0';
    http_response_free(&resp);
    return (i > 0) ? 0 : -1;
}

/* 接收一条完整二进制消息（累积分片）。返回 0=成功，1=超时无数据，-1=错误。 */
static int ws_recv_binary(HINTERNET ws, uint8_t **out, size_t *out_len) {
    size_t cap = 256 * 1024, len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return -1;

    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        if (len == cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
        }
        DWORD err = WinHttpWebSocketReceive(ws, buf + len, (DWORD)(cap - len), &read, &type);
        if (err == ERROR_WINHTTP_TIMEOUT) {
            free(buf); return 1;  /* 空闲超时：让上层检查 stop */
        }
        if (err != ERROR_SUCCESS) {
            free(buf); return -1;
        }
        len += read;
        if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) break;
        if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) continue;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        /* UTF8 或其它类型：忽略 */
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) break;
        /* PING/PONG 已由栈自动处理 */
    }
    *out = buf;
    *out_len = len;
    return 0;
}

/* 心跳线程：每 5s 发送 hb 帧（用事件信号停止，确保线程退出后才释放栈对象） */
typedef struct {
    HINTERNET ws;
    HANDLE stop_event;
} hb_arg_t;

static DWORD WINAPI heartbeat_thread(LPVOID arg) {
    hb_arg_t *a = (hb_arg_t *)arg;
    uint8_t frame[64];
    size_t flen = build_pushframe(frame, sizeof(frame), 0, "hb", NULL, 0);
    for (;;) {
        if (WaitForSingleObject(a->stop_event, 5000) == WAIT_OBJECT_0) break;
        WinHttpWebSocketSend(a->ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, frame, (DWORD)flen);
    }
    return 0;
}

/* 构建 wss URL（不含 signature） */
static void build_wss(char *wss, size_t cap, const char *room_id) {
    snprintf(wss, cap,
        "wss://webcast100-ws-web-lq.douyin.com/webcast/im/push/v2/?app_name=douyin_web"
        "&version_code=180800&webcast_sdk_version=1.0.14-beta.0"
        "&update_version_code=1.0.14-beta.0&compress=gzip&device_platform=web&cookie_enabled=true"
        "&screen_width=1536&screen_height=864&browser_language=zh-CN&browser_platform=Win32"
        "&browser_name=Mozilla"
        "&browser_version=5.0%%20(Windows%%20NT%%2010.0;%%20Win64;%%20x64)%%20AppleWebKit/537.36%%20(KHTML,"
        "%%20like%%20Gecko)%%20Chrome/126.0.0.0%%20Safari/537.36"
        "&browser_online=true&tz_name=Asia/Shanghai"
        "&cursor=d-1_u-1_fh-7392091211001140287_t-1721106114633_r-1"
        "&internal_ext=internal_src:dim|wss_push_room_id:%s|wss_push_did:7319483754668557238"
        "|first_req_ms:1721106114541|fetch_time:1721106114633|seq:1|wss_info:0-1721106114633-0-0|"
        "wrds_v:7392094459690748497"
        "&host=https://live.douyin.com&aid=6383&live_id=1&did_rule=3&endpoint=live_pc&support_wrds=1"
        "&user_unique_id=7319483754668557238&im_path=/webcast/im/fetch/&identity=audience"
        "&need_persist_msg_count=15&insert_task_id=&live_reason=&room_id=%s&heartbeatDuration=0",
        room_id, room_id);
}

/* 计算 signature：13 参数逗号串 → MD5 → X-Bogus */
static void compute_signature(const char *room_id, char sig[17]) {
    char param[1024];
    snprintf(param, sizeof(param),
        "live_id=1,aid=6383,version_code=180800,webcast_sdk_version=1.0.14-beta.0,"
        "room_id=%s,sub_room_id=,sub_channel_id=,did_rule=3,user_unique_id=7319483754668557238,"
        "device_platform=web,device_type=,ac=,identity=audience", room_id);
    uint8_t d[16]; char hex[33];
    md5_str(param, d); md5_hex(d, hex);
    xbogus_generate(hex, sig);
}

/* 建立 WebSocket 连接。成功返回 ws 句柄并输出 session/conn/req；失败返回 NULL 并自行清理。 */
static HINTERNET ws_connect(const char *wss, const char *cookie_src, const char *ttwid,
                            HINTERNET *out_session, HINTERNET *out_conn, HINTERNET *out_req) {
    *out_session = NULL; *out_conn = NULL; *out_req = NULL;

    char parse_url[2200];
    snprintf(parse_url, sizeof(parse_url), "https://%s", wss + 6);
    wchar_t *wurl = u8w(parse_url);
    if (!wurl) return NULL;

    URL_COMPONENTS uc; memset(&uc, 0, sizeof(uc)); uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1; uc.dwUrlPathLength = (DWORD)-1; uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { free(wurl); fprintf(stderr, "URL 解析失败\n"); return NULL; }

    wchar_t host[256] = {0}, path[2048] = {0};
    memcpy(host, uc.lpszHostName, uc.dwHostNameLength * sizeof(wchar_t));
    int plen = 0;
    if (uc.dwUrlPathLength) { memcpy(path, uc.lpszUrlPath, uc.dwUrlPathLength * sizeof(wchar_t)); plen = (int)uc.dwUrlPathLength; }
    if (uc.dwExtraInfoLength) { memcpy(path + plen, uc.lpszExtraInfo, uc.dwExtraInfoLength * sizeof(wchar_t)); plen += (int)uc.dwExtraInfoLength; }
    if (plen == 0) { path[0] = L'/'; path[1] = 0; }

    HINTERNET session = WinHttpOpen(UA, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { free(wurl); return NULL; }
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);  /* 解析/连接超时需设在 session 上 */
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); free(wurl); return NULL; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); free(wurl); return NULL; }
    WinHttpSetTimeouts(req, 10000, 10000, 10000, 10000);

    wchar_t hdrs[20000] = {0};
    wcsncpy(hdrs, L"User-Agent: ", 19999); wcsncat(hdrs, UA, 19999 - wcslen(hdrs)); wcsncat(hdrs, L"\r\n", 19999 - wcslen(hdrs));
    if (cookie_src) {
        wchar_t *wc = u8w(cookie_src);
        wcsncat(hdrs, L"Cookie: ", 19999 - wcslen(hdrs));
        if (wc) { wcsncat(hdrs, wc, 19999 - wcslen(hdrs)); free(wc); }
        wcsncat(hdrs, L"\r\n", 19999 - wcslen(hdrs));
    } else {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "ttwid=%s", ttwid);
        wchar_t *wc = u8w(tmp);
        wcsncat(hdrs, L"Cookie: ", 19999 - wcslen(hdrs));
        if (wc) { wcsncat(hdrs, wc, 19999 - wcslen(hdrs)); free(wc); }
        wcsncat(hdrs, L"\r\n", 19999 - wcslen(hdrs));
    }
    WinHttpAddRequestHeaders(req, hdrs, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        fprintf(stderr, "WinHttpSetOption 失败\n");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); free(wurl);
        return NULL;
    }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        fprintf(stderr, "WinHttpSendRequest 失败\n");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); free(wurl);
        return NULL;
    }
    if (!WinHttpReceiveResponse(req, NULL)) {
        fprintf(stderr, "WinHttpReceiveResponse 失败\n");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); free(wurl);
        return NULL;
    }

    DWORD status = 0, sz = sizeof(status);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX)) {
        fprintf(stderr, "[debug] WS 握手 status=%lu\n", status);
    }

    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (!ws) {
        fprintf(stderr, "WebSocket 升级失败（可能需配置 Cookie）\n");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); free(wurl);
        return NULL;
    }
    WinHttpSetTimeouts(ws, 0, 0, 0, 1000);  /* 接收空闲超时 1s：让 stop 能打断阻塞接收 */

    free(wurl);
    *out_session = session;
    *out_conn = conn;
    *out_req = req;
    return ws;
}

int fetcher_run(const fetcher_config_t *cfg) {
    char room_id[64] = {0};
    char ttwid[256] = {0};

    /* 1. ttwid */
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    if (http_get("https://live.douyin.com/", NULL, NULL, &resp) != 0) {
        fprintf(stderr, "获取 ttwid 失败\n");
        return -1;
    }
    char *tw = http_cookie_value(resp.set_cookie, "ttwid");
    if (tw) { snprintf(ttwid, sizeof(ttwid), "%s", tw); free(tw); }
    http_response_free(&resp);
    if (!ttwid[0]) { fprintf(stderr, "未获取到 ttwid\n"); return -1; }
    if (cfg->stop && *cfg->stop) return -1;

    /* 2. room_id */
    if (get_room_id(cfg->live_id, ttwid, room_id, sizeof(room_id)) != 0) {
        fprintf(stderr, "获取 room_id 失败\n");
        return -1;
    }
    printf("room_id = %s\n", room_id);
    if (cfg->stop && *cfg->stop) return -1;

    /* 3-4. 构建 wss + 连接（带重试，每次重新生成签名） */
    const char *cookie_src = cfg->cookie[0] ? cfg->cookie : NULL;
    HINTERNET session = NULL, conn = NULL, req = NULL, ws = NULL;
    char wss[2048];

    for (int attempt = 1; attempt <= 3; attempt++) {
        if (cfg->stop && *cfg->stop) break;
        build_wss(wss, sizeof(wss), room_id);
        char sig[17];
        compute_signature(room_id, sig);
        strncat(wss, "&signature=", sizeof(wss) - strlen(wss) - 1);
        strncat(wss, sig, sizeof(wss) - strlen(wss) - 1);
        printf("signature = %s\n", sig);

        ws = ws_connect(wss, cookie_src, ttwid, &session, &conn, &req);
        if (ws) break;
        if (attempt < 3) {
            fprintf(stderr, "2 秒后重试（%d/3）...\n", attempt);
            Sleep(2000);
        }
    }
    if (!ws) {
        fprintf(stderr, "多次尝试后仍无法连接直播间\n");
        return -1;
    }
    printf("已连接到直播间 %s\n", cfg->live_id);
    if (cfg->ws_out) *cfg->ws_out = ws;
    if (cfg->on_open) cfg->on_open(cfg->ud);

    /* 5. 心跳线程 */
    hb_arg_t hb = { ws, CreateEvent(NULL, TRUE, FALSE, NULL) };
    HANDLE hth = hb.stop_event ? CreateThread(NULL, 0, heartbeat_thread, &hb, 0, NULL) : NULL;

    /* 6. 接收循环 */
    for (;;) {
        if (cfg->stop && *cfg->stop) break;
        uint8_t *frame = NULL; size_t flen = 0;
        int r = ws_recv_binary(ws, &frame, &flen);
        if (r == 1) continue;   /* 空闲超时：回到顶部检查 stop */
        if (r != 0) break;      /* 接收错误 */

        /* 解析 PushFrame：log_id(2) + payload(8) */
        uint64_t log_id = 0;
        const uint8_t *pf_payload; size_t pf_plen;
        pb_get_varint(frame, flen, 2, &log_id);
        if (!pb_get_bytes(frame, flen, 8, &pf_payload, &pf_plen)) { free(frame); continue; }

        /* gzip 解压 → Response */
        uint8_t *respdata = NULL; size_t resplen = 0;
        if (gzip_decompress(pf_payload, pf_plen, &respdata, &resplen) != 0) { free(frame); continue; }

        int need_ack = 0;
        const uint8_t *iext; size_t iext_len = 0;
        msg_handlers_t h = { .on_message = cfg->on_message, .ud = cfg->ud };
        messages_parse(respdata, resplen, &h, &need_ack, &iext, &iext_len);

        if (need_ack && iext && iext_len) {
            uint8_t ack[8192];
            size_t alen = build_pushframe(ack, sizeof(ack), log_id, "ack", iext, iext_len);
            if (alen > 0) {  /* 空间不足（internal_ext 过长）时放弃发送，避免越界 */
                WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, ack, (DWORD)alen);
            }
        }

        free(respdata);
        free(frame);

        if (h.should_stop) break; /* 直播间已结束 */
    }

    if (hb.stop_event) SetEvent(hb.stop_event);
    if (hth) { WaitForSingleObject(hth, INFINITE); CloseHandle(hth); }
    if (hb.stop_event) CloseHandle(hb.stop_event);
    if (cfg->ws_out) *cfg->ws_out = NULL;
    WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    WinHttpCloseHandle(ws);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    printf("连接已关闭\n");
    return 0;
}
