/* 抖音直播弹幕 GUI（Win32 原生） */
#include <windows.h>
#include <winhttp.h>
#include <richedit.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <ctype.h>
#include "fetcher.h"
#include "enter.h"
#include "json.h"
#include "utf8_file.h"

/* 配置/数据路径：固定在可执行文件所在目录，避免因启动位置不同而错位 */
static char g_config_path[600];
static char g_data_dir[600];

static void init_paths(void) {
    wchar_t wp[1024];
    GetModuleFileNameW(NULL, wp, 1024);
    wchar_t *slash = wcsrchr(wp, L'\\');
    if (slash) *slash = L'\0';
    char dir[512];
    WideCharToMultiByte(CP_UTF8, 0, wp, -1, dir, (int)sizeof(dir), NULL, NULL);
    snprintf(g_config_path, sizeof(g_config_path), "%s\\config.json", dir);
    snprintf(g_data_dir, sizeof(g_data_dir), "%s\\data", dir);
}

/* 控件 ID */
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
#define IDC_ANCHOR_LIST 1001
#define IDC_BTN_ADD 1002
#define IDC_BTN_DEL 1003
#define IDC_BTN_CHECKALL 1004
#define IDC_BTN_PLAY 1005
#define IDC_BTN_COOKIE 1006
#define IDC_BTN_CONNECT 1007
#define IDC_SEARCH 1008
#define IDC_MSG_DISPLAY 1100
#define IDC_STATUS 1101
#define IDC_VIEWER 1102
#define IDC_FILTER_ROOT 1200
#define IDC_FILTER_BASE 1210

/* 自定义消息 */
#define WM_APP_CHECK (WM_APP + 1)
#define WM_APP_CONNECTED (WM_APP + 2)
#define WM_APP_DISCONNECTED (WM_APP + 3)
#define WM_APP_NAME (WM_APP + 4)
#define WM_APP_STREAMS (WM_APP + 5)
#define WM_APP_CHECK_DONE (WM_APP + 6)

#define IDT_QUEUE 1

#define N_FILTER 6
static const char *g_filter_types[N_FILTER] = { "chat", "gift", "like", "member", "social", "fansclub" };
static const wchar_t *g_filter_names[N_FILTER] = { L"弹幕", L"礼物", L"点赞", L"进场", L"关注", L"粉丝团" };

/* 全局状态 */
static HINSTANCE g_hinst;
static HWND g_hwnd;
static HWND g_anchor_list, g_msg_display, g_status, g_viewer, g_play_btn, g_search;
static HWND g_filter_cb[N_FILTER], g_filter_root_cb;
static int g_filter_on[N_FILTER] = { 1, 0, 0, 0, 0, 0 };

/* 右键播放菜单用：清晰度 URL 缓存 */
static char g_menu_urls[6][1536];
static int g_menu_count = 0;

static anchor_list_t g_anchors;
static char g_cookie[16384];

static volatile int g_stop = 0;
static HANDLE g_fetch_thread = NULL;
static int g_connected = 0;
static char g_current_rid[64];
static char g_current_name[256];
static char g_rid_folder[512];
static char g_current_status[800];      /* 当前连接主播的状态文本 */
static volatile LONG g_fetch_gen = 0;   /* 连接代次，用于忽略陈旧断开消息 */
static char g_cached_rid[64];           /* 缓存的直播流（右键菜单提速） */
static room_info_t g_cached_info;
static int g_cached_valid = 0;

/* 线程安全消息队列 */
#define MAX_QUEUE 4096
typedef struct { char type[16]; char *line; } queued_msg_t;
static queued_msg_t g_queue[MAX_QUEUE];
static int g_q_head = 0, g_q_count = 0;
static CRITICAL_SECTION g_q_cs;

static HFONT g_font_ui, g_font_msg;

/* g_cookie 由 GUI 线程写、check/fetch 线程读，用临界区保护避免数据竞争 */
static CRITICAL_SECTION g_cookie_cs;

static void cookie_snapshot(char *buf, size_t sz) {
    EnterCriticalSection(&g_cookie_cs);
    snprintf(buf, sz, "%s", g_cookie);
    LeaveCriticalSection(&g_cookie_cs);
}

static void cookie_set(const char *v) {
    EnterCriticalSection(&g_cookie_cs);
    snprintf(g_cookie, sizeof(g_cookie), "%s", v);
    LeaveCriticalSection(&g_cookie_cs);
}

/* ---------------- 配置 ---------------- */
static void load_cfg(void) {
    anchor_list_free(&g_anchors);
    g_cookie[0] = '\0';
    FILE *f = fopen_utf8(g_config_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)n + 1);
    if (text) {
        if (n > 0) fread(text, 1, (size_t)n, f);
        text[n] = '\0';
        char *cookie = NULL;
        json_load_config(text, &g_anchors, &cookie);
        if (cookie) { snprintf(g_cookie, sizeof(g_cookie), "%s", cookie); free(cookie); }
        free(text);
    }
    fclose(f);
}

static void save_cfg(void) {
    char *out = json_save_config(&g_anchors, g_cookie);
    FILE *f = fopen_utf8(g_config_path, "wb");
    if (f) { fwrite(out, 1, strlen(out), f); fclose(f); }
    free(out);
}

/* ---------------- UTF-8 -> wide 辅助 ---------------- */
static void set_wnd_text_utf8(HWND hwnd, const char *utf8) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wn);
    SetWindowTextW(hwnd, w);
    free(w);
}

static void set_status_utf8(const char *utf8) { set_wnd_text_utf8(g_status, utf8); }

static void listbox_add_utf8(const char *utf8) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wn);
    SendMessageW(g_anchor_list, LB_ADDSTRING, 0, (LPARAM)w);
    free(w);
}

/* ---------------- 消息队列 ---------------- */
static void queue_push(const char *type, const char *line) {
    EnterCriticalSection(&g_q_cs);
    int idx;
    if (g_q_count < MAX_QUEUE) {
        idx = (g_q_head + g_q_count) % MAX_QUEUE;
        g_q_count++;
    } else {
        idx = g_q_head;
        free(g_queue[idx].line);
        g_q_head = (g_q_head + 1) % MAX_QUEUE;
    }
    snprintf(g_queue[idx].type, sizeof(g_queue[idx].type), "%s", type);
    g_queue[idx].line = strdup(line);
    LeaveCriticalSection(&g_q_cs);
}

static int queue_pop(char *type, size_t type_sz, char **line) {
    int got = 0;
    EnterCriticalSection(&g_q_cs);
    if (g_q_count > 0) {
        snprintf(type, type_sz, "%s", g_queue[g_q_head].type);
        *line = g_queue[g_q_head].line;
        g_q_head = (g_q_head + 1) % MAX_QUEUE;
        g_q_count--;
        got = 1;
    }
    LeaveCriticalSection(&g_q_cs);
    return got;
}

