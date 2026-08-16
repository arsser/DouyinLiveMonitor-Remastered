#ifndef DLM_PROTOBUF_H
#define DLM_PROTOBUF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 最小 protobuf wire-format 解码器（仅覆盖本项目需要：varint + length-delimited） */

/* 读取 varint，返回 0=成功，-1=越界 */
int pb_read_varint(const uint8_t **p, const uint8_t *end, uint64_t *out);

/*
 * 读取一个字段。返回 0=已到末尾；>0=成功（此时 *field 为字段号，*wire 为 wire type）。
 * 对 wire_type==0（varint），值写入 *varint_value；
 * 对 wire_type==2（length-delimited），数据指针与长度写入 *bytes 与 *bytes_len。
 */
int pb_read_field(const uint8_t **p, const uint8_t *end,
                  uint32_t *field, int *wire,
                  uint64_t *varint_value,
                  const uint8_t **bytes, size_t *bytes_len);

/* 在子消息中查找指定字段的 varint 值（存在多个取第一个）。返回 1=找到，0=未找到 */
int pb_get_varint(const uint8_t *data, size_t len, uint32_t field, uint64_t *out);

/* 查找指定字段的 length-delimited 数据（string/bytes）。返回 1=找到 */
int pb_get_bytes(const uint8_t *data, size_t len, uint32_t field,
                 const uint8_t **out, size_t *out_len);

/* 查找指定字段的子消息。返回 1=找到 */
int pb_get_message(const uint8_t *data, size_t len, uint32_t field,
                   const uint8_t **out, size_t *out_len);

/* 从 length-delimited 数据中拷贝出以 NUL 结尾的字符串（截断到 buf-1 字节） */
void pb_copy_str(const uint8_t *data, size_t len, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* DLM_PROTOBUF_H */
