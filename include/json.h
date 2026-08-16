#ifndef DLM_JSON_H
#define DLM_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 主播项：rid -> 昵称 */
typedef struct {
    char *rid;
    char *name;
} anchor_t;

typedef struct {
    anchor_t *items;
    size_t count;
    size_t cap;
} anchor_list_t;

void anchor_list_init(anchor_list_t *l);
void anchor_list_free(anchor_list_t *l);
/* 添加/覆盖主播，返回 0=成功，-1=内存不足 */
int anchor_list_set(anchor_list_t *l, const char *rid, const char *name);
const char *anchor_list_name(const anchor_list_t *l, const char *rid);

/*
 * 解析 config.json 文本。
 * anchors 可选（可传 NULL）；cookie 返回 malloc 字符串（可传 NULL），无 cookie 时为 ""。
 * 返回 0=成功，-1=解析失败。
 */
int json_load_config(const char *text, anchor_list_t *anchors, char **cookie);

/* 序列化 config.json 文本（malloc 返回，调用方 free） */
char *json_save_config(const anchor_list_t *anchors, const char *cookie);

#ifdef __cplusplus
}
#endif

#endif /* DLM_JSON_H */
