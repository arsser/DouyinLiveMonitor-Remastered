/* 抖音直播弹幕 CLI（C 重写版） */
#include "fetcher.h"
#include "json.h"
#include "enter.h"
#include "utf8_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <wchar.h>
#include <direct.h>
#include <sys/stat.h>

static char g_anchor_name[256];
static char g_live_id[64];
static char g_rid_folder[512];
static char g_only[512];   /* --only 过滤：逗号分隔的消息类型，空 = 全部 */

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

/* 读取文件全部内容，返回 malloc 缓冲（NULL=无文件/失败） */
static char *read_file(const char *path) {
    FILE *f = fopen_utf8(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0) fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int write_file(const char *path, const char *data) {
    FILE *f = fopen_utf8(path, "wb");
    if (!f) return -1;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return 0;
}

/* 递归创建目录（UTF-8 路径，见 utf8_file.h 的 mkdir_p_utf8） */

/* 去除 Windows 文件名非法字符 */
static void sanitize_filename(char *s) {
    for (char *p = s; *p; p++) {
        if (*p == '<' || *p == '>' || *p == ':' || *p == '"' ||
            *p == '/' || *p == '\\' || *p == '|' || *p == '?' || *p == '*') {
            *p = '_';
        }
    }
}

/* 配置读写 */
static void load_config(anchor_list_t *al, char **cookie) {
    anchor_list_init(al);
    if (cookie) *cookie = NULL;
    char *text = read_file(g_config_path);
    if (text) {
        json_load_config(text, al, cookie);
        free(text);
    }
    if (cookie && !*cookie) *cookie = strdup("");
}

static void save_config(const anchor_list_t *al, const char *cookie) {
    char *out = json_save_config(al, cookie ? cookie : "");
    write_file(g_config_path, out ? out : "{}");
    free(out);
}

/* 消息类型过滤：g_only 为空则全部通过 */
static int type_allowed(const char *type) {
    if (!g_only[0]) return 1;
    size_t tlen = strlen(type);
    const char *p = g_only;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == tlen && strncmp(p, type, len) == 0) return 1;
        if (!comma) break;
        p = comma + 1;
    }
    return 0;
}

/* 消息回调：打印 + 存档 */
static void on_message(void *ud, const char *type, const char *time_str, const char *text) {
    (void)ud;
    if (!type_allowed(type)) return;
    printf("%s\n", text);
    fflush(stdout);

    /* 存档：剥掉 "[ts] " 前缀 */
    const char *stripped = text;
    size_t ts_len = strlen(time_str);
    if (strncmp(text, "[", 1) == 0 && strlen(text) > ts_len + 2 &&
        strncmp(text + 1, time_str, ts_len) == 0 && strncmp(text + ts_len + 1, "] ", 2) == 0) {
        stripped = text + ts_len + 3;
    }

    char date[16];
    time_t now = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);

    char dir[1200], path[1300];
    snprintf(dir, sizeof(dir), "%s\\%s", g_data_dir, g_rid_folder);
    snprintf(path, sizeof(path), "%s\\%s.jsonl", dir, date);

    static char cur_path[1300];
    static int path_inited = 0;
    if (!path_inited || strcmp(cur_path, path) != 0) {
        snprintf(cur_path, sizeof(cur_path), "%s", path);
        mkdir_p_utf8(dir);
        path_inited = 1;
    }

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
}

/* 用 PotPlayer 播放 URL，返回 0=成功，-1=未找到 */
static int play_with_potplayer(const char *url) {
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

static void print_usage(const char *prog) {
    printf("用法:\n");
    printf("  %s <直播间ID> [--only chat,gift]   连接直播间，抓取弹幕\n", prog);
    printf("  %s add <ID> [名称]                 添加主播（名称留空自动获取昵称）\n", prog);
    printf("  %s del <ID>                        删除主播\n", prog);
    printf("  %s list                            列出主播\n", prog);
    printf("  %s check <ID>                      检测单个主播开播状态\n", prog);
    printf("  %s check                           一键检测所有主播\n", prog);
    printf("  %s stream <ID>                     打印直播流直链\n", prog);
    printf("  %s play <ID>                       提取最佳清晰度并用 PotPlayer 播放\n", prog);
    printf("  %s cookie <Cookie>                 设置 Cookie\n", prog);
    printf("  --only 类型列表：chat,gift,like,member,social,stats,fansclub,emoji,room,roomstats,control\n");
}

static int cmd_list(void) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);
    printf("主播列表（%zu）:\n", al.count);
    for (size_t i = 0; i < al.count; i++) {
        printf("  %s  (%s)\n", al.items[i].name, al.items[i].rid);
    }
    free(cookie); anchor_list_free(&al);
    return 0;
}

