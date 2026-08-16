/* 抖音直播消息解析（12 类），基于最小 protobuf 解码 */
#include "messages.h"
#include "protobuf.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

time_source_t msg_fmt_time_src(int64_t ts, char out[16]) {
    time_source_t src = TIME_SRC_SYS;
    time_t t = time(NULL); /* 默认系统时间（兜底） */

    if (ts >= 100000000000LL) { /* 毫秒级 */
        ts /= 1000;
        src = TIME_SRC_MS;
    } else if (ts > 0) {        /* 秒级 */
        src = TIME_SRC_SEC;
    } else {                    /* 0 或负：系统时间 */
        src = TIME_SRC_SYS;
    }

    if (src != TIME_SRC_SYS) {
        /* 仅接受 1970-01-01 ~ 2100-01-01 的秒级时间戳，越界则回退系统时间 */
        if (ts >= 1 && ts <= 4102444800LL) {
            t = (time_t)ts;
        } else {
            src = TIME_SRC_SYS;
            t = time(NULL);
        }
    }

    struct tm tmv;
#if defined(_WIN32)
    if (localtime_s(&tmv, &t) != 0) { strcpy(out, "??:??:??"); return TIME_SRC_SYS; }
#else
    if (localtime_r(&t, &tmv) == NULL) { strcpy(out, "??:??:??"); return TIME_SRC_SYS; }
#endif
    strftime(out, 16, "%H:%M:%S", &tmv);
    return src;
}

void msg_fmt_time(int64_t ts, char out[16]) {
    msg_fmt_time_src(ts, out);
}

/* 从消息中提取时间戳：event_time/send_time > common.create_time */
static int64_t get_msg_time(const uint8_t *msg, size_t len) {
    uint64_t v;
    if (pb_get_varint(msg, len, 15, &v) && v > 0) return (int64_t)v; /* event_time */
    if (pb_get_varint(msg, len, 33, &v) && v > 0) return (int64_t)v; /* send_time */
    const uint8_t *common; size_t cl;
    if (pb_get_message(msg, len, 1, &common, &cl)) {
        if (pb_get_varint(common, cl, 4, &v) && v > 0) return (int64_t)v; /* create_time */
    }
    return 0;
}

static void get_nick(const uint8_t *user, size_t ulen, char *out, size_t outlen) {
    const uint8_t *nick; size_t nl;
    if (pb_get_bytes(user, ulen, 3, &nick, &nl)) {
        pb_copy_str(nick, nl, out, outlen);
    } else {
        out[0] = '\0';
    }
}

static void get_user(const uint8_t *msg, size_t len, uint32_t field, char *out, size_t outlen) {
    const uint8_t *u; size_t ul;
    if (pb_get_message(msg, len, field, &u, &ul)) get_nick(u, ul, out, outlen);
    else out[0] = '\0';
}

static void emit(msg_handlers_t *h, const char *type, int64_t ts, const char *text) {
    if (h && h->on_message) {
        char tsbuf[16];
        msg_fmt_time(ts, tsbuf);
        h->on_message(h->ud, type, tsbuf, text);
    }
}

/* ---- 各类消息 ---- */
static void parse_chat(msg_handlers_t *h, const uint8_t *m, size_t len) {
    char user[256], content[2048], buf[2600];
    get_user(m, len, 2, user, sizeof(user));
    const uint8_t *c; size_t cl;
    if (!pb_get_bytes(m, len, 3, &c, &cl)) return;
    pb_copy_str(c, cl, content, sizeof(content));
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    snprintf(buf, sizeof(buf), "[%s] 【弹幕】 %s: %s", tb, user, content);
    emit(h, "chat", ts, buf);
}

