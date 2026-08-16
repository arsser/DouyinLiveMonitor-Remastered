#ifndef DLM_FETCHER_H
#define DLM_FETCHER_H

#include "messages.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char live_id[64];      /* 直播间短 ID（rid） */
    char cookie[16384];    /* 可选完整 Cookie，空串表示无 */
    message_fn on_message; /* 每条消息回调 */
    void *ud;
    void (*on_open)(void *ud);  /* 可选：WebSocket 建立后回调 */
    volatile int *stop;         /* 可选：置 1 请求停止接收 */
    void **ws_out;              /* 可选：fetcher 填入 WebSocket 句柄，供外部中断 */
} fetcher_config_t;

/*
 * 连接直播间并进入接收循环（阻塞），直到连接关闭或直播结束。
 * 返回 0=正常结束，-1=错误。
 */
int fetcher_run(const fetcher_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* DLM_FETCHER_H */
