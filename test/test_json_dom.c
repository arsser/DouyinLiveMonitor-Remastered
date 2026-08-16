/* JSON DOM 解析器单元测试 */
#include <stdio.h>
#include <string.h>
#include "json_dom.h"

static int failures = 0;
static void chk(int ok, const char *label) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) failures++;
}

int main(void) {
    /* 模拟 enter API 响应片段 */
    const char *json =
        "{\"data\":{"
        "\"user\":{\"nickname\":\"雾子😀\",\"id\":123},"
        "\"data\":[{\"status\":2,\"title\":\"标题\",\"user_count_str\":\"1.2万\","
        "\"stream_url\":{\"flv_pull_url\":{\"FULL_HD1\":\"http://a/b.flv\",\"HD1\":\"http://c/d.flv\"},"
        "\"live_core_sdk_data\":{\"pull_data\":{\"stream_data\":\"{\\\"data\\\":{\\\"origin\\\":{\\\"main\\\":{\\\"flv\\\":\\\"http://o.flv\\\"}},\\\"ao\\\":{\\\"main\\\":{\\\"flv\\\":\\\"http://a.flv\\\"}}}}\"}}}"
        "}]"
        "},\"status_code\":0}";

    jval_t *root = json_parse(json);
    chk(root != NULL, "解析成功");

    jval_t *data = json_get(root, "data");
    chk(data != NULL, "取 data");

    jval_t *user = json_get(data, "user");
    const char *nick = json_str(json_get(user, "nickname"));
    chk(user && nick && strcmp(nick, "雾子😀") == 0, "取 user.nickname（含 emoji）");

    jval_t *room = json_at(json_get(data, "data"), 0);
    chk(room && json_int(json_get(room, "status")) == 2, "取 data.data[0].status");

    const char *uc = json_str(json_get(room, "user_count_str"));
    chk(uc && strcmp(uc, "1.2万") == 0, "取 user_count_str");

    jval_t *flv = json_get(json_get(room, "stream_url"), "flv_pull_url");
    const char *h1 = json_str(json_get(flv, "FULL_HD1"));
    chk(h1 && strcmp(h1, "http://a/b.flv") == 0, "取 flv_pull_url.FULL_HD1");

    /* 嵌套 JSON 字符串再解析 */
    jval_t *sdk = json_get(json_get(room, "stream_url"), "live_core_sdk_data");
    const char *sdata = json_str(json_get(json_get(sdk, "pull_data"), "stream_data"));
    chk(sdata != NULL, "取 stream_data 字符串");
    jval_t *sd = json_parse(sdata);
    chk(sd != NULL, "内嵌 JSON 再解析成功");
    const char *oflv = json_str(json_get(json_get(json_get(sd, "data"), "origin"), "main") ? NULL : NULL);
    /* 用路径逐级取 */
    jval_t *sddata = json_get(sd, "data");
    jval_t *origin = json_get(sddata, "origin");
    jval_t *omain = json_get(origin, "main");
    const char *o = json_str(json_get(omain, "flv"));
    chk(o && strcmp(o, "http://o.flv") == 0, "取 origin.main.flv");
    (void)oflv;
    json_free(sd);

    /* 数组/布尔/null */
    const char *json2 = "[1,2,3,\"a\",true,false,null]";
    jval_t *arr = json_parse(json2);
    chk(arr && json_count(arr) == 7, "数组长度");
    chk(json_int(json_at(arr, 2)) == 3, "数组元素");
    chk(json_bool(json_at(arr, 4)) == 1 && json_bool(json_at(arr, 5)) == 0, "布尔");
    chk(json_type(json_at(arr, 6)) == J_NULL, "null");
    json_free(arr);

    json_free(root);

    if (failures == 0) printf("\n全部通过 ✅\n");
    else printf("\n%d 项失败 ❌\n", failures);
    return failures ? 1 : 0;
}
