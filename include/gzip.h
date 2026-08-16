#ifndef DLM_GZIP_H
#define DLM_GZIP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 解压 gzip 数据到 malloc 分配的缓冲区。返回 0=成功（*out 由调用方 free），-1=失败 */
int gzip_decompress(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* DLM_GZIP_H */
