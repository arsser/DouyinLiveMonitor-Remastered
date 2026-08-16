#ifndef DLM_HTTP_H
#define DLM_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *body;         /* malloc，响应体字节（按 UTF-8 原样存储） */
    size_t body_len;
    int status_code;
    char *set_cookie;   /* malloc，Set-Cookie 响应头（可能为 NULL） */
} http_response_t;

/*
 * 执行一次 HTTP GET（https）。返回 0=成功，-1=失败。
 * cookie_header：可选，作为 Cookie 请求头（可为 NULL）。
 * referer：可选（可为 NULL）。
 * 成功时 resp 需调用 http_response_free 释放。
 */
int http_get(const char *url, const char *cookie_header, const char *referer,
             http_response_t *resp);

void http_response_free(http_response_t *resp);

/* 从 Set-Cookie 头字符串中提取指定 cookie 的值，返回 malloc 字符串（未找到返回 NULL） */
char *http_cookie_value(const char *set_cookie, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DLM_HTTP_H */
