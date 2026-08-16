#include "protobuf.h"
#include <string.h>

int pb_read_varint(const uint8_t **p, const uint8_t *end, uint64_t *out) {
    uint64_t v = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            *out = v;
            return 0;
        }
        shift += 7;
        if (shift >= 64) return -1;
    }
    return -1;
}

int pb_read_field(const uint8_t **p, const uint8_t *end,
                  uint32_t *field, int *wire,
                  uint64_t *varint_value,
                  const uint8_t **bytes, size_t *bytes_len) {
    uint64_t tag;
    if (*p >= end) return 0;
    if (pb_read_varint(p, end, &tag) != 0) return -1;
    *field = (uint32_t)(tag >> 3);
    *wire = (int)(tag & 7);
    switch (*wire) {
        case 0:
            if (pb_read_varint(p, end, varint_value) != 0) return -1;
            break;
        case 1: /* 64-bit fixed */
            if (end - *p < 8) return -1;
            *p += 8;
            break;
        case 2: {
            uint64_t len;
            if (pb_read_varint(p, end, &len) != 0) return -1;
            if ((size_t)(end - *p) < (size_t)len) return -1;
            *bytes = *p;
            *bytes_len = (size_t)len;
            *p += len;
            break;
        }
        case 5: /* 32-bit fixed */
            if (end - *p < 4) return -1;
            *p += 4;
            break;
        default:
            return -1;
    }
    return 1;
}

int pb_get_varint(const uint8_t *data, size_t len, uint32_t field, uint64_t *out) {
    const uint8_t *p = data, *end = data + len;
    uint32_t f; int w; uint64_t v; const uint8_t *b; size_t bl;
    int r;
    while ((r = pb_read_field(&p, end, &f, &w, &v, &b, &bl)) > 0) {
        if (f == field && w == 0) { *out = v; return 1; }
    }
    return 0;
}

int pb_get_bytes(const uint8_t *data, size_t len, uint32_t field,
                 const uint8_t **out, size_t *out_len) {
    const uint8_t *p = data, *end = data + len;
    uint32_t f; int w; uint64_t v; const uint8_t *b; size_t bl;
    int r;
    while ((r = pb_read_field(&p, end, &f, &w, &v, &b, &bl)) > 0) {
        if (f == field && w == 2) { *out = b; *out_len = bl; return 1; }
    }
    return 0;
}

int pb_get_message(const uint8_t *data, size_t len, uint32_t field,
                   const uint8_t **out, size_t *out_len) {
    return pb_get_bytes(data, len, field, out, out_len);
}

void pb_copy_str(const uint8_t *data, size_t len, char *buf, size_t buflen) {
    if (buflen == 0) return;
    size_t n = len < buflen - 1 ? len : buflen - 1;
    memcpy(buf, data, n);
    buf[n] = '\0';
}
