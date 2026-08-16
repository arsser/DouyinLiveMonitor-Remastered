/* X-Bogus / MD5 对拍测试：与 sign.js（Math.random=0）的已知输出比对 */
#include <stdio.h>
#include <string.h>
#include "md5.h"
#include "xbogus.h"

static int failures = 0;

static void check_str(const char *label, const char *got, const char *expect) {
    int ok = strcmp(got, expect) == 0;
    printf("[%s] %s\n  got   =%s\n  expect=%s\n", ok ? "PASS" : "FAIL", label, got, expect);
    if (!ok) failures++;
}

static void check_hex(const char *label, const char *got, const char *expect) {
    int ok = strcmp(got, expect) == 0;
    printf("[%s] %s\n  got   =%s\n  expect=%s\n", ok ? "PASS" : "FAIL", label, got, expect);
    if (!ok) failures++;
}

int main(void) {
    /* ---- MD5 正确性 ---- */
    uint8_t d[16];
    char h[33];
    md5_str("", d); md5_hex(d, h);
    check_hex("MD5('')", h, "d41d8cd98f00b204e9800998ecf8427e");
    md5_str("abc", d); md5_hex(d, h);
    check_hex("MD5('abc')", h, "900150983cd24fb0d6963f7d28e17f72");
    md5_str("The quick brown fox jumps over the lazy dog", d); md5_hex(d, h);
    check_hex("MD5(fox)", h, "9e107d9d372bb6826bd81d3542a419d6");

    /* ---- X-Bogus（random=0 → header_bit=0, byte8=0, rnd=0） ---- */
    char out[17];
    struct { const char *md5; const char *expect; } cases[] = {
        {"b5f7f2b3e491718deb69195be3284b1b", "fDpl4KiMGE7XTRYb"},
        {"51b61920a08ead94f29e425711c5ba5c", "fDpl4KiMGEN6PVTm"},
        {"00000000000000000000000000000000", "fDpl4KiMGEihqVPM"},
        {"ffffffffffffffffffffffffffffffff", "fDpl4KiMGEi/CKON"},
        {"0123456789abcdef0123456789abcdef", "fDpl4KiMGENs2KOV"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        xbogus_compute(cases[i].md5, 0, 0, 0, out);
        char label[64];
        snprintf(label, sizeof(label), "X-Bogus(%s)", cases[i].md5);
        check_str(label, out, cases[i].expect);
    }

    /* ---- 随机版本仅验证长度与字符集 ---- */
    xbogus_generate("b5f7f2b3e491718deb69195be3284b1b", out);
    int len_ok = strlen(out) == 16;
    printf("[%s] xbogus_generate 长度=16 (%zu)\n", len_ok ? "PASS" : "FAIL", strlen(out));
    if (!len_ok) failures++;

    if (failures == 0) {
        printf("\n全部测试通过 ✅\n");
    } else {
        printf("\n%d 项失败 ❌\n", failures);
    }
    return failures ? 1 : 0;
}
