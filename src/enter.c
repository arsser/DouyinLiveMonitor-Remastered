/* 直播间信息检测（enter API）+ 直播流提取，对应原 chat_window.py 的 fetch_room_info */
#include "enter.h"
#include "http.h"
#include "json_dom.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_token_counter = 0;

static void gen_ms_token(char *out, size_t outlen) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    size_t n = outlen - 1;
    if (n > 107) n = 107;
    unsigned int s = (unsigned int)(InterlockedIncrement(&g_token_counter) * 2654435761u);
    for (size_t i = 0; i < n; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;   /* xorshift32：线程安全、无需 rand() */
        out[i] = chars[s % (sizeof(chars) - 1)];
    }
    out[n] = '\0';
}

/* 获取一个新的匿名 ttwid cookie（"ttwid=..."） */
int fetch_anon_ttwid(char *buf, size_t sz) {
    http_response_t r;
    memset(&r, 0, sizeof(r));
    if (http_get("https://live.douyin.com/", NULL, NULL, &r) != 0) return -1;
    char *tw = http_cookie_value(r.set_cookie, "ttwid");
    if (tw) { snprintf(buf, sz, "ttwid=%s", tw); free(tw); }
    http_response_free(&r);
    return buf[0] ? 0 : -1;
}

static const char *QUALITY_ORDER[] = { "origin", "FULL_HD1", "HD1", "SD2", "SD1", "audio" };
#define NQ (sizeof(QUALITY_ORDER) / sizeof(QUALITY_ORDER[0]))

static void add_stream(room_info_t *info, const char *name, const char *url) {
    if (!url || !url[0] || !name || !name[0]) return;
    if (info->stream_count >= 8) return;
    for (size_t i = 0; i < info->stream_count; i++) {
        if (strcmp(info->streams[i].name, name) == 0) return;
    }
    snprintf(info->streams[info->stream_count].name, sizeof(info->streams[0].name), "%s", name);
    snprintf(info->streams[info->stream_count].url, sizeof(info->streams[0].url), "%s", url);
    info->stream_count++;
}

int enter_fetch_ttwid(const char *rid, const char *ttwid_cookie, room_info_t *info) {
    memset(info, 0, sizeof(*info));
    if (!ttwid_cookie || !ttwid_cookie[0]) return -1;

    /* 构建 enter URL */
    char mst[128];
    gen_ms_token(mst, sizeof(mst));
    char url[800];
    snprintf(url, sizeof(url),
        "https://live.douyin.com/webcast/room/web/enter/?aid=6383"
        "&app_name=douyin_web&live_id=1&device_platform=web&language=zh-CN"
        "&enter_from=page_refresh&cookie_enabled=true"
        "&screen_width=1920&screen_height=1080&browser_language=zh-CN"
        "&browser_platform=Win32&browser_name=Chrome&browser_version=140.0.0.0"
        "&web_rid=%s&room_id_str=%s"
        "&enter_source=&is_need_double_stream=false&insert_task_id=&live_reason="
        "&msToken=%s", rid, rid, mst);

    char referer[256];
    snprintf(referer, sizeof(referer), "https://live.douyin.com/%s", rid);

    /* 3. 请求 */
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    if (http_get(url, ttwid_cookie, referer, &resp) != 0) return -1;

    /* 4. 解析 JSON */
    jval_t *root = json_parse(resp.body);
    http_response_free(&resp);
    if (!root) return -1;

    jval_t *data = json_get(root, "data");
    if (!data || data->type != J_OBJ) { json_free(root); return -1; }

    jval_t *user = json_get(data, "user");
    if (user && user->type == J_OBJ) {
        const char *nick = json_str(json_get(user, "nickname"));
        if (nick) snprintf(info->nickname, sizeof(info->nickname), "%s", nick);
    }

    jval_t *room = json_at(json_get(data, "data"), 0); /* data.data 是数组 */
    if (room && room->type == J_OBJ) {
        info->is_live = (json_int(json_get(room, "status")) == 2);
        const char *t = json_str(json_get(room, "title"));
        if (t) snprintf(info->title, sizeof(info->title), "%s", t);
        const char *uc = json_str(json_get(room, "user_count_str"));
        if (uc) snprintf(info->user_count, sizeof(info->user_count), "%s", uc);

        jval_t *stream_url = json_get(room, "stream_url");
        if (stream_url && stream_url->type == J_OBJ) {
            /* flv_pull_url：FULL_HD1/HD1/SD1/SD2 → url */
            jval_t *flv = json_get(stream_url, "flv_pull_url");
            if (flv && flv->type == J_OBJ) {
                for (size_t i = 0; i < flv->kcount; i++) {
                    add_stream(info, flv->keys[i], json_str(flv->vals[i]));
                }
            }
            /* live_core_sdk_data.pull_data.stream_data：内嵌 JSON 字符串，含 origin/audio */
            jval_t *sdk = json_get(stream_url, "live_core_sdk_data");
            if (sdk && sdk->type == J_OBJ) {
                jval_t *pull = json_get(sdk, "pull_data");
                const char *sdata = pull ? json_str(json_get(pull, "stream_data")) : NULL;
                if (sdata && sdata[0]) {
                    jval_t *sd = json_parse(sdata);
                    if (sd) {
                        jval_t *sddata = json_get(sd, "data");
                        if (sddata && sddata->type == J_OBJ) {
                            jval_t *omain = json_get(json_get(sddata, "origin"), "main");
                            add_stream(info, "origin", json_str(json_get(omain, "flv")));
                            jval_t *amain = json_get(json_get(sddata, "ao"), "main");
                            add_stream(info, "audio", json_str(json_get(amain, "flv")));
                        }
                        json_free(sd);
                    }
                }
            }
        }
    }

    json_free(root);
    return 0;
}

/* 每次调用取一个新的匿名 ttwid 再查询（单次检测用） */
int enter_fetch(const char *rid, const char *cookie, room_info_t *info) {
    (void)cookie;
    char ttwid[512];
    if (fetch_anon_ttwid(ttwid, sizeof(ttwid)) != 0) return -1;
    return enter_fetch_ttwid(rid, ttwid, info);
}

const char *room_stream(const room_info_t *info, const char *name) {
    for (size_t i = 0; i < info->stream_count; i++) {
        if (strcmp(info->streams[i].name, name) == 0) return info->streams[i].url;
    }
    return NULL;
}

const char *room_best_stream(const room_info_t *info) {
    for (size_t q = 0; q < NQ; q++) {
        const char *u = room_stream(info, QUALITY_ORDER[q]);
        if (u) return u;
    }
    return NULL;
}
