#ifndef DLM_XBOGUS_H
#define DLM_XBOGUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 生成抖音 X-Bogus 签名（纯 C 逆向移植自 sign.js 的 byted_acrawler 1.0.0.53）。
 *
 * 输入 md5hex：32 位小写十六进制（liveMan 中为 13 参数逗号串的 MD5）。
 * 输出 out：16 字符签名（需提供 >=17 字节缓冲区，含结尾 NUL）。
 */
void xbogus_generate(const char *md5hex, char out[17]);

/*
 * 纯函数版本，供测试/对拍：三个随机量由调用方给定。
 *   header_bit: 0 或 1（对应签名 header 的 bit4）
 *   byte8:      缓冲第 9 字节（0..255）
 *   rnd:        RC4 密钥字节（0..255）
 */
void xbogus_compute(const char *md5hex, int header_bit, uint8_t byte8, uint8_t rnd, char out[17]);

#ifdef __cplusplus
}
#endif

#endif /* DLM_XBOGUS_H */
