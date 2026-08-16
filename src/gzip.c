#include "gzip.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int gzip_decompress(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    /* 15 + 16 = 自动检测 gzip/zlib 包装 */
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return -1;

    size_t cap = in_len * 16 + 4096;
    uint8_t *buf = (uint8_t *)malloc(cap ? cap : 4096);
    if (!buf) { inflateEnd(&zs); return -1; }

    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)in_len;

    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        zs.next_out = buf + zs.total_out;
        zs.avail_out = (uInt)(cap - zs.total_out);
        if (zs.avail_out == 0) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); inflateEnd(&zs); return -1; }
            buf = nb;
            continue;
        }
        ret = inflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            free(buf); inflateEnd(&zs); return -1;
        }
        if (zs.avail_in == 0 && ret == Z_BUF_ERROR) {
            free(buf); inflateEnd(&zs); return -1;
        }
    }

    *out = buf;
    *out_len = zs.total_out;
    inflateEnd(&zs);
    return 0;
}