static void parse_gift(msg_handlers_t *h, const uint8_t *m, size_t len) {
    char user[256];
    get_user(m, len, 7, user, sizeof(user));
    const uint8_t *g; size_t gl;
    if (!pb_get_message(m, len, 15, &g, &gl)) return;
    const uint8_t *gn; size_t gnl;
    char gname[256];
    if (pb_get_bytes(g, gl, 16, &gn, &gnl)) pb_copy_str(gn, gnl, gname, sizeof(gname));
    else gname[0] = '\0';
    uint64_t cnt = 0;
    pb_get_varint(m, len, 6, &cnt);
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[600];
    snprintf(buf, sizeof(buf), "[%s] 【礼物】 %s 送出 %s x%llu", tb, user, gname, (unsigned long long)cnt);
    emit(h, "gift", ts, buf);
}

static void parse_like(msg_handlers_t *h, const uint8_t *m, size_t len) {
    char user[256];
    get_user(m, len, 5, user, sizeof(user));
    uint64_t cnt = 0;
    pb_get_varint(m, len, 2, &cnt);
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[600];
    snprintf(buf, sizeof(buf), "[%s] 【点赞】 %s 点了%llu个赞", tb, user, (unsigned long long)cnt);
    emit(h, "like", ts, buf);
}

static void parse_member(msg_handlers_t *h, const uint8_t *m, size_t len) {
    char user[256];
    get_user(m, len, 2, user, sizeof(user));
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[600];
    snprintf(buf, sizeof(buf), "[%s] 【进场】 %s 进入了直播间", tb, user);
    emit(h, "member", ts, buf);
}

static void parse_social(msg_handlers_t *h, const uint8_t *m, size_t len) {
    char user[256];
    get_user(m, len, 2, user, sizeof(user));
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[600];
    snprintf(buf, sizeof(buf), "[%s] 【关注】 %s 关注了主播", tb, user);
    emit(h, "social", ts, buf);
}

static void parse_room_user_seq(msg_handlers_t *h, const uint8_t *m, size_t len) {
    uint64_t current = 0;
    pb_get_varint(m, len, 3, &current);
    const uint8_t *tp; size_t tpl;
    char total[64] = "0";
    if (pb_get_bytes(m, len, 11, &tp, &tpl)) pb_copy_str(tp, tpl, total, sizeof(total));
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[600];
    snprintf(buf, sizeof(buf), "[%s] 【统计】 当前观看: %llu, 累计观看: %s", tb, (unsigned long long)current, total);
    emit(h, "stats", ts, buf);
}

static void parse_fansclub(msg_handlers_t *h, const uint8_t *m, size_t len) {
    const uint8_t *c; size_t cl;
    char content[1024];
    if (!pb_get_bytes(m, len, 3, &c, &cl)) return;
    pb_copy_str(c, cl, content, sizeof(content));
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[1300];
    snprintf(buf, sizeof(buf), "[%s] 【粉丝团】 %s", tb, content);
    emit(h, "fansclub", ts, buf);
}

static void parse_emoji(msg_handlers_t *h, const uint8_t *m, size_t len) {
    char user[256];
    get_user(m, len, 2, user, sizeof(user));
    const uint8_t *c; size_t cl;
    char content[1024];
    if (pb_get_bytes(m, len, 5, &c, &cl)) pb_copy_str(c, cl, content, sizeof(content));
    else content[0] = '\0';
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[1300];
    snprintf(buf, sizeof(buf), "[%s] 【表情】 %s: %s", tb, user, content);
    emit(h, "emoji", ts, buf);
}

static void parse_room(msg_handlers_t *h, const uint8_t *m, size_t len) {
    const uint8_t *common; size_t cl;
    uint64_t rid = 0;
    if (pb_get_message(m, len, 1, &common, &cl)) pb_get_varint(common, cl, 3, &rid);
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[256];
    snprintf(buf, sizeof(buf), "[%s] 【房间】 id:%llu", tb, (unsigned long long)rid);
    emit(h, "room", ts, buf);
}