/* ---------------- 数据存档 ---------------- */
static CRITICAL_SECTION g_arch_cs;

static void sanitize_filename(char *s) {
    for (char *p = s; *p; p++) {
        if (*p == '<' || *p == '>' || *p == ':' || *p == '"' ||
            *p == '/' || *p == '\\' || *p == '|' || *p == '?' || *p == '*') {
            *p = '_';
        }
    }
}

/* 存档到 data/{主播名}（{id}）/{日期}.jsonl（与原项目一致） */
static void archive_message(const char *type, const char *time_str, const char *text) {
    if (!g_current_rid[0]) return;
    const char *stripped = text;
    size_t ts_len = strlen(time_str);
    if (strncmp(text, "[", 1) == 0 && strlen(text) > ts_len + 2 &&
        strncmp(text + 1, time_str, ts_len) == 0 && strncmp(text + ts_len + 1, "] ", 2) == 0) {
        stripped = text + ts_len + 3;
    }
    char date[16];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_s(&tmv, &now);
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);

    char dir[1300], path[1400];
    snprintf(dir, sizeof(dir), "%s\\%s", g_data_dir, g_rid_folder);
    snprintf(path, sizeof(path), "%s\\%s.jsonl", dir, date);

    EnterCriticalSection(&g_arch_cs);
    mkdir_p_utf8(dir);
    FILE *f = fopen_utf8(path, "ab");
    if (f) {
        fputc('{', f);
        fprintf(f, "\"time\": \"%s\", \"type\": \"%s\", \"text\": \"", time_str, type);
        for (const char *p = stripped; *p; p++) {
            if (*p == '"' || *p == '\\') fputc('\\', f);
            fputc(*p, f);
        }
        fprintf(f, "\"}\n");
        fclose(f);
    }
    LeaveCriticalSection(&g_arch_cs);

    /* 调试：首次存档时记录路径，便于排查 */
    static int dbg_done = 0;
    if (!dbg_done) {
        dbg_done = 1;
        char logpath[1400];
        snprintf(logpath, sizeof(logpath), "%s\\archive.log", g_data_dir);
        FILE *lf = fopen_utf8(logpath, "w");
        if (lf) { fprintf(lf, "data_dir=%s\nfolder=%s\nfile=%s\n", g_data_dir, g_rid_folder, path); fclose(lf); }
    }
}

/* ---------------- 弹幕回调（fetcher 线程） ---------------- */
static void gui_on_message(void *ud, const char *type, const char *time_str, const char *text) {
    (void)ud;
    archive_message(type, time_str, text);  /* 所有消息都存档，不受过滤影响 */
    queue_push(type, text);
}
static void gui_on_open(void *ud) {
    (void)ud;
    PostMessage(g_hwnd, WM_APP_CONNECTED, 0, 0);
}

/* ---------------- 主播列表 ---------------- */
static void refresh_anchor_list(void) {
    SendMessage(g_anchor_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_anchors.count; i++) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s  (%s)", g_anchors.items[i].name, g_anchors.items[i].rid);
        listbox_add_utf8(buf);
    }
}

/* 大小写不敏感子串匹配 */
static const char *stristr(const char *hay, const char *needle) {
    if (!needle[0]) return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
        if (!*n) return hay;
    }
    return NULL;
}

/* 搜索主播：滚动到第一个匹配（昵称或房间号）的位置（不选中，避免触发自动连接） */
static void search_anchor(void) {
    if (!g_search || !g_anchor_list) return;
    int len = GetWindowTextLengthW(g_search);
    if (len <= 0) return;
    wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    if (!w) return;
    GetWindowTextW(g_search, w, len + 1);
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *q = (char *)malloc((size_t)n + 1);
    if (!q) { free(w); return; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, q, n, NULL, NULL);
    free(w);
    for (size_t i = 0; i < g_anchors.count; i++) {
        if (stristr(g_anchors.items[i].name, q) || strstr(g_anchors.items[i].rid, q)) {
            SendMessageW(g_anchor_list, LB_SETCARETINDEX, (WPARAM)i, 0); /* 聚焦光标 */
            SendMessageW(g_anchor_list, LB_SETTOPINDEX, (WPARAM)i, 0);   /* 滚动到顶部 */
            break;
        }
    }
    free(q);
}

static int get_selected_rid(char *rid, size_t rid_sz) {
    LRESULT sel = SendMessage(g_anchor_list, LB_GETCURSEL, 0, 0);
    if (sel < 0 || (size_t)sel >= g_anchors.count) return -1;
    snprintf(rid, rid_sz, "%s", g_anchors.items[sel].rid);
    return 0;
}

/* ---------------- 检测状态（用于排序） ---------------- */
typedef struct {
    char rid[64];
    int live;
    int checked;
    double viewers;
} check_state_t;

static check_state_t *g_states;
static size_t g_state_count;

/* 解析“粗略观看人数”字符串："3000+"→3000，"1w+"→10000，"1.2w"→12000，"10.3万"→103000 */
static double parse_user_count(const char *s) {
    if (!s || !s[0]) return 0;
    double v = strtod(s, NULL);
    if (strstr(s, "万") || strchr(s, 'w') || strchr(s, 'W')) v *= 10000.0;
    return v;
}

static void check_state_set(const char *rid, int live, double viewers) {
    for (size_t i = 0; i < g_state_count; i++) {
        if (strcmp(g_states[i].rid, rid) == 0) { g_states[i].live = live; g_states[i].viewers = viewers; g_states[i].checked = 1; return; }
    }
    check_state_t *n = (check_state_t *)realloc(g_states, (g_state_count + 1) * sizeof(check_state_t));
    if (!n) return;
    g_states = n;
    snprintf(g_states[g_state_count].rid, sizeof(g_states[g_state_count].rid), "%s", rid);
    g_states[g_state_count].live = live;
    g_states[g_state_count].viewers = viewers;
    g_states[g_state_count].checked = 1;
    g_state_count++;
}

static void check_state_get(const char *rid, int *live, double *viewers, int *checked) {
    *live = 0; *viewers = 0; if (checked) *checked = 0;
    for (size_t i = 0; i < g_state_count; i++) {
        if (strcmp(g_states[i].rid, rid) == 0) {
            *live = g_states[i].live; *viewers = g_states[i].viewers;
            if (checked) *checked = g_states[i].checked;
            return;
        }
    }
}

/* 排序：开播在前；同为开播按观看人数降序 */
static int anchor_cmp(const void *a, const void *b) {
    const anchor_t *x = (const anchor_t *)a;
    const anchor_t *y = (const anchor_t *)b;
    int xl = 0, yl = 0; double xv = 0, yv = 0;
    check_state_get(x->rid, &xl, &xv, NULL);
    check_state_get(y->rid, &yl, &yv, NULL);
    if (xl != yl) return yl - xl;            /* 开播(1)在前 */
    if (xv != yv) return (xv > yv) ? -1 : 1; /* 人数降序：多在前 */
    return 0;
}