static int cmd_add(const char *rid, const char *name) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);

    char resolved[256];
    if (name && name[0]) {
        snprintf(resolved, sizeof(resolved), "%s", name);
    } else {
        printf("正在获取主播昵称...\n");
        room_info_t info;
        if (enter_fetch(rid, cookie, &info) == 0 && info.nickname[0]) {
            snprintf(resolved, sizeof(resolved), "%s", info.nickname);
        } else {
            snprintf(resolved, sizeof(resolved), "%s", rid);
            printf("获取昵称失败，使用 ID 作为名称\n");
        }
    }

    anchor_list_set(&al, rid, resolved);
    save_config(&al, cookie);
    free(cookie); anchor_list_free(&al);
    printf("已添加主播: %s (%s)\n", resolved, rid);
    return 0;
}

static int cmd_del(const char *rid) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);
    anchor_list_t nl; anchor_list_init(&nl);
    for (size_t i = 0; i < al.count; i++) {
        if (strcmp(al.items[i].rid, rid) != 0) anchor_list_set(&nl, al.items[i].rid, al.items[i].name);
    }
    save_config(&nl, cookie);
    free(cookie); anchor_list_free(&al); anchor_list_free(&nl);
    printf("已删除主播: %s\n", rid);
    return 0;
}

static int cmd_set_cookie(const char *cookie) {
    anchor_list_t al; char *old = NULL;
    load_config(&al, &old);
    save_config(&al, cookie ? cookie : old);
    free(old); anchor_list_free(&al);
    printf("Cookie 已保存\n");
    return 0;
}

static int cmd_check_one(const char *rid) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);
    room_info_t info;
    if (enter_fetch(rid, cookie, &info) != 0) {
        printf("获取直播间信息失败（可能需要 Cookie）\n");
        free(cookie); anchor_list_free(&al);
        return -1;
    }
    const char *name = anchor_list_name(&al, rid);
    const char *disp = info.nickname[0] ? info.nickname : (name ? name : rid);
    if (info.is_live) {
        printf("[直播] %s (%s) - %s 人观看 - %s\n", disp, rid, info.user_count, info.title);
    } else {
        printf("[未开] %s (%s)\n", disp, rid);
    }
    free(cookie); anchor_list_free(&al);
    return 0;
}

static int cmd_check_all(void) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);
    if (al.count == 0) { printf("主播列表为空，先 add 添加\n"); free(cookie); anchor_list_free(&al); return 0; }
    for (size_t i = 0; i < al.count; i++) {
        room_info_t info;
        printf("检测 %s (%s) ... ", al.items[i].name, al.items[i].rid);
        fflush(stdout);
        if (enter_fetch(al.items[i].rid, cookie, &info) != 0) { printf("失败\n"); continue; }
        if (info.is_live) printf("[直播] %s 人观看 - %s\n", info.user_count, info.title);
        else printf("[未开]\n");
    }
    free(cookie); anchor_list_free(&al);
    return 0;
}

static int cmd_stream(const char *rid) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);
    room_info_t info;
    if (enter_fetch(rid, cookie, &info) != 0) {
        printf("获取直播间信息失败\n");
        free(cookie); anchor_list_free(&al); return -1;
    }
    const char *disp = info.nickname[0] ? info.nickname : rid;
    if (!info.is_live) {
        printf("主播「%s」未开播，无直播流\n", disp);
        free(cookie); anchor_list_free(&al); return 0;
    }
    if (info.stream_count == 0) {
        printf("未获取到直播流（可能需要 Cookie）\n");
        free(cookie); anchor_list_free(&al); return -1;
    }
    printf("主播「%s」直播流（%s 人观看）:\n", disp, info.user_count);
    for (size_t i = 0; i < info.stream_count; i++) {
        printf("  %-8s  %s\n", info.streams[i].name, info.streams[i].url);
    }
    free(cookie); anchor_list_free(&al);
    return 0;
}

