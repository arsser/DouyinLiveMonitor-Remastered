/* 最小 JSON 解析/序列化：仅支持本项目 config.json 的结构
 * {"anchors":{"rid":"name",...},"cookie":"..."}
 */
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void anchor_list_init(anchor_list_t *l) {
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void anchor_list_free(anchor_list_t *l) {
    for (size_t i = 0; i < l->count; i++) {
        free(l->items[i].rid);
        free(l->items[i].name);
    }
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

int anchor_list_set(anchor_list_t *l, const char *rid, const char *name) {
    for (size_t i = 0; i < l->count; i++) {
        if (strcmp(l->items[i].rid, rid) == 0) {
            char *n = strdup(name);
            if (!n) return -1;
            free(l->items[i].name);
            l->items[i].name = n;
            return 0;
        }
    }
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 8;
        anchor_t *ni = (anchor_t *)realloc(l->items, nc * sizeof(anchor_t));
        if (!ni) return -1;
        l->items = ni;
        l->cap = nc;
    }
    char *r = strdup(rid);
    char *n = strdup(name);
    if (!r || !n) { free(r); free(n); return -1; }
    l->items[l->count].rid = r;
    l->items[l->count].name = n;
    l->count++;
    return 0;
}

const char *anchor_list_name(const anchor_list_t *l, const char *rid) {
    for (size_t i = 0; i < l->count; i++) {
        if (strcmp(l->items[i].rid, rid) == 0) return l->items[i].name;
    }
    return NULL;
}

/* ---------------- 解析器 ---------------- */
typedef struct {
    const char *p;
    const char *end;
} jp_t;

static void jp_ws(jp_t *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int jp_char(jp_t *j, char c) {
    jp_ws(j);
    if (j->p < j->end && *j->p == c) { j->p++; return 1; }
    return 0;
}

static int jp_hex4(jp_t *j, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        if (j->p >= j->end) return -1;
        char c = *j->p++;
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

/* 解析 JSON 字符串（含转义），返回 malloc 字符串 */
static char *jp_string(jp_t *j) {
    if (!jp_char(j, '"')) return NULL;
    size_t cap = 32, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->end) { free(buf); return NULL; }
            char e = *j->p++;
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned u;
                    if (jp_hex4(j, &u) != 0) { free(buf); return NULL; }
                    /* 简化：仅处理 BMP 与常见代理对（本项目昵称可能含 emoji） */
                    if (u >= 0xD800 && u <= 0xDBFF && j->p + 1 < j->end && j->p[0] == '\\' && j->p[1] == 'u') {
                        j->p += 2;
                        unsigned lo;
                        if (jp_hex4(j, &lo) != 0) { free(buf); return NULL; }
                        unsigned cp = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                        /* UTF-8 编码 */
                        char utf8[4]; int n = 0;
                        if (cp < 0x80) { utf8[n++] = (char)cp; }
                        else if (cp < 0x800) { utf8[n++] = (char)(0xC0 | (cp >> 6)); utf8[n++] = (char)(0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) { utf8[n++] = (char)(0xE0 | (cp >> 12)); utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (cp & 0x3F)); }
                        else { utf8[n++] = (char)(0xF0 | (cp >> 18)); utf8[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (cp & 0x3F)); }
                        for (int k = 0; k < n; k++) {
                            if (len + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
                            buf[len++] = utf8[k];
                        }
                        continue;
                    } else {
                        /* BMP 内直接 UTF-8 编码 */
                        char utf8[3]; int n = 0;
                        if (u < 0x80) utf8[n++] = (char)u;
                        else if (u < 0x800) { utf8[n++] = (char)(0xC0 | (u >> 6)); utf8[n++] = (char)(0x80 | (u & 0x3F)); }
                        else { utf8[n++] = (char)(0xE0 | (u >> 12)); utf8[n++] = (char)(0x80 | ((u >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (u & 0x3F)); }
                        for (int k = 0; k < n; k++) {
                            if (len + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
                            buf[len++] = utf8[k];
                        }
                        continue;
                    }
                }
                default: { free(buf); return NULL; }
            }
        }
        if (len + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
        buf[len++] = c;
    }
    if (!jp_char(j, '"')) { free(buf); return NULL; }
    if (len + 1 >= cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
    buf[len] = '\0';
    return buf;
}

static void jp_skip_value(jp_t *j) {
    jp_ws(j);
    if (j->p >= j->end) return;
    char c = *j->p;
    if (c == '"') { char *s = jp_string(j); free(s); return; }
    if (c == '{') {
        j->p++;
        while (!jp_char(j, '}')) {
            char *k = jp_string(j); free(k);
            if (!jp_char(j, ':')) return;
            jp_skip_value(j);
            if (!jp_char(j, ',')) { jp_char(j, '}'); return; }
        }
        return;
    }
    if (c == '[') {
        j->p++;
        while (!jp_char(j, ']')) {
            jp_skip_value(j);
            if (!jp_char(j, ',')) { jp_char(j, ']'); return; }
        }
        return;
    }
    /* 数字 / true / false / null */
    while (j->p < j->end && (*j->p != ',' && *j->p != '}' && *j->p != ']' && *j->p != ' ' && *j->p != '\t' && *j->p != '\n' && *j->p != '\r')) j->p++;
}

int json_load_config(const char *text, anchor_list_t *anchors, char **cookie) {
    if (cookie) *cookie = NULL;
    jp_t j = { text, text + strlen(text) };
    jp_ws(&j);
    if (!jp_char(&j, '{')) return -1;

    while (1) {
        jp_ws(&j);
        if (j.p >= j.end) break;
        if (*j.p == '}') { j.p++; break; }
        char *key = jp_string(&j);
        if (!key) return -1;
        if (!jp_char(&j, ':')) { free(key); return -1; }
        if (strcmp(key, "anchors") == 0 && anchors) {
            jp_ws(&j);
            if (!jp_char(&j, '{')) { free(key); return -1; }
            while (1) {
                jp_ws(&j);
                if (j.p >= j.end) break;
                if (*j.p == '}') { j.p++; break; }
                char *rid = jp_string(&j);
                if (!rid) { free(key); return -1; }
                if (!jp_char(&j, ':')) { free(rid); free(key); return -1; }
                char *name = jp_string(&j);
                if (!name) { free(rid); free(key); return -1; }
                anchor_list_set(anchors, rid, name);
                free(rid); free(name);
                if (!jp_char(&j, ',')) { jp_char(&j, '}'); break; }
            }
        } else if (strcmp(key, "cookie") == 0 && cookie) {
            *cookie = jp_string(&j);
            if (!*cookie) { free(key); return -1; }
        } else {
            jp_skip_value(&j);
        }
        free(key);
        if (!jp_char(&j, ',')) { jp_char(&j, '}'); break; }
    }
    if (cookie && !*cookie) *cookie = strdup("");
    return 0;
}

/* 字符串构建器：追加字符串/单字节，自动扩容 */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} sb_t;

static int sb_reserve(sb_t *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 0;
    size_t nc = b->cap;
    while (nc < b->len + extra + 1) nc *= 2;
    char *nb = (char *)realloc(b->buf, nc);
    if (!nb) return -1;
    b->buf = nb;
    b->cap = nc;
    return 0;
}

static int sb_add(sb_t *b, const char *s) {
    size_t n = strlen(s);
    if (sb_reserve(b, n) != 0) return -1;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    return 0;
}

static int sb_addc(sb_t *b, char c) {
    if (sb_reserve(b, 1) != 0) return -1;
    b->buf[b->len++] = c;
    return 0;
}

char *json_save_config(const anchor_list_t *anchors, const char *cookie) {
    const char *ck = cookie ? cookie : "";
    sb_t b = { (char *)malloc(4096), 4096, 0 };
    if (!b.buf) return NULL;

    sb_add(&b, "{\n  \"anchors\": {\n");
    for (size_t i = 0; i < anchors->count; i++) {
        sb_add(&b, "    \"");
        sb_add(&b, anchors->items[i].rid);
        sb_add(&b, "\": \"");
        for (const char *s = anchors->items[i].name; *s; s++) {
            if (*s == '"' || *s == '\\') sb_addc(&b, '\\');
            sb_addc(&b, *s);
        }
        sb_add(&b, (i + 1 < anchors->count) ? "\",\n" : "\"\n");
    }
    sb_add(&b, "  },\n  \"cookie\": \"");
    for (const char *s = ck; *s; s++) {
        if (*s == '"' || *s == '\\') sb_addc(&b, '\\');
        sb_addc(&b, *s);
    }
    sb_add(&b, "\"\n}\n");
    b.buf[b.len] = '\0';
    return b.buf;
}