static void sort_anchors(void) {
    qsort(g_anchors.items, g_anchors.count, sizeof(anchor_t), anchor_cmp);
    SendMessage(g_anchor_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_anchors.count; i++) {
        char buf[512];
        int live = 0, checked = 0; double viewers = 0;
        check_state_get(g_anchors.items[i].rid, &live, &viewers, &checked);
        if (checked)
            snprintf(buf, sizeof(buf), "%s%s  (%s)", live ? "[直播] " : "[未开] ", g_anchors.items[i].name, g_anchors.items[i].rid);
        else
            snprintf(buf, sizeof(buf), "%s  (%s)", g_anchors.items[i].name, g_anchors.items[i].rid);
        listbox_add_utf8(buf);
    }
}

/* ---------------- fetcher 线程 ---------------- */
typedef struct { char *rid; LONG gen; } fetch_arg_t;

static DWORD WINAPI fetch_thread(LPVOID arg) {
    fetch_arg_t *fa = (fetch_arg_t *)arg;
    char *rid = fa->rid;
    LONG gen = fa->gen;
    free(fa);
    fetcher_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.live_id, sizeof(cfg.live_id), "%s", rid);
    cookie_snapshot(cfg.cookie, sizeof(cfg.cookie));
    cfg.on_message = gui_on_message;
    cfg.on_open = gui_on_open;
    cfg.stop = &g_stop;
    fetcher_run(&cfg);
    free(rid);
    PostMessage(g_hwnd, WM_APP_DISCONNECTED, (WPARAM)gen, 0);
    return 0;
}

static void stop_fetch(void) {
    if (!g_connected && !g_fetch_thread) return;
    g_stop = 1;   /* 接收循环靠 1s 空闲超时定期返回并检查此标志，无需跨线程关句柄 */
    if (g_fetch_thread) {
        WaitForSingleObject(g_fetch_thread, 15000);  /* 连接阶段已设 10s 超时 + 停止检查，15s 足够 */
        CloseHandle(g_fetch_thread);
        g_fetch_thread = NULL;
    }
    g_connected = 0;
}

static void start_fetch(const char *rid) {
    stop_fetch();
    g_stop = 0;
    snprintf(g_current_rid, sizeof(g_current_rid), "%s", rid);
    const char *name = anchor_list_name(&g_anchors, rid);
    snprintf(g_current_name, sizeof(g_current_name), "%s", name ? name : rid);
    char folder[400];
    snprintf(folder, sizeof(folder), "%s（%s）", g_current_name, rid);
    sanitize_filename(folder);
    snprintf(g_rid_folder, sizeof(g_rid_folder), "%s", folder);
    LONG gen = InterlockedIncrement(&g_fetch_gen);
    fetch_arg_t *fa = (fetch_arg_t *)malloc(sizeof(fetch_arg_t));
    fa->rid = strdup(rid);
    fa->gen = gen;
    g_fetch_thread = CreateThread(NULL, 0, fetch_thread, fa, 0, NULL);
    g_connected = 1;
    set_status_utf8("连接中...");
}

/* ---------------- 检测线程 ---------------- */
typedef struct {
    char rid[64];
    room_info_t info;
    int ok;
    int auto_connect;   /* 1 = 选中触发，开播则自动连接 */
} check_result_t;

typedef struct { char rid[64]; int auto_connect; } check_arg_t;

static DWORD WINAPI check_thread(LPVOID arg) {
    check_arg_t *a = (check_arg_t *)arg;
    check_result_t *r = (check_result_t *)calloc(1, sizeof(check_result_t));
    snprintf(r->rid, sizeof(r->rid), "%s", a->rid);
    r->auto_connect = a->auto_connect;
    char ck[16384];
    cookie_snapshot(ck, sizeof(ck));
    r->ok = (enter_fetch(a->rid, ck, &r->info) == 0);
    PostMessage(g_hwnd, WM_APP_CHECK, 0, (LPARAM)r);
    free(a);
    return 0;
}

/* 一键检测：全并发 + ttwid 分批轮换。每 50 个主播共用一个新 ttwid，各批同时并发 */
#define CHECKALL_BATCH 50

typedef struct {
    char **rids;
    int n;
    int auto_connect;
    volatile LONG done;   /* 剩余未完成的检测数，减到 0 表示全部完成 */
} checkall_arg_t;

/* 单次检测参数：一个主播 + 一个 ttwid */
typedef struct {
    checkall_arg_t *a;
    int idx;
    char ttwid[512];
} check_one_arg_t;

static DWORD WINAPI check_one(LPVOID arg) {
    check_one_arg_t *o = (check_one_arg_t *)arg;
    check_result_t *r = (check_result_t *)calloc(1, sizeof(check_result_t));
    snprintf(r->rid, sizeof(r->rid), "%s", o->a->rids[o->idx]);
    r->auto_connect = o->a->auto_connect;
    r->ok = (enter_fetch_ttwid(o->a->rids[o->idx], o->ttwid, &r->info) == 0);
    PostMessage(g_hwnd, WM_APP_CHECK, 0, (LPARAM)r);
    if (InterlockedDecrement(&o->a->done) == 0) {
        PostMessage(g_hwnd, WM_APP_CHECK_DONE, 0, 0);
        for (int i = 0; i < o->a->n; i++) free(o->a->rids[i]);
        free(o->a->rids);
        free(o->a);
    }
    free(o);
    return 0;
}

static DWORD WINAPI checkall_coordinator(LPVOID arg) {
    checkall_arg_t *a = (checkall_arg_t *)arg;
    if (a->n == 0) {
        PostMessage(g_hwnd, WM_APP_CHECK_DONE, 0, 0);
        free(a->rids);
        free(a);
        return 0;
    }
    int n_batches = (a->n + CHECKALL_BATCH - 1) / CHECKALL_BATCH;

    /* 为每批取一个新 ttwid */
    char *ttwids = (char *)malloc((size_t)n_batches * 512);
    for (int b = 0; b < n_batches; b++) {
        if (fetch_anon_ttwid(ttwids + (size_t)b * 512, 512) != 0) ttwids[(size_t)b * 512] = '\0';
    }

    /* 一次性为所有主播开线程（各批同时并发），每批内共用各自的 ttwid */
    a->done = a->n;
    for (int i = 0; i < a->n; i++) {
        check_one_arg_t *o = (check_one_arg_t *)malloc(sizeof(check_one_arg_t));
        o->a = a;
        o->idx = i;
        memcpy(o->ttwid, ttwids + (size_t)(i / CHECKALL_BATCH) * 512, 512);
        CreateThread(NULL, 0, check_one, o, 0, NULL);
    }
    free(ttwids);
    return 0;
}

