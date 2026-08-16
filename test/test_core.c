/* protobuf / json / gzip 单元测试 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protobuf.h"
#include "json.h"
#include "gzip.h"
#include <zlib.h>

static int failures = 0;
static void chk(int ok, const char *label) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) failures++;
}

int main(void) {
    /* ---- protobuf：手写编码 ChatMessage(content=字段3, event_time=字段15) ---- */
    uint8_t buf[64];
    size_t n = 0;
    /* field 3 (content), wire 2, len 5, "hello" */
    buf[n++] = (3 << 3) | 2; buf[n++] = 5;
    memcpy(buf + n, "hello", 5); n += 5;
    /* field 15 (event_time), wire 0, varint 12345 */
    buf[n++] = (15 << 3) | 0;
    uint64_t v = 12345;
    while (v >= 0x80) { buf[n++] = (uint8_t)(v | 0x80); v >>= 7; }
    buf[n++] = (uint8_t)v;

    const uint8_t *b = NULL; size_t bl = 0; uint64_t et = 0;
    int r1 = pb_get_bytes(buf, n, 3, &b, &bl);
    int r2 = pb_get_varint(buf, n, 15, &et);
    char content[32];
    pb_copy_str(b, bl, content, sizeof(content));
    chk(r1 == 1 && bl == 5 && strcmp(content, "hello") == 0, "protobuf bytes 字段提取");
    chk(r2 == 1 && et == 12345, "protobuf varint 字段提取");

    /* ---- json 解析 ---- */
    const char *cfg = "{\"anchors\":{\"123\":\"主播A\",\"456\":\"主播B\"},\"cookie\":\"ttwid=abc\"}";
    anchor_list_t al; anchor_list_init(&al);
    char *cookie = NULL;
    int rc = json_load_config(cfg, &al, &cookie);
    chk(rc == 0 && al.count == 2, "json 解析主播数量");
    chk(al.count == 2 && strcmp(al.items[1].rid, "456") == 0 && strcmp(al.items[1].name, "主播B") == 0, "json 主播内容");
    chk(cookie && strcmp(cookie, "ttwid=abc") == 0, "json cookie");
    char *back = json_save_config(&al, cookie);
    anchor_list_t al2; anchor_list_init(&al2);
    char *cookie2 = NULL;
    int rc2 = json_load_config(back, &al2, &cookie2);
    chk(rc2 == 0 && al2.count == 2 && strcmp(cookie2, "ttwid=abc") == 0, "json 序列化后重新解析");
    free(cookie); free(cookie2); free(back);
    anchor_list_free(&al); anchor_list_free(&al2);

    /* ---- gzip ---- */
    const char *msg = "hello gzip hello gzip hello gzip";
    size_t msg_len = strlen(msg);
    /* 用 zlib 压缩 */
    uLongf clen = compressBound((uLong)msg_len);
    uint8_t *cdata = (uint8_t *)malloc(clen);
    compress2(cdata, &clen, (const Bytef *)msg, (uLong)msg_len, Z_BEST_COMPRESSION);
    /* 但 gzip_decompress 期待 gzip 头，用 gzip 包装压缩 */
    uint8_t *gdata = (uint8_t *)malloc(clen + 64);
    uLongf glen = clen + 64;
    z_stream zs; memset(&zs, 0, sizeof(zs));
    deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    zs.next_in = (Bytef *)msg; zs.avail_in = (uInt)msg_len;
    zs.next_out = gdata; zs.avail_out = (uInt)glen;
    deflate(&zs, Z_FINISH);
    glen = zs.total_out;
    deflateEnd(&zs);
    uint8_t *out = NULL; size_t olen = 0;
    int grc = gzip_decompress(gdata, glen, &out, &olen);
    chk(grc == 0 && olen == msg_len && memcmp(out, msg, msg_len) == 0, "gzip 解压");
    free(cdata); free(gdata); free(out);

    if (failures == 0) printf("\n全部通过 ✅\n");
    else printf("\n%d 项失败 ❌\n", failures);
    return failures ? 1 : 0;
}