static int cmd_play(const char *rid) {
    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);
    room_info_t info;
    if (enter_fetch(rid, cookie, &info) != 0) {
        printf("获取直播间信息失败\n");
        free(cookie); anchor_list_free(&al); return -1;
    }
    const char *disp = info.nickname[0] ? info.nickname : rid;
    if (!info.is_live) {
        printf("主播「%s」未开播\n", disp);
        free(cookie); anchor_list_free(&al); return 0;
    }
    const char *best = room_best_stream(&info);
    if (!best) {
        printf("未获取到直播流\n");
        free(cookie); anchor_list_free(&al); return -1;
    }
    if (play_with_potplayer(best) == 0) {
        printf("已用 PotPlayer 播放: %s\n", best);
    } else {
        printf("未找到 PotPlayer，直链如下：\n%s\n", best);
    }
    free(cookie); anchor_list_free(&al);
    return 0;
}

static int run_fetch(const char *rid, const char *only) {
    if (only && only[0]) snprintf(g_only, sizeof(g_only), "%s", only);
    else g_only[0] = '\0';

    anchor_list_t al; char *cookie = NULL;
    load_config(&al, &cookie);

    snprintf(g_live_id, sizeof(g_live_id), "%s", rid);
    const char *name = anchor_list_name(&al, rid);
    snprintf(g_anchor_name, sizeof(g_anchor_name), "%s", name ? name : rid);
    char folder_name[300];
    snprintf(folder_name, sizeof(folder_name), "%s（%s）", g_anchor_name, rid);
    sanitize_filename(folder_name);
    snprintf(g_rid_folder, sizeof(g_rid_folder), "%s", folder_name);

    fetcher_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.live_id, sizeof(cfg.live_id), "%s", rid);
    snprintf(cfg.cookie, sizeof(cfg.cookie), "%s", cookie ? cookie : "");
    cfg.on_message = on_message;

    printf("【控制台模式】正在连接直播间 %s (room: %s) ...\n", rid, g_anchor_name);
    if (cookie && cookie[0]) printf("已加载 Cookie（%zu 字符）\n", strlen(cookie));
    else printf("未配置 Cookie（礼物等需登录的消息不可用）\n");
    if (g_only[0]) printf("消息过滤: %s\n", g_only);
    int rc = fetcher_run(&cfg);

    free(cookie);
    anchor_list_free(&al);
    return rc;
}

int main(int argc, char **argv) {
    srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    init_paths();

    if (argc < 2) {
        print_usage(argv[0]);
        cmd_list();
        return 0;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "list") == 0) return cmd_list();
    if (strcmp(cmd, "add") == 0 && argc >= 3) return cmd_add(argv[2], argc >= 4 ? argv[3] : NULL);
    if (strcmp(cmd, "del") == 0 && argc >= 3) return cmd_del(argv[2]);
    if (strcmp(cmd, "cookie") == 0 && argc >= 3) return cmd_set_cookie(argv[2]);
    if (strcmp(cmd, "check") == 0) return argc >= 3 ? cmd_check_one(argv[2]) : cmd_check_all();
    if (strcmp(cmd, "stream") == 0 && argc >= 3) return cmd_stream(argv[2]);
    if (strcmp(cmd, "play") == 0 && argc >= 3) return cmd_play(argv[2]);
    if (strcmp(cmd, "console") == 0 && argc >= 3) return run_fetch(argv[2], NULL);

    /* 默认：<ID> [--only types] */
    const char *only = NULL;
    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--only=", 7) == 0) only = argv[i] + 7;
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = argv[++i];
    }
    return run_fetch(argv[1], only);
}
