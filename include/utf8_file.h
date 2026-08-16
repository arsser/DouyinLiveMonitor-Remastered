#ifndef DLM_UTF8_FILE_H
#define DLM_UTF8_FILE_H

/* UTF-8 路径的文件操作辅助（Windows 的 fopen/_mkdir 按 ANSI 代码页解释路径，中文会乱码） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

static inline FILE *fopen_utf8(const char *path_utf8, const char *mode_utf8) {
    int pn = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, NULL, 0);
    int mn = MultiByteToWideChar(CP_UTF8, 0, mode_utf8, -1, NULL, 0);
    if (pn <= 0 || mn <= 0) return NULL;
    wchar_t *wp = (wchar_t *)malloc((size_t)pn * sizeof(wchar_t));
    wchar_t *wm = (wchar_t *)malloc((size_t)mn * sizeof(wchar_t));
    if (!wp || !wm) { free(wp); free(wm); return NULL; }
    MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wp, pn);
    MultiByteToWideChar(CP_UTF8, 0, mode_utf8, -1, wm, mn);
    FILE *f = _wfopen(wp, wm);
    free(wp);
    free(wm);
    return f;
}

static inline void mkdir_p_utf8(const char *path_utf8) {
    int pn = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, NULL, 0);
    if (pn <= 0) return;
    wchar_t *w = (wchar_t *)malloc((size_t)pn * sizeof(wchar_t));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, w, pn);
    size_t len = wcslen(w);
    for (size_t i = 1; i < len; i++) {
        if (w[i] == L'/' || w[i] == L'\\') {
            wchar_t c = w[i]; w[i] = L'\0';
            CreateDirectoryW(w, NULL);
            w[i] = c;
        }
    }
    CreateDirectoryW(w, NULL);
    free(w);
}

#endif /* DLM_UTF8_FILE_H */
