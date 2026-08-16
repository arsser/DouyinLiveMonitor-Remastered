/* WinHTTP 最小 HTTP 客户端 */
#include "http.h"
#include "gzip.h"
#include <windows.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t *UA = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                           L"(KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";

void http_response_free(http_response_t *resp) {
    free(resp->body);
    free(resp->set_cookie);
    resp->body = NULL;
    resp->set_cookie = NULL;
}

char *http_cookie_value(const char *set_cookie, const char *name) {
    if (!set_cookie) return NULL;
    const char *p = set_cookie;
    size_t name_len = strlen(name);
    while (*p) {
        /* 跳过前导空格/分号 */
        while (*p == ' ' || *p == ';') p++;
        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            p += name_len + 1;
            const char *end = p;
            while (*end && *end != ';') end++;
            size_t len = (size_t)(end - p);
            char *out = (char *)malloc(len + 1);
            if (!out) return NULL;
            memcpy(out, p, len);
            out[len] = '\0';
            return out;
        }
        while (*p && *p != ';') p++;
    }
    return NULL;
}

/* UTF-8 -> wide */
static wchar_t *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *wide_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

int http_get(const char *url, const char *cookie_header, const char *referer,
             http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));

    wchar_t *wurl = utf8_to_wide(url);
    if (!wurl) return -1;

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { free(wurl); return -1; }

    wchar_t *host = (wchar_t *)malloc((uc.dwHostNameLength + 1) * sizeof(wchar_t));
    wchar_t *path = (wchar_t *)malloc((uc.dwUrlPathLength + uc.dwExtraInfoLength + 2) * sizeof(wchar_t));
    if (!host || !path) { free(host); free(path); free(wurl); return -1; }
    memcpy(host, uc.lpszHostName, uc.dwHostNameLength * sizeof(wchar_t));
    host[uc.dwHostNameLength] = 0;
    int pathlen = 0;
    if (uc.dwUrlPathLength > 0) { memcpy(path, uc.lpszUrlPath, uc.dwUrlPathLength * sizeof(wchar_t)); pathlen = (int)uc.dwUrlPathLength; }
    if (uc.dwExtraInfoLength > 0) { memcpy(path + pathlen, uc.lpszExtraInfo, uc.dwExtraInfoLength * sizeof(wchar_t)); pathlen += (int)uc.dwExtraInfoLength; }
    if (pathlen == 0) { path[0] = L'/'; path[1] = 0; }
    else { path[pathlen] = 0; }

    BOOL secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET session = WinHttpOpen(UA, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { free(host); free(path); free(wurl); return -1; }

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) { WinHttpCloseHandle(session); free(host); free(path); free(wurl); return -1; }

    wchar_t *wreferer = referer ? utf8_to_wide(referer) : NULL;
    HINTERNET req = WinHttpOpenRequest(connect, L"GET", path, NULL,
                                       wreferer ? wreferer : WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    free(wreferer);
    if (!req) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); free(host); free(path); free(wurl); return -1; }

    /* 请求头（需容纳完整 Cookie，可达数 KB） */
    wchar_t hdrs[20000] = {0};
    wcsncpy(hdrs, L"User-Agent: ", 19999);
    wcsncat(hdrs, UA, 19999 - wcslen(hdrs));
    wcsncat(hdrs, L"\r\n", 19999 - wcslen(hdrs));
    if (cookie_header) {
        wchar_t *wc = utf8_to_wide(cookie_header);
        wcsncat(hdrs, L"Cookie: ", 19999 - wcslen(hdrs));
        if (wc) { wcsncat(hdrs, wc, 19999 - wcslen(hdrs)); free(wc); }
        wcsncat(hdrs, L"\r\n", 19999 - wcslen(hdrs));
    }
    WinHttpAddRequestHeaders(req, hdrs, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    /* 设置超时，避免网络挂起导致调用方永久阻塞 */
    WinHttpSetTimeouts(req, 10000, 10000, 10000, 10000);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        free(host); free(path); free(wurl);
        return -1;
    }
    if (!WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        free(host); free(path); free(wurl);
        return -1;
    }

    /* 状态码 */
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status;

    /* Set-Cookie 头（可能多个） */
    DWORD idx = 0;
    for (;;) {
        DWORD need = 0;
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                 WINHTTP_NO_OUTPUT_BUFFER, &need, &idx)) {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && need > 0) {
                wchar_t *buf = (wchar_t *)malloc(need);
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                        buf, &need, &idx)) {
                    char *s = wide_to_utf8(buf);
                    if (s) {
                        if (resp->set_cookie) {
                            size_t old = strlen(resp->set_cookie);
                            resp->set_cookie = (char *)realloc(resp->set_cookie, old + strlen(s) + 3);
                            strcat(resp->set_cookie, "; ");
                            strcat(resp->set_cookie, s);
                        } else {
                            resp->set_cookie = s; s = NULL;
                        }
                        free(s);
                    }
                    free(buf);
                    idx++;
                    continue;
                }
                free(buf);
            }
            break;
        }
        break;
    }

    /* Content-Encoding（判断是否 gzip） */
    wchar_t enc_w[64] = {0};
    DWORD enc_len = sizeof(enc_w);
    char *enc = NULL;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_ENCODING, WINHTTP_HEADER_NAME_BY_INDEX,
                            enc_w, &enc_len, WINHTTP_NO_HEADER_INDEX)) {
        enc = wide_to_utf8(enc_w);
    }

    /* 读取响应体（循环读取直到耗尽） */
    size_t cap = 0, len = 0;
    char *body = NULL;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        if (len + avail + 1 > cap) {
            size_t nc = len + avail + 1 + 65536;
            char *nb = (char *)realloc(body, nc);
            if (!nb) break;
            body = nb; cap = nc;
        }
        DWORD read = 0;
        if (!WinHttpReadData(req, body + len, avail, &read) || read == 0) break;
        len += read;
    }

    if (body) body[len] = '\0';

    /* 若为 gzip 编码，解压（requests 会自动做，WinHTTP 不会） */
    if (enc && strstr(enc, "gzip") && len > 0) {
        uint8_t *dec = NULL; size_t dec_len = 0;
        if (gzip_decompress((const uint8_t *)body, len, &dec, &dec_len) == 0) {
            free(body);
            body = (char *)realloc(dec, dec_len + 1);
            if (body) { body[dec_len] = '\0'; len = dec_len; }
            else { body = (char *)dec; len = dec_len; }
        }
    }
    free(enc);

    resp->body = body;
    resp->body_len = len;

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    free(host);
    free(path);
    free(wurl);
    return 0;
}