/* ---------------- PotPlayer ---------------- */
static int play_potplayer(const char *url) {
    static const char *paths[] = {
        "C:\\Program Files\\DAUM\\PotPlayer\\PotPlayerMini64.exe",
        "C:\\Program Files (x86)\\DAUM\\PotPlayer\\PotPlayerMini.exe",
        "C:\\Program Files\\PotPlayer\\PotPlayerMini64.exe",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (GetFileAttributesA(paths[i]) == INVALID_FILE_ATTRIBUTES) continue;
        char cmd[1800];
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", paths[i], url);
        STARTUPINFOA si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            return 0;
        }
    }
    return -1;
}

/* 播放线程（后台取流并拉起 PotPlayer，避免阻塞 UI） */
static DWORD WINAPI play_thread(LPVOID arg) {
    char *rid = (char *)arg;
    char ck[16384];
    cookie_snapshot(ck, sizeof(ck));
    room_info_t info;
    if (enter_fetch(rid, ck, &info) == 0 && info.is_live) {
        const char *best = room_best_stream(&info);
        if (best) play_potplayer(best);
        else MessageBoxA(NULL, "未获取到直播流", "提示", MB_OK);
    } else {
        MessageBoxA(NULL, "未开播或获取失败", "提示", MB_OK);
    }
    free(rid);
    return 0;
}

/* 复制文本到剪贴板 */
static void copy_to_clipboard(const char *s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t n = strlen(s) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (h) {
        void *p = GlobalLock(h);
        if (p) { memcpy(p, s, n); GlobalUnlock(h); }
        SetClipboardData(CF_TEXT, h);
    }
    CloseClipboard();
}

/* 显示多清晰度右键菜单（播放/复制直链） */
static void show_stream_menu(const room_info_t *info) {
    static const char *order[6] = { "origin", "FULL_HD1", "HD1", "SD2", "SD1", "audio" };
    static const wchar_t *names[6] = { L"原画", L"蓝光", L"超清", L"高清", L"标清", L"仅音频" };
    g_menu_count = 0;
    HMENU menu = CreatePopupMenu();
    if (info) {
        for (int i = 0; i < 6; i++) {
            const char *url = room_stream(info, order[i]);
            if (!url) continue;
            snprintf(g_menu_urls[g_menu_count], sizeof(g_menu_urls[0]), "%s", url);
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING, 2000 + g_menu_count, L"播放");
            AppendMenuW(sub, MF_STRING, 2100 + g_menu_count, L"复制直链");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, names[i]);
            g_menu_count++;
        }
    }
    if (g_menu_count == 0) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"未获取到直播流");
    }
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
}

/* 后台取流用于右键菜单 */
typedef struct { room_info_t info; int ok; } stream_result_t;

static DWORD WINAPI stream_fetch_thread(LPVOID arg) {
    char *rid = (char *)arg;
    char ck[16384];
    cookie_snapshot(ck, sizeof(ck));
    stream_result_t *r = (stream_result_t *)calloc(1, sizeof(stream_result_t));
    r->ok = (enter_fetch(rid, ck, &r->info) == 0 && r->info.is_live);
    PostMessage(g_hwnd, WM_APP_STREAMS, 0, (LPARAM)r);
    free(rid);
    return 0;
}

/* 昵称后台获取（添加主播留空名称时用） */
typedef struct { char rid[64]; char name[256]; } name_update_t;

static DWORD WINAPI name_fetch_thread(LPVOID arg) {
    char *rid = (char *)arg;
    char ck[16384];
    cookie_snapshot(ck, sizeof(ck));
    room_info_t info;
    if (enter_fetch(rid, ck, &info) == 0 && info.nickname[0]) {
        name_update_t *u = (name_update_t *)calloc(1, sizeof(name_update_t));
        snprintf(u->rid, sizeof(u->rid), "%s", rid);
        snprintf(u->name, sizeof(u->name), "%s", info.nickname);
        PostMessage(g_hwnd, WM_APP_NAME, 0, (LPARAM)u);
    }
    free(rid);
    return 0;
}

/* ---------------- 模态对话框 ---------------- */
static volatile int g_dlg_done = 0;
static int g_dlg_result = 0;

static int run_modal(HWND dlg) {
    g_dlg_done = 0; g_dlg_result = 0;
    EnableWindow(g_hwnd, FALSE);
    MSG msg;
    while (!g_dlg_done) {
        int r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g_hwnd, TRUE);
    SetForegroundWindow(g_hwnd);   /* 关闭对话框后恢复主窗口前台 */
    SetActiveWindow(g_hwnd);
    return g_dlg_result;
}

static void center_on_parent(HWND dlg, int w, int h) {
    RECT r;
    GetWindowRect(g_hwnd, &r);
    int x = r.left + (r.right - r.left - w) / 2;
    int y = r.top + (r.bottom - r.top - h) / 2;
    SetWindowPos(dlg, NULL, x, y, w, h, SWP_NOZORDER);
}

/* 添加主播对话框 */
static HWND g_add_rid_edit, g_add_name_edit;
static char g_add_rid[64], g_add_name[256];

static LRESULT CALLBACK add_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateWindowExW(0, L"STATIC", L"直播间ID:", WS_CHILD | WS_VISIBLE, 15, 15, 90, 20, hwnd, NULL, g_hinst, NULL);
            g_add_rid_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 15, 38, 280, 26, hwnd, NULL, g_hinst, NULL);
            CreateWindowExW(0, L"STATIC", L"名称(留空自动获取):", WS_CHILD | WS_VISIBLE, 15, 75, 200, 20, hwnd, NULL, g_hinst, NULL);
            g_add_name_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 15, 98, 280, 26, hwnd, NULL, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 80, 145, 90, 30, hwnd, (HMENU)IDOK, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 190, 145, 90, 30, hwnd, (HMENU)IDCANCEL, g_hinst, NULL);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                GetWindowTextA(g_add_rid_edit, g_add_rid, sizeof(g_add_rid));
                GetWindowTextA(g_add_name_edit, g_add_name, sizeof(g_add_name));
                g_dlg_result = 1; g_dlg_done = 1; DestroyWindow(hwnd);
                return 0;
            } else if (LOWORD(wp) == IDCANCEL) {
                g_dlg_result = 0; g_dlg_done = 1; DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:   /* 点右上角 X：等价于取消，结束模态循环，避免主窗口被永久禁用 */
            g_dlg_result = 0; g_dlg_done = 1; DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_add_dialog(void) {
    static int reg = 0;
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = add_dlg_proc;
        wc.hInstance = g_hinst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"DlmAddDlg";
        RegisterClassW(&wc);
        reg = 1;
    }
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DlmAddDlg", L"添加主播",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, 320, 210,
                               g_hwnd, NULL, g_hinst, NULL);
    center_on_parent(dlg, 320, 210);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    if (run_modal(dlg) && g_add_rid[0]) {
        if (g_add_name[0]) {
            anchor_list_set(&g_anchors, g_add_rid, g_add_name);
            save_cfg();
            refresh_anchor_list();
        } else {
            /* 名称留空：先用 rid 占位，后台获取昵称后再更新 */
            anchor_list_set(&g_anchors, g_add_rid, g_add_rid);
            save_cfg();
            refresh_anchor_list();
            char *arg = strdup(g_add_rid);
            CreateThread(NULL, 0, name_fetch_thread, arg, 0, NULL);
        }
    }
}

