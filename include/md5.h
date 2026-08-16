#ifndef DLM_MD5_H
#define DLM_MD5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[4];   /* A, B, C, D */
    uint64_t count;      /* 已处理字节数 */
    uint8_t  buffer[64]; /* 输入缓冲 */
} md5_ctx_t;

void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const uint8_t *data, size_t len);
void md5_final(md5_ctx_t *ctx, uint8_t digest[16]);

/* 便捷接口 */
void md5_buf(const uint8_t *data, size_t len, uint8_t digest[16]);
void md5_str(const char *str, uint8_t digest[16]);              /* strlen(str) */
void md5_hex(const uint8_t digest[16], char out[33]);           /* 32 位小写 hex + NUL */

#ifdef __cplusplus
}
#endif

#endif /* DLM_MD5_H */
