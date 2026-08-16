#ifndef DLM_JSON_DOM_H
#define DLM_JSON_DOM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 通用 JSON DOM 解析器（用于 enter API 等深层嵌套 JSON） */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype_t;

typedef struct jval {
    jtype_t type;
    int b;              /* J_BOOL */
    double n;           /* J_NUM */
    char *s;            /* J_STR（malloc，UTF-8） */
    struct jval **items; size_t count;   /* J_ARR */
    char **keys; struct jval **vals; size_t kcount; /* J_OBJ */
} jval_t;

/* 解析 JSON 文本，返回根节点（失败返回 NULL） */
jval_t *json_parse(const char *text);

/* 递归释放 */
void json_free(jval_t *v);

/* 取对象字段（不存在返回 NULL） */
jval_t *json_get(const jval_t *obj, const char *key);

/* 取数组元素（越界返回 NULL） */
jval_t *json_at(const jval_t *arr, size_t i);

jtype_t json_type(const jval_t *v);
const char *json_str(const jval_t *v);      /* 非字符串返回 NULL */
double json_num(const jval_t *v);           /* 非数字返回 0 */
int json_int(const jval_t *v);              /* 数字转 int */
int json_bool(const jval_t *v);             /* 布尔转 0/1 */
size_t json_count(const jval_t *v);         /* 数组/对象元素数 */

#ifdef __cplusplus
}
#endif

#endif /* DLM_JSON_DOM_H */