/* Cookie 对话框 */
static HWND g_cookie_edit;

static LRESULT CALLBACK cookie_dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateWindowExW(0, L"STATIC", L"浏览器登录 live.douyin.com → F12 控制台输入 document.cookie → 复制粘贴到下方",
                            WS_CHILD | WS_VISIBLE, 15, 12, 580, 40, hwnd, NULL, g_hinst, NULL);
            g_cookie_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                                            15, 58, 580, 300, hwnd, NULL, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, 370, 100, 30, hwnd, (HMENU)IDOK, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 500, 370, 100, 30, hwnd, (HMENU)IDCANCEL, g_hinst, NULL);
            /* 填入当前 cookie */
            {
                int wn = MultiByteToWideChar(CP_UTF8, 0, g_cookie, -1, NULL, 0);
                wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
                MultiByteToWideChar(CP_UTF8, 0, g_cookie, -1, w, wn);
                SetWindowTextW(g_cookie_edit, w);
                free(w);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                int len = GetWindowTextLengthA(g_cookie_edit);
                char *buf = (char *)malloc((size_t)len + 1);
                GetWindowTextA(g_cookie_edit, buf, len + 1);
                cookie_set(buf);
                free(buf);
                save_cfg();
                g_dlg_result = 1; g_dlg_done = 1; DestroyWindow(hwnd);
                return 0;
            } else if (LOWORD(wp) == IDCANCEL) {
                g_dlg_result = 0; g_dlg_done = 1; DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:   /* 点右上角 X：等价于取消，结束模态循环，避免主窗口被永久禁用 */
            g_dlg_result = 0; g_dlg_done = 1; DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_cookie_dialog(void) {
    static int reg = 0;
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = cookie_dlg_proc;
        wc.hInstance = g_hinst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"DlmCookieDlg";
        RegisterClassW(&wc);
        reg = 1;
    }
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DlmCookieDlg", L"Cookie 配置",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, 620, 440,
                               g_hwnd, NULL, g_hinst, NULL);
    center_on_parent(dlg, 620, 440);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    run_modal(dlg);
}

/* ---------------- 主窗口过程 ---------------- */
/* 是否已滚动到底部（用于决定新消息是否自动滚动） */
static int is_at_bottom(void) {
    int len = GetWindowTextLengthW(g_msg_display);
    if (len <= 0) return 1;
    int count = (int)SendMessageW(g_msg_display, EM_GETLINECOUNT, 0, 0);
    if (count <= 1) return 1;
    /* 取最后一条非空显示行的起始字符，判断它是否还在客户区内 */
    int line = count - 1;
    int idx = (int)SendMessageW(g_msg_display, EM_LINEINDEX, line, 0);
    if (idx < 0 && line > 0)
        idx = (int)SendMessageW(g_msg_display, EM_LINEINDEX, line - 1, 0);
    if (idx < 0) return 1;
    POINT pt;
    memset(&pt, 0, sizeof(pt));
    SendMessageW(g_msg_display, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)idx);
    RECT rc;
    GetClientRect(g_msg_display, &rc);
    return pt.y < rc.bottom;
}

/* ---------------- 消息显示历史（用于全范围过滤重绘） ---------------- */
#define HIST_MAX 50000    /* 内存里最多保留的显示消息条数（超出丢最旧） */
#define HIST_EVICT 10000  /* 达到上限时一次丢弃的最旧条数 */

typedef struct {
    char type[16];
    char *text;   /* malloc 的完整显示行（含时间戳） */
} hist_entry_t;

static hist_entry_t *g_hist;
static size_t g_hist_count;
static size_t g_hist_cap;

static void hist_append(const char *type, const char *text) {
    if (g_hist_count == g_hist_cap) {
        size_t ncap = g_hist_cap ? g_hist_cap * 2 : 256;
        hist_entry_t *n = (hist_entry_t *)realloc(g_hist, ncap * sizeof(hist_entry_t));
        if (!n) return;
        g_hist = n;
        g_hist_cap = ncap;
    }
    if (g_hist_count >= HIST_MAX) {
        /* 达到上限：丢弃最旧 HIST_EVICT 条（一次性批量淘汰，摊销成本） */
        for (size_t i = 0; i < HIST_EVICT; i++) free(g_hist[i].text);
        memmove(g_hist, g_hist + HIST_EVICT, (g_hist_count - HIST_EVICT) * sizeof(hist_entry_t));
        g_hist_count -= HIST_EVICT;
    }
    snprintf(g_hist[g_hist_count].type, sizeof(g_hist[g_hist_count].type), "%s", type);
    g_hist[g_hist_count].text = strdup(text);
    g_hist_count++;
}

