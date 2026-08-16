/* 通用 JSON DOM 解析器（递归下降，支持 UTF-8/emoji/代理对） */
#include "json_dom.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
} jp_t;

static void ws(jp_t *j) {
    while (j->p < j->end && isspace((unsigned char)*j->p)) j->p++;
}

static int hex4(jp_t *j, unsigned *out) {
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

/* 追加单个字节（原始 UTF-8 字节直接拷贝） */
static int append_byte(char **buf, size_t *cap, size_t *len, char c) {
    if (*len + 1 >= *cap) {
        *cap *= 2;
        char *nb = (char *)realloc(*buf, *cap);
        if (!nb) return -1;
        *buf = nb;
    }
    (*buf)[(*len)++] = c;
    return 0;
}

/* 把 Unicode 码点编码为 UTF-8，追加到 buf */
static int append_utf8(char **buf, size_t *cap, size_t *len, unsigned cp) {
    char u[4]; int n = 0;
    if (cp < 0x80) u[n++] = (char)cp;
    else if (cp < 0x800) { u[n++] = (char)(0xC0 | (cp >> 6)); u[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { u[n++] = (char)(0xE0 | (cp >> 12)); u[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[n++] = (char)(0x80 | (cp & 0x3F)); }
    else { u[n++] = (char)(0xF0 | (cp >> 18)); u[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); u[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[n++] = (char)(0x80 | (cp & 0x3F)); }
    for (int i = 0; i < n; i++) {
        if (*len + 1 >= *cap) {
            *cap *= 2;
            char *nb = (char *)realloc(*buf, *cap);
            if (!nb) return -1;
            *buf = nb;
        }
        (*buf)[(*len)++] = u[i];
    }
    return 0;
}

static char *parse_string(jp_t *j) {
    if (j->p >= j->end || *j->p != '"') return NULL;
    j->p++;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    while (j->p < j->end && *j->p != '"') {
        unsigned char c = (unsigned char)*j->p++;
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
                    if (hex4(j, &u) != 0) { free(buf); return NULL; }
                    if (u >= 0xD800 && u <= 0xDBFF && j->p + 1 < j->end && j->p[0] == '\\' && j->p[1] == 'u') {
                        j->p += 2;
                        unsigned lo;
                        if (hex4(j, &lo) != 0) { free(buf); return NULL; }
                        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    if (append_utf8(&buf, &cap, &len, u) != 0) { free(buf); return NULL; }
                    continue;
                }
                default: { free(buf); return NULL; }
            }
        }
        /* 普通字节（含 UTF-8 多字节序列）直接拷贝 */
        if (append_byte(&buf, &cap, &len, (char)c) != 0) { free(buf); return NULL; }
    }
    if (j->p >= j->end || *j->p != '"') { free(buf); return NULL; }
    j->p++;
    if (len + 1 >= cap) { cap = len + 1; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
    buf[len] = '\0';
    return buf;
}

static jval_t *parse_value(jp_t *j);

static jval_t *parse_object(jp_t *j) {
    /* 已消费 '{' */
    jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
    if (!v) return NULL;
    v->type = J_OBJ;
    ws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return v; }
    while (1) {
        ws(j);
        char *key = parse_string(j);
        if (!key) { json_free(v); return NULL; }
        ws(j);
        if (j->p >= j->end || *j->p != ':') { free(key); json_free(v); return NULL; }
        j->p++;
        ws(j);
        jval_t *val = parse_value(j);
        if (!val) { free(key); json_free(v); return NULL; }
        v->keys = (char **)realloc(v->keys, (v->kcount + 1) * sizeof(char *));
        v->vals = (jval_t **)realloc(v->vals, (v->kcount + 1) * sizeof(jval_t *));
        v->keys[v->kcount] = key;
        v->vals[v->kcount] = val;
        v->kcount++;
        ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; return v; }
        json_free(v);
        return NULL;
    }
}

static jval_t *parse_array(jp_t *j) {
    /* 已消费 '[' */
    jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
    if (!v) return NULL;
    v->type = J_ARR;
    ws(j);
    if (j->p < j->end && *j->p == ']') { j->p++; return v; }
    while (1) {
        ws(j);
        jval_t *item = parse_value(j);
        if (!item) { json_free(v); return NULL; }
        v->items = (jval_t **)realloc(v->items, (v->count + 1) * sizeof(jval_t *));
        v->items[v->count++] = item;
        ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == ']') { j->p++; return v; }
        json_free(v);
        return NULL;
    }
}

static jval_t *parse_number(jp_t *j) {
    const char *start = j->p;
    if (j->p < j->end && *j->p == '-') j->p++;
    while (j->p < j->end && isdigit((unsigned char)*j->p)) j->p++;
    if (j->p < j->end && *j->p == '.') {
        j->p++;
        while (j->p < j->end && isdigit((unsigned char)*j->p)) j->p++;
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
        j->p++;
        if (j->p < j->end && (*j->p == '+' || *j->p == '-')) j->p++;
        while (j->p < j->end && isdigit((unsigned char)*j->p)) j->p++;
    }
    char tmp[128];
    size_t n = (size_t)(j->p - start);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
    if (!v) return NULL;
    v->type = J_NUM;
    v->n = strtod(tmp, NULL);
    return v;
}

static jval_t *parse_value(jp_t *j) {
    ws(j);
    if (j->p >= j->end) return NULL;
    char c = *j->p;
    if (c == '"') {
        jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
        if (!v) return NULL;
        v->type = J_STR;
        v->s = parse_string(j);
        if (!v->s) { free(v); return NULL; }
        return v;
    }
    if (c == '{') { j->p++; return parse_object(j); }
    if (c == '[') { j->p++; return parse_array(j); }
    if (c == 't' && j->end - j->p >= 4 && strncmp(j->p, "true", 4) == 0) {
        j->p += 4;
        jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
        if (!v) return NULL;
        v->type = J_BOOL; v->b = 1;
        return v;
    }
    if (c == 'f' && j->end - j->p >= 5 && strncmp(j->p, "false", 5) == 0) {
        j->p += 5;
        jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
        if (!v) return NULL;
        v->type = J_BOOL; v->b = 0;
        return v;
    }
    if (c == 'n' && j->end - j->p >= 4 && strncmp(j->p, "null", 4) == 0) {
        j->p += 4;
        jval_t *v = (jval_t *)calloc(1, sizeof(jval_t));
        if (!v) return NULL;
        v->type = J_NULL;
        return v;
    }
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(j);
    return NULL;
}

jval_t *json_parse(const char *text) {
    if (!text) return NULL;
    jp_t j = { text, text + strlen(text) };
    jval_t *v = parse_value(&j);
    if (!v) return NULL;
    ws(&j);
    if (j.p != j.end) { json_free(v); return NULL; } /* 末尾有多余内容 */
    return v;
}

void json_free(jval_t *v) {
    if (!v) return;
    if (v->type == J_STR) free(v->s);
    else if (v->type == J_ARR) {
        for (size_t i = 0; i < v->count; i++) json_free(v->items[i]);
        free(v->items);
    } else if (v->type == J_OBJ) {
        for (size_t i = 0; i < v->kcount; i++) { free(v->keys[i]); json_free(v->vals[i]); }
        free(v->keys);
        free(v->vals);
    }
    free(v);
}

jval_t *json_get(const jval_t *obj, const char *key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (size_t i = 0; i < obj->kcount; i++) {
        if (strcmp(obj->keys[i], key) == 0) return obj->vals[i];
    }
    return NULL;
}

jval_t *json_at(const jval_t *arr, size_t i) {
    if (!arr || arr->type != J_ARR || i >= arr->count) return NULL;
    return arr->items[i];
}

jtype_t json_type(const jval_t *v) { return v ? v->type : J_NULL; }
const char *json_str(const jval_t *v) { return (v && v->type == J_STR) ? v->s : NULL; }
double json_num(const jval_t *v) { return (v && v->type == J_NUM) ? v->n : 0.0; }
int json_int(const jval_t *v) { return (v && v->type == J_NUM) ? (int)v->n : 0; }
int json_bool(const jval_t *v) { return (v && v->type == J_BOOL) ? v->b : 0; }
size_t json_count(const jval_t *v) {
    if (!v) return 0;
    return v->type == J_ARR ? v->count : (v->type == J_OBJ ? v->kcount : 0);
}
