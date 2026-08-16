#ifndef DLM_ENTER_H
#define DLM_ENTER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 直播流条目：清晰度 -> 直链 */
typedef struct {
    char name[64];
    char url[1536];
} stream_item_t;

/* 直播间信息（enter API） */
typedef struct {
    char nickname[256];
    int is_live;             /* 1 = 直播中 */
    char title[512];
    char user_count[64];
    stream_item_t streams[8];
    size_t stream_count;
} room_info_t;

/* 通过 enter API 获取直播间信息。返回 0=成功，-1=失败。 */
int enter_fetch(const char *rid, const char *cookie, room_info_t *info);

/* 使用指定的匿名 ttwid cookie（"ttwid=..."）调用 enter API。返回 0=成功，-1=失败。 */
int enter_fetch_ttwid(const char *rid, const char *ttwid_cookie, room_info_t *info);

/* 获取一个新的匿名 ttwid cookie（"ttwid=..."）到 buf。返回 0=成功，-1=失败。 */
int fetch_anon_ttwid(char *buf, size_t sz);

/* 按清晰度优先级（原画>蓝光>超清>高清>标清>仅音频）取最佳流 URL，无则返回 NULL */
const char *room_best_stream(const room_info_t *info);

/* 按清晰度名取流 URL（如 "origin"/"FULL_HD1"/"HD1"/"SD2"/"SD1"/"audio"），无则 NULL */
const char *room_stream(const room_info_t *info, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DLM_ENTER_H */