/* 追加一行（不做滚动处理，供重建时批量使用） */
static void append_msg_plain(const char *utf8, COLORREF color) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wn);
    int len = GetWindowTextLengthW(g_msg_display);
    SendMessageW(g_msg_display, EM_SETSEL, len, len);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    SendMessageW(g_msg_display, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(g_msg_display, EM_REPLACESEL, 0, (LPARAM)w);
    SendMessageW(g_msg_display, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
    free(w);
}

static void append_msg(const char *utf8, COLORREF color) {
    POINT sp_before;
    SendMessageW(g_msg_display, EM_GETSCROLLPOS, 0, (LPARAM)&sp_before);
    CHARRANGE sel_before;
    SendMessageW(g_msg_display, EM_EXGETSEL, 0, (LPARAM)&sel_before);
    int at_bottom = is_at_bottom();

    /* 不在底部（用户正在浏览/选择文本）时，追加会把光标临时挪到末尾造成闪烁，先隐藏光标 */
    int hide_caret = (!at_bottom && GetFocus() == g_msg_display);
    if (hide_caret) HideCaret(g_msg_display);

    append_msg_plain(utf8, color);

    if (at_bottom) {
        /* 在底部：强制滚动到末尾（只读控件下 EM_SCROLLCARET 不可靠，改用 SB_BOTTOM） */
        SendMessageW(g_msg_display, EM_SETSEL, -1, -1);
        SendMessageW(g_msg_display, WM_VSCROLL, SB_BOTTOM, 0);
    } else {
        /* 用户在浏览历史：恢复选区与滚动位置，避免被插入操作顶走 */
        SendMessageW(g_msg_display, EM_EXSETSEL, 0, (LPARAM)&sel_before);
        sp_before.x = 0;
        SendMessageW(g_msg_display, EM_SETSCROLLPOS, 0, (LPARAM)&sp_before);
        if (hide_caret) ShowCaret(g_msg_display);
    }
}

static int filter_allows(const char *type) {
    for (int i = 0; i < N_FILTER; i++)
        if (strcmp(type, g_filter_types[i]) == 0) return g_filter_on[i];
    return 1;
}

/* 消息类型 -> 颜色（弹幕黑，其余参照原项目配色，适配白底加深） */
static COLORREF type_color(const char *type) {
    if (strcmp(type, "chat") == 0) return RGB(0, 0, 0);           /* 弹幕 黑 */
    if (strcmp(type, "gift") == 0) return RGB(0xe6, 0x51, 0x00);  /* 礼物 橙 */
    if (strcmp(type, "like") == 0) return RGB(0xd8, 0x1b, 0x60);  /* 点赞 粉 */
    if (strcmp(type, "member") == 0) return RGB(0x2e, 0x7d, 0x32); /* 进场 绿 */
    if (strcmp(type, "social") == 0) return RGB(0x00, 0x89, 0x7b); /* 关注 青 */
    if (strcmp(type, "fansclub") == 0) return RGB(0x8e, 0x24, 0xaa); /* 粉丝团 紫 */
    if (strcmp(type, "control") == 0) return RGB(0xc6, 0x28, 0x28);  /* 状态 红 */
    if (strcmp(type, "system") == 0) return RGB(0x60, 0x60, 0x60);   /* 系统分隔 灰 */
    return RGB(0, 0, 0);
}

/* 记录到历史并按当前过滤显示（过滤只在“显示”阶段生效，存档不受影响） */
static void display_line(const char *type, const char *text) {
    hist_append(type, text);
    if (filter_allows(type)) append_msg(text, type_color(type));
}

/* 按当前过滤设置，用全部历史重绘显示区（O(N)：一次拼装 + 一次写入 + 批量上色） */
static void rebuild_display(void) {
    if (!g_msg_display) return;

    /* 第一遍：统计要显示的宽字符总数（每行文本 + \r + 结尾 NUL） */
    size_t total = 1;
    for (size_t i = 0; i < g_hist_count; i++) {
        if (!filter_allows(g_hist[i].type)) continue;
        int n = MultiByteToWideChar(CP_UTF8, 0, g_hist[i].text, -1, NULL, 0);
        total += (n > 0 ? (size_t)(n - 1) : 0) + 1;
    }

    wchar_t *buf = (wchar_t *)malloc(total * sizeof(wchar_t));
    if (!buf) return;

    /* 第二遍：拼装整段文本 */
    size_t pos = 0;
    for (size_t i = 0; i < g_hist_count; i++) {
        if (!filter_allows(g_hist[i].type)) continue;
        int n = MultiByteToWideChar(CP_UTF8, 0, g_hist[i].text, -1, buf + pos, (int)(total - pos));
        if (n > 0) pos += (size_t)(n - 1);
        buf[pos++] = L'\r';   /* RichEdit 段落分隔符：单个 \r，写 \r\n 会被归一化导致上色偏移错位 */
    }
    buf[pos] = L'\0';

    SendMessageW(g_msg_display, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(g_msg_display, buf);                                  /* 一次性写入，O(N) */
    SendMessageW(g_msg_display, WM_SETFONT, (WPARAM)g_font_msg, TRUE);

    /* 第三遍：按类型给各行上色（只改颜色，不影响字体） */
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    pos = 0;
    for (size_t i = 0; i < g_hist_count; i++) {
        if (!filter_allows(g_hist[i].type)) continue;
        int n = MultiByteToWideChar(CP_UTF8, 0, g_hist[i].text, -1, NULL, 0);
        size_t len = (n > 0) ? (size_t)(n - 1) : 0;
        if (len > 0) {
            cf.crTextColor = type_color(g_hist[i].type);
            CHARRANGE cr;
            cr.cpMin = (LONG)pos;
            cr.cpMax = (LONG)(pos + len);
            SendMessageW(g_msg_display, EM_EXSETSEL, 0, (LPARAM)&cr);
            SendMessageW(g_msg_display, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        }
        pos += len + 1;   /* 文本 + \r */
    }

    free(buf);
    SendMessageW(g_msg_display, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_msg_display, NULL, TRUE);
    SendMessageW(g_msg_display, EM_SETSEL, -1, -1);
    SendMessageW(g_msg_display, WM_VSCROLL, SB_BOTTOM, 0);
}

static void drain_queue(void) {
    char type[16];
    char *line;

    /* 队列为空时直接返回，避免浏览状态下每 200ms 空转一次全量重绘导致闪烁 */
    EnterCriticalSection(&g_q_cs);
    int has_msg = (g_q_count > 0);
    LeaveCriticalSection(&g_q_cs);
    if (!has_msg) return;

    /* 用户正在浏览/选择文本时，整批关闭重绘，避免插入过程闪烁（恢复选区+滚动在中途不绘制） */
    int browsing = !is_at_bottom();
    if (browsing) SendMessageW(g_msg_display, WM_SETREDRAW, FALSE, 0);

    while (queue_pop(type, sizeof(type), &line)) {
        if (strcmp(type, "stats") == 0) {
            const char *p = strstr(line, "当前观看: ");
            if (p) {
                p += strlen("当前观看: ");
                char num[32]; size_t i = 0;
                while (p[i] >= '0' && p[i] <= '9' && i < sizeof(num) - 1) num[i] = p[i], i++;
                num[i] = '\0';
                char buf[64];
                snprintf(buf, sizeof(buf), "%s 人观看", num);
                set_wnd_text_utf8(g_viewer, buf);
            }
            free(line);
            continue;
        }
        if (strcmp(type, "room") == 0 || strcmp(type, "adaptation") == 0 ||
            strcmp(type, "rank") == 0 || strcmp(type, "roomstats") == 0 ||
            strcmp(type, "emoji") == 0) {
            free(line);  /* 房间类/表情消息不显示（roomstats 的在线观众数格式不稳，不用于显示） */
            continue;
        }
        display_line(type, line);
        free(line);
    }

    if (browsing) {
        SendMessageW(g_msg_display, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_msg_display, NULL, TRUE);  /* TRUE：擦除背景，避免残留旧内容 */
    }
}

static void listbox_add_utf8_at(int index, const char *utf8);

static void on_check_result(check_result_t *r) {
    /* 0. 记录检测状态（用于排序） */
    check_state_set(r->rid, (r->ok && r->info.is_live), parse_user_count(r->info.user_count));

    /* 1. 缓存直播流（右键菜单提速用） */
    if (r->ok && r->info.is_live) {
        g_cached_info = r->info;
        snprintf(g_cached_rid, sizeof(g_cached_rid), "%s", r->rid);
        g_cached_valid = 1;
    }
    /* 2. 更新列表前缀 */
    for (size_t i = 0; i < g_anchors.count; i++) {
        if (strcmp(g_anchors.items[i].rid, r->rid) == 0) {
            const char *disp = (r->ok && r->info.nickname[0]) ? r->info.nickname : g_anchors.items[i].name;
            char buf[512];
            if (r->ok && r->info.is_live) snprintf(buf, sizeof(buf), "[直播] %s  (%s)", disp, r->rid);
            else snprintf(buf, sizeof(buf), "[未开] %s  (%s)", disp, r->rid);
            SendMessageW(g_anchor_list, LB_DELETESTRING, (WPARAM)i, 0);
            listbox_add_utf8_at(i, buf);
            SendMessageW(g_anchor_list, LB_SETCURSEL, (WPARAM)i, 0);
            break;
        }
    }
    /* 3. 状态与自动连接 */
    if (r->auto_connect) {
        if (r->ok && r->info.is_live) {
            char st[800];
            snprintf(st, sizeof(st), "● 直播中 | %s 人观看 | %s", r->info.user_count, r->info.title);
            snprintf(g_current_status, sizeof(g_current_status), "%s", st);
            if (!g_connected || strcmp(g_current_rid, r->rid) != 0) {
                if (g_connected) {  /* 切换主播时在消息列表里加一条分隔提示 */
                    const char *nm = anchor_list_name(&g_anchors, r->rid);
                    char sys[600];
                    snprintf(sys, sizeof(sys), "── 已切换到 %s ──", nm ? nm : r->rid);
                    display_line("system", sys);
                }
                start_fetch(r->rid);  /* 断开旧的并连接新主播 */
            }
            set_status_utf8(st);
        } else {
            if (g_connected) set_status_utf8(g_current_status);  /* 未开播：保持原连接，状态切回原主播 */
            else set_status_utf8("○ 未开播");
        }
    } else {
        /* 一键检测：不自动连接 */
        if (!g_connected) {
            if (r->ok && r->info.is_live) {
                char st[800];
                snprintf(st, sizeof(st), "● 直播中 | %s 人观看 | %s", r->info.user_count, r->info.title);
                set_status_utf8(st);
            } else set_status_utf8("○ 未开播");
        } else if (r->ok && r->info.is_live && strcmp(g_current_rid, r->rid) == 0) {
            snprintf(g_current_status, sizeof(g_current_status), "● 直播中 | %s 人观看 | %s", r->info.user_count, r->info.title);
        }
    }
    free(r);
}

/* 在指定位置插入列表项（UTF-8） */
static void listbox_add_utf8_at(int index, const char *utf8) {
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wn);
    SendMessageW(g_anchor_list, LB_INSERTSTRING, (WPARAM)index, (LPARAM)w);
    free(w);
}

/* 底部信息栏自适应布局：左侧观看人数固定，其余宽度全给状态栏（标题更长也能多显示） */
static void layout_status_bar(HWND hwnd) {
    if (!g_viewer || !g_status) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int status_w = rc.right - 170 - 10;
    if (status_w < 100) status_w = 100;
    MoveWindow(g_viewer, 10, 546, 150, 20, TRUE);
    MoveWindow(g_status, 170, 546, status_w, 20, TRUE);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            g_font_ui = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, 0, L"Microsoft YaHei UI");
            g_font_msg = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, 0, L"Microsoft YaHei UI");

            CreateWindowExW(0, L"STATIC", L"主播列表", WS_CHILD | WS_VISIBLE, 10, 6, 80, 20, hwnd, NULL, g_hinst, NULL);
            g_search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                                       95, 4, 500, 22, hwnd, (HMENU)IDC_SEARCH, g_hinst, NULL);
            SendMessageW(g_search, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索昵称或房间号");
            g_anchor_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                                            10, 26, 600, 90, hwnd, (HMENU)IDC_ANCHOR_LIST, g_hinst, NULL);
            SendMessageW(g_anchor_list, WM_SETFONT, (WPARAM)g_font_ui, TRUE);

            CreateWindowExW(0, L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 120, 90, 28, hwnd, (HMENU)IDC_BTN_ADD, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 105, 120, 90, 28, hwnd, (HMENU)IDC_BTN_DEL, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"一键检测", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 200, 120, 100, 28, hwnd, (HMENU)IDC_BTN_CHECKALL, g_hinst, NULL);
            g_play_btn = CreateWindowExW(0, L"BUTTON", L"播放", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 305, 120, 75, 28, hwnd, (HMENU)IDC_BTN_PLAY, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"Cookie", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 385, 120, 85, 28, hwnd, (HMENU)IDC_BTN_COOKIE, g_hinst, NULL);
            CreateWindowExW(0, L"STATIC", L"过滤:", WS_CHILD | WS_VISIBLE, 10, 158, 36, 20, hwnd, NULL, g_hinst, NULL);
            g_filter_root_cb = CreateWindowExW(0, L"BUTTON", L"全部",
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                               46, 156, 50, 22, hwnd, (HMENU)IDC_FILTER_ROOT, g_hinst, NULL);
            SendMessageW(g_filter_root_cb, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            int fx = 100;
            for (int i = 0; i < N_FILTER; i++) {
                g_filter_cb[i] = CreateWindowExW(0, L"BUTTON", g_filter_names[i],
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 fx, 156, 60, 22, hwnd, (HMENU)(INT_PTR)(IDC_FILTER_BASE + i), g_hinst, NULL);
                SendMessageW(g_filter_cb[i], WM_SETFONT, (WPARAM)g_font_ui, TRUE);
                if (g_filter_on[i]) SendMessageW(g_filter_cb[i], BM_SETCHECK, BST_CHECKED, 0);
                fx += 60;
            }

            g_msg_display = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                            10, 182, 600, 358, hwnd, (HMENU)IDC_MSG_DISPLAY, g_hinst, NULL);
            SendMessageW(g_msg_display, WM_SETFONT, (WPARAM)g_font_msg, TRUE);
            SendMessageW(g_msg_display, EM_SETLIMITTEXT, 0, 0);

            g_viewer = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 546, 200, 20, hwnd, (HMENU)IDC_VIEWER, g_hinst, NULL);
            SendMessageW(g_viewer, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            g_status = CreateWindowExW(0, L"STATIC", L"未连接", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, 215, 546, 395, 20, hwnd, (HMENU)IDC_STATUS, g_hinst, NULL);
            SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
            layout_status_bar(hwnd);

            refresh_anchor_list();
            SetTimer(hwnd, IDT_QUEUE, 200, NULL);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDC_BTN_ADD) {
                show_add_dialog();
                return 0;
            } else if (id == IDC_BTN_DEL) {
                char rid[64];
                if (get_selected_rid(rid, sizeof(rid)) == 0) {
                    anchor_list_t nl; anchor_list_init(&nl);
                    for (size_t i = 0; i < g_anchors.count; i++)
                        if (strcmp(g_anchors.items[i].rid, rid) != 0) anchor_list_set(&nl, g_anchors.items[i].rid, g_anchors.items[i].name);
                    anchor_list_free(&g_anchors);
                    g_anchors = nl;
                    save_cfg();
                    refresh_anchor_list();
                }
                return 0;
            } else if (id == IDC_BTN_CHECKALL) {
                /* 快照主播 ID，交给协调器分批检测（每 50 个换新 ttwid） */
                checkall_arg_t *ca = (checkall_arg_t *)malloc(sizeof(checkall_arg_t));
                ca->n = (int)g_anchors.count;
                ca->rids = (char **)malloc((size_t)ca->n * sizeof(char *));
                for (int i = 0; i < ca->n; i++) ca->rids[i] = strdup(g_anchors.items[i].rid);
                ca->auto_connect = 0;
                CreateThread(NULL, 0, checkall_coordinator, ca, 0, NULL);
                return 0;
            } else if (id >= 2000 && id < 2000 + 6) {
                play_potplayer(g_menu_urls[id - 2000]);
                return 0;
            } else if (id >= 2100 && id < 2100 + 6) {
                copy_to_clipboard(g_menu_urls[id - 2100]);
                return 0;
            } else if (id == IDC_BTN_PLAY) {
                char rid[64];
                if (get_selected_rid(rid, sizeof(rid)) == 0) {
                    char *arg = strdup(rid);
                    CreateThread(NULL, 0, play_thread, arg, 0, NULL);
                }
                return 0;
            } else if (id == IDC_BTN_COOKIE) {
                show_cookie_dialog();
                return 0;
            } else if (id == IDC_FILTER_ROOT) {
                int on = SendMessageW(g_filter_root_cb, BM_GETCHECK, 0, 0) == BST_CHECKED;
                for (int i = 0; i < N_FILTER; i++) {
                    g_filter_on[i] = on;
                    SendMessageW(g_filter_cb[i], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
                }
                rebuild_display();
                return 0;
            } else if (id >= IDC_FILTER_BASE && id < IDC_FILTER_BASE + N_FILTER) {
                g_filter_on[id - IDC_FILTER_BASE] = SendMessageW(g_filter_cb[id - IDC_FILTER_BASE], BM_GETCHECK, 0, 0) == BST_CHECKED;
                rebuild_display();
                return 0;
            } else if (id == IDC_SEARCH && HIWORD(wp) == EN_CHANGE) {
                search_anchor();
                return 0;
            } else if (id == IDC_ANCHOR_LIST && HIWORD(wp) == LBN_SELCHANGE) {
                char rid[64];
                if (get_selected_rid(rid, sizeof(rid)) == 0) {
                    check_arg_t *a = (check_arg_t *)malloc(sizeof(check_arg_t));
                    snprintf(a->rid, sizeof(a->rid), "%s", rid);
                    a->auto_connect = 1;  /* 选中触发：开播则自动连接 */
                    CreateThread(NULL, 0, check_thread, a, 0, NULL);
                }
                return 0;
            }
            break;
        }
        case WM_CONTEXTMENU:
            if ((HWND)wp == g_play_btn) {
                char rid[64];
                if (get_selected_rid(rid, sizeof(rid)) == 0) {
                    if (g_cached_valid && strcmp(g_cached_rid, rid) == 0 && g_cached_info.is_live) {
                        show_stream_menu(&g_cached_info);  /* 命中缓存，即时弹出 */
                    } else {
                        char *arg = strdup(rid);
                        CreateThread(NULL, 0, stream_fetch_thread, arg, 0, NULL);
                    }
                }
            }
            return 0;
        case WM_APP_CHECK:
            on_check_result((check_result_t *)lp);
            return 0;
        case WM_APP_CHECK_DONE:
            sort_anchors();
            return 0;
        case WM_APP_STREAMS: {
            stream_result_t *r = (stream_result_t *)lp;
            show_stream_menu(r->ok ? &r->info : NULL);
            free(r);
            return 0;
        }
        case WM_APP_NAME: {
            name_update_t *u = (name_update_t *)lp;
            for (size_t i = 0; i < g_anchors.count; i++) {
                if (strcmp(g_anchors.items[i].rid, u->rid) == 0) {
                    free(g_anchors.items[i].name);
                    g_anchors.items[i].name = strdup(u->name);
                    save_cfg();
                    refresh_anchor_list();
                    break;
                }
            }
            free(u);
            return 0;
        }
        case WM_APP_CONNECTED:
            set_status_utf8(g_current_status[0] ? g_current_status : "● 直播中");
            return 0;
        case WM_APP_DISCONNECTED:
            if ((LONG)wp == g_fetch_gen) {  /* 仅当前连接的断开才更新状态 */
                set_status_utf8("已断开");
                g_connected = 0;
            }
            return 0;
        case WM_TIMER:
            if (wp == IDT_QUEUE) drain_queue();
            return 0;
        case WM_SIZE:
            layout_status_bar(hwnd);
            return 0;
        case WM_CLOSE:
            stop_fetch();
            save_cfg();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, IDT_QUEUE);
            DeleteObject(g_font_msg);
            DeleteObject(g_font_ui);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(void) {
    g_hinst = GetModuleHandleW(NULL);
    srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());  /* msToken 随机种子 */
    LoadLibraryW(L"msftedit.dll");  /* 富文本控件，支持彩色消息 */
    init_paths();
    InitializeCriticalSection(&g_q_cs);
    InitializeCriticalSection(&g_cookie_cs);
    InitializeCriticalSection(&g_arch_cs);
    anchor_list_init(&g_anchors);
    load_cfg();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DlmMain";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DlmMain", L"抖音直播弹幕",
                                WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                                CW_USEDEFAULT, CW_USEDEFAULT, 620, 640,
                                NULL, NULL, g_hinst, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    stop_fetch();
    save_cfg();
    anchor_list_free(&g_anchors);
    free(g_states);
    DeleteCriticalSection(&g_arch_cs);
    DeleteCriticalSection(&g_cookie_cs);
    DeleteCriticalSection(&g_q_cs);
    return 0;
}
