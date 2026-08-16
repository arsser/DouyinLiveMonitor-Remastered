/*
 * X-Bogus 签名 —— 纯 C 逆向移植。
 *
 * 算法（自 sign.js 还原）：
 *   1. header = (kWebsocket<<6) | (initialized<<5) | (header_bit<<4)
 *      其中 kWebsocket=1, initialized=0，故 header = 64 或 80。
 *   2. 构造 9 字节缓冲：
 *        [0] = 1          (t<<6 | (bogusIndex&63)，bogusIndex=1)
 *        [1] = 0          (envcode>>8 & 255，envcode=1)
 *        [2] = 1          (envcode & 255)
 *        [3] = 14         (ubcode，裸环境固定值)
 *        [4..5] = MD5( MD5("") 的 16 字节 ) 的第 14、15 字节
 *        [6..7] = MD5( hexDecode(md5hex) 的 16 字节 ) 的第 14、15 字节
 *        [8] = 随机字节
 *   3. checksum = 缓冲 9 字节异或；buf = 缓冲 + checksum（10 字节）。
 *   4. rc4 = RC4(key=chr(rnd), data=buf)。
 *   5. raw = chr(header) + chr(rnd) + rc4（12 字节）。
 *   6. X-Bogus = 用 S1 字母表做标准 base64（12 字节 → 16 字符，无填充）。
 */
#include "xbogus.h"
#include "md5.h"
#include <stdlib.h>
#include <string.h>

static const char S1[] =
    "Dkdpgh4ZKsQB80/Mfvw36XI1R25-WUAlEi7NLboqYTOPuzmFjJnryx9HVGcaStCe=";

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void hex_decode(const char *hex, size_t nbytes, uint8_t *out) {
    for (size_t i = 0; i < nbytes; i++) {
        out[i] = (uint8_t)((hex_nibble(hex[2 * i]) << 4) | hex_nibble(hex[2 * i + 1]));
    }
}

static void rc4(const uint8_t *key, size_t keylen,
                const uint8_t *data, size_t datalen, uint8_t *out) {
    uint8_t S[256];
    int i, j;
    for (i = 0; i < 256; i++) S[i] = (uint8_t)i;

    j = 0;
    for (i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % keylen]) & 255;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }

    i = 0; j = 0;
    for (size_t k = 0; k < datalen; k++) {
        i = (i + 1) & 255;
        j = (j + S[i]) & 255;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
        out[k] = (uint8_t)(data[k] ^ S[(S[i] + S[j]) & 255]);
    }
}

static void base64_s1(const uint8_t *data, size_t len, char *out) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        out[o++] = S1[(n >> 18) & 63];
        out[o++] = S1[(n >> 12) & 63];
        out[o++] = S1[(n >> 6) & 63];
        out[o++] = S1[n & 63];
    }
    out[o] = '\0';
}

void xbogus_compute(const char *md5hex, int header_bit, uint8_t byte8, uint8_t rnd, char out[17]) {
    uint8_t b[9];
    uint8_t t[16];

    /* 1. header */
    int key = 1;         /* kWebsocket */
    int initialized = 0;
    int header = (key << 6) | (initialized << 5) | ((header_bit & 1) << 4);

    /* 2. 9 字节缓冲 */
    b[0] = (uint8_t)((0 << 6) | (1 & 63));   /* t=0, bogusIndex=1 */
    b[1] = (uint8_t)((1 >> 8) & 255);        /* envcode=1 */
    b[2] = (uint8_t)(1 & 255);
    b[3] = 14;                               /* ubcode（裸环境） */

    /* [4..5] = MD5(MD5("") 的 16 字节) 的 [14],[15] */
    md5_str("", t);                          /* MD5("") */
    md5_buf(t, 16, t);                       /* MD5(16 字节) */
    b[4] = t[14];
    b[5] = t[15];

    /* [6..7] = MD5(hexDecode(md5hex) 的 16 字节) 的 [14],[15] */
    uint8_t in[16];
    hex_decode(md5hex, 16, in);
    md5_buf(in, 16, t);
    b[6] = t[14];
    b[7] = t[15];

    b[8] = byte8;

    /* 3. checksum + buf */
    uint8_t buf[10];
    uint8_t checksum = 0;
    for (int i = 0; i < 9; i++) {
        buf[i] = b[i];
        checksum ^= b[i];
    }
    buf[9] = checksum;

    /* 4. RC4 */
    uint8_t rc[10];
    rc4(&rnd, 1, buf, 10, rc);

    /* 5. raw = header + rnd + rc4 */
    uint8_t raw[12];
    raw[0] = (uint8_t)header;
    raw[1] = rnd;
    memcpy(raw + 2, rc, 10);

    /* 6. base64 */
    base64_s1(raw, 12, out);
}

void xbogus_generate(const char *md5hex, char out[17]) {
    int header_bit = rand() & 1;
    uint8_t byte8 = (uint8_t)(rand() & 255);
    uint8_t rnd = (uint8_t)(rand() & 255);
    xbogus_compute(md5hex, header_bit, byte8, rnd, out);
}
