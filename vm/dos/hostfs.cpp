#include "dos/hostfs.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

/* 只支持一个实例。真需要多虚拟机各自一个根目录时，
 * 把这儿换成按 ctx 分配的结构，接口不用动 */
static char g_root[512];
static char g_cwd[512];

/* 目录快照。FindFirst 时拍一张，FindNext 按序号取。
 * 不直接重复遍历是有原因的：目录迭代顺序不保证稳定，
 * 重复遍历会让"下一个"跳到别的条目甚至重复 */
static std::vector<host_dirent> g_dir;
static char g_dir_path[512];

static void to_slash(char *s)
{
    for (; *s; ++s)
        if (*s == '\\') *s = '/';
}

/* DOS 路径 → 宿主路径。处理盘符、反斜杠、以及当前目录前缀 */
static void translate(const char *dos_path, char *out, size_t out_size)
{
    const char *s = dos_path;

    if (s[0] && s[1] == ':') s += 2;          /* 去掉 X: */
    while (*s == '\\' || *s == '/') ++s;      /* 去掉前导分隔符 */

    if (g_cwd[0])
        std::snprintf(out, out_size, "%s/%s/%s", g_root, g_cwd, s);
    else
        std::snprintf(out, out_size, "%s/%s", g_root, s);

    to_slash(out);
}

static std::FILE *open_in_mode(const char *path, int mode)
{
    const char *m = (mode == 1) ? "wb" : (mode == 2 ? "r+b" : "rb");
    return std::fopen(path, m);
}

static host_handle stdio_open(void *ctx, const char *path, int mode, int *err)
{
    (void)ctx;

    char host[1024];
    translate(path, host, sizeof(host));

    std::FILE *f = open_in_mode(host, mode);
    if (!f) {
        /* 大小写变体各试一次：DOS 名不区分大小写，宿主（尤其 Linux）区分 */
        std::string up(host), lo(host);
        for (auto &c : up) c = (char)std::toupper((unsigned char)c);
        for (auto &c : lo) c = (char)std::tolower((unsigned char)c);
        f = open_in_mode(up.c_str(), mode);
        if (!f) f = open_in_mode(lo.c_str(), mode);
    }

    if (!f) {
        if (err) *err = 2;   /* 文件没找到。路径错和文件不存在先都报这个 */
        return -1;
    }
    return (host_handle)(intptr_t)f;
}

static int stdio_read(void *ctx, host_handle fh, void *buf, int len)
{
    (void)ctx;
    return (int)std::fread(buf, 1, (size_t)len, (std::FILE *)(intptr_t)fh);
}

static int stdio_write(void *ctx, host_handle fh, const void *buf, int len)
{
    (void)ctx;
    return (int)std::fwrite(buf, 1, (size_t)len, (std::FILE *)(intptr_t)fh);
}

static void stdio_close(void *ctx, host_handle fh)
{
    (void)ctx;
    std::fclose((std::FILE *)(intptr_t)fh);
}

static int stdio_readdir(void *ctx, const char *dir, int idx, host_dirent *out)
{
    (void)ctx;

    char host[1024];
    translate(dir, host, sizeof(host));

    /* 换了目录就重拍一张快照 */
    if (std::strcmp(g_dir_path, host) != 0) {
        g_dir.clear();
        std::snprintf(g_dir_path, sizeof(g_dir_path), "%s", host);

        std::error_code ec;
        fs::directory_iterator it(host, ec);
        if (ec) return -1;

        for (const auto &e : it) {
            host_dirent d{};
            std::string name = e.path().filename().string();
            std::snprintf(d.name, sizeof(d.name), "%s", name.c_str());
            d.is_dir = (u8)e.is_directory(ec);
            d.size = d.is_dir ? 0 : (u32)e.file_size(ec);
            g_dir.push_back(d);
        }
    }

    if (idx < 0 || idx >= (int)g_dir.size()) return 0;
    *out = g_dir[(size_t)idx];
    return 1;
}

static int stdio_chdir(void *ctx, const char *path)
{
    (void)ctx;

    char host[1024];
    translate(path, host, sizeof(host));

    std::error_code ec;
    if (!fs::is_directory(host, ec)) return -1;

    /* 只记相对根目录的那一段，省得每次拼绝对路径 */
    std::snprintf(g_cwd, sizeof(g_cwd), "%s", path);
    for (char *q = g_cwd; *q; ++q)
        if (*q == '\\' || *q == '/') *q = '/';
    while (g_cwd[0] == '/') std::memmove(g_cwd, g_cwd + 1, std::strlen(g_cwd));

    g_dir_path[0] = '\0';
    return 0;
}

void hostfs_stdio_init(hostfs_ops *ops, const char *root)
{
    std::snprintf(g_root, sizeof(g_root), "%s", root);
    to_slash(g_root);
    g_cwd[0] = '\0';
    g_dir_path[0] = '\0';
    g_dir.clear();

    ops->ctx = nullptr;
    ops->open = stdio_open;
    ops->read = stdio_read;
    ops->write = stdio_write;
    ops->close = stdio_close;
    ops->readdir = stdio_readdir;
    ops->chdir = stdio_chdir;
}
