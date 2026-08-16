#ifndef DLM_MESSAGES_H
#define DLM_MESSAGES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 消息回调：type（chat/gift/like/member/...）、time_str（HH:MM:SS）、text（完整显示文本） */
typedef void (*message_fn)(void *ud, const char *type, const char *time_str, const char *text);

typedef struct {
    message_fn on_message;
    void *ud;
    int should_stop; /* 由解析器设置：收到下播(control status==3)时置 1 */
} msg_handlers_t;

/*
 * 解析已 gzip 解压的 Response protobuf。
 * 对每条消息调用 h->on_message。
 * 输出 need_ack（服务端要求 ACK）与 internal_ext（ACK 应答体，仅 need_ack 时有效）。
 * 返回 0=成功，-1=解析失败。
 */
int messages_parse(const uint8_t *resp, size_t resp_len, msg_handlers_t *h,
                   int *need_ack, const uint8_t **internal_ext, size_t *internal_ext_len);

/* 时间戳来源 */
typedef enum {
    TIME_SRC_SYS = 0, /* 系统时间（兜底） */
    TIME_SRC_SEC = 1, /* 秒级时间戳 */
    TIME_SRC_MS = 2,  /* 毫秒级时间戳（已换算为秒） */
} time_source_t;

/* 将 Unix 时间戳格式化为本地 HH:MM:SS，并返回来源 */
time_source_t msg_fmt_time_src(int64_t ts, char out[16]);

/* 同 msg_fmt_time_src，忽略来源 */
void msg_fmt_time(int64_t ts, char out[16]);

#ifdef __cplusplus
}
#endif

#endif /* DLM_MESSAGES_H */