static void parse_room_stats(msg_handlers_t *h, const uint8_t *m, size_t len) {
    const uint8_t *d; size_t dl;
    char display[1024];
    if (!pb_get_bytes(m, len, 4, &d, &dl)) return;
    pb_copy_str(d, dl, display, sizeof(display));
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[1300];
    snprintf(buf, sizeof(buf), "[%s] 【直播】 %s", tb, display);
    emit(h, "roomstats", ts, buf);
}

static void parse_control(msg_handlers_t *h, const uint8_t *m, size_t len) {
    uint64_t status = 0;
    pb_get_varint(m, len, 2, &status);
    if (status != 3) return; /* 仅 status==3 表示下播 */
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[256];
    snprintf(buf, sizeof(buf), "[%s] 【状态】 直播间已结束", tb);
    emit(h, "control", ts, buf);
    h->should_stop = 1;
}

static void parse_adaptation(msg_handlers_t *h, const uint8_t *m, size_t len) {
    uint64_t type = 0;
    pb_get_varint(m, len, 2, &type);
    int64_t ts = get_msg_time(m, len);
    char tb[16]; msg_fmt_time(ts, tb);
    char buf[256];
    snprintf(buf, sizeof(buf), "[%s] 【流适配】 adaptation: %llu", tb, (unsigned long long)type);
    emit(h, "adaptation", ts, buf);
}

int messages_parse(const uint8_t *resp, size_t resp_len, msg_handlers_t *h,
                   int *need_ack, const uint8_t **internal_ext, size_t *internal_ext_len) {
    *need_ack = 0;
    *internal_ext = NULL;
    *internal_ext_len = 0;

    /* 遍历 Response.messages_list (field 1) */
    const uint8_t *p = resp, *end = resp + resp_len;
    uint32_t f; int w; uint64_t v; const uint8_t *b; size_t bl;

    /* 先取 need_ack(9) 与 internal_ext(5) */
    if (pb_get_varint(resp, resp_len, 9, &v)) *need_ack = (v != 0);
    if (pb_get_bytes(resp, resp_len, 5, internal_ext, internal_ext_len)) { /* ok */ }

    int r;
    while ((r = pb_read_field(&p, end, &f, &w, &v, &b, &bl)) > 0) {
        if (f != 1 || w != 2) continue; /* 只关心 messages_list */
        /* 解析单条 Message：method(1,str) + payload(2,bytes) */
        const uint8_t *method; size_t mlen;
        const uint8_t *payload; size_t plen;
        if (!pb_get_bytes(b, bl, 1, &method, &mlen)) continue;
        if (!pb_get_bytes(b, bl, 2, &payload, &plen)) continue;

        char m[64];
        pb_copy_str(method, mlen, m, sizeof(m));

        if (strcmp(m, "WebcastChatMessage") == 0) parse_chat(h, payload, plen);
        else if (strcmp(m, "WebcastGiftMessage") == 0) parse_gift(h, payload, plen);
        else if (strcmp(m, "WebcastLikeMessage") == 0) parse_like(h, payload, plen);
        else if (strcmp(m, "WebcastMemberMessage") == 0) parse_member(h, payload, plen);
        else if (strcmp(m, "WebcastSocialMessage") == 0) parse_social(h, payload, plen);
        else if (strcmp(m, "WebcastRoomUserSeqMessage") == 0) parse_room_user_seq(h, payload, plen);
        else if (strcmp(m, "WebcastFansclubMessage") == 0) parse_fansclub(h, payload, plen);
        else if (strcmp(m, "WebcastEmojiChatMessage") == 0) parse_emoji(h, payload, plen);
        else if (strcmp(m, "WebcastRoomMessage") == 0) parse_room(h, payload, plen);
        else if (strcmp(m, "WebcastRoomStatsMessage") == 0) parse_room_stats(h, payload, plen);
        else if (strcmp(m, "WebcastControlMessage") == 0) parse_control(h, payload, plen);
        else if (strcmp(m, "WebcastRoomStreamAdaptationMessage") == 0) parse_adaptation(h, payload, plen);
        /* 其它类型静默忽略 */
    }
    return 0;
}
