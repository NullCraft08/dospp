#include "dos/files.h"

#include <cstdio>
#include <cstring>
#include <cctype>

void dos_files_init(dos_files *f, const hostfs_ops *fs)
{
    std::memset(f, 0, sizeof(*f));
    f->fs = *fs;
    for (int i = 0; i < DOS_HANDLES; ++i)
        f->h[i].host = -1;

    f->dta_off = 0x0080;      /* 默认 DTA 在 PSP:0080 */
    f->search_spec[0] = '\0';
}

void dos_files_set_dta(dos_files *f, u32 base, u16 off)
{
    f->dta_base = base;
    f->dta_off = off;
}

/* 通配符匹配，不区分大小写，只支持 * 和 ? */
static int wild_match(const char *pat, const char *name)
{
    while (*pat) {
        if (*pat == '*') {
            /* 星号吃掉后面所有，逐个位置试 */
            if (!pat[1]) return 1;
            for (const char *n = name; ; ++n) {
                if (wild_match(pat + 1, n)) return 1;
                if (!*n) return 0;
            }
        }
        if (!*name) return 0;
        if (*pat != '?' &&
            std::toupper((unsigned char)*pat) != std::toupper((unsigned char)*name))
            return 0;
        ++pat;
        ++name;
    }
    return *name == '\0';
}

/* 把 "DIR\*.TXT" 拆成目录 "DIR" 和掩码 "*.TXT"。
 * 只给目录不给文件名时，DOS 视作 *.* */
static void split_spec(const char *spec, char *dir, size_t dir_size, char *mask, size_t mask_size)
{
    const char *slash = std::strrchr(spec, '\\');
    const char *slash2 = std::strrchr(spec, '/');
    if (slash2 && (!slash || slash2 > slash)) slash = slash2;

    if (slash) {
        size_t n = (size_t)(slash - spec);
        if (n >= dir_size) n = dir_size - 1;
        std::memcpy(dir, spec, n);
        dir[n] = '\0';
        std::snprintf(mask, mask_size, "%s", slash + 1);
    } else {
        dir[0] = '\0';
        std::snprintf(mask, mask_size, "%s", spec);
    }

    if (mask[0] == '\0') std::snprintf(mask, mask_size, "*.*");
}

static void put_finddata(const dos_files *f, const memory *m,
                         const host_dirent &d, int idx)
{
    const u32 base = f->dta_base;
    const u32 off = f->dta_off;

    /* 前 21 字节 DOS 内部用，我们拿它存游标，正好和真 DOS 的用法一致 */
    for (int i = 0; i < 21; ++i)
        mem_write8(m, base, off + (u32)i, 0);
    mem_write8(m, base, off + 0, (u8)(idx & 0xFF));

    mem_write8(m, base, off + FIND_ATTR, (u8)(d.is_dir ? 0x10 : 0x20));

    /* 时间日期固定成 1990-01-01。真 DOS 会填文件时间，
     * 但宿主时间格式和 DOS 的编码差得远，先给个能看的值 */
    mem_write16(m, base, off + FIND_TIME, 0x0000);
    mem_write16(m, base, off + FIND_DATE, 0x1421);   /* 1990-01-01 */

    mem_write32(m, base, off + FIND_SIZE, d.size);

    /* 文件名：大写，最多 12 字节 + 结尾 0。长文件名先不转换 8.3，
     * 这会让某些程序看到超长名字，等真需要时再补 */
    char upper[16];
    std::snprintf(upper, sizeof(upper), "%s", d.name);
    for (char *q = upper; *q; ++q)
        *q = (char)std::toupper((unsigned char)*q);
    upper[12] = '\0';

    for (int i = 0; i <= 12; ++i) {
        char c = upper[i];
        mem_write8(m, base, off + FIND_NAME + (u32)i, (u8)c);
        if (c == '\0') break;
    }
}

/* 从 search_idx 开始找下一条命中的记录 */
static int scan_from(dos_files *f, const memory *m)
{
    char dir[128];
    char mask[32];
    split_spec(f->search_spec, dir, sizeof(dir), mask, sizeof(mask));

    host_dirent d;
    int rc;

    while ((rc = f->fs.readdir(f->fs.ctx, dir, f->search_idx, &d)) == 1) {
        ++f->search_idx;
        if (d.name[0] == '.' &&
            (d.name[1] == '\0' || (d.name[1] == '.' && d.name[2] == '\0')))
            continue;                    /* 跳过 . 和 .. */
        /* DOS 里没写扩展名的文件等价于"名字 + 空扩展名"。
         * 不补这个点，*.* 就会漏掉 SUBDIR 这种名字——真 DOS 是能匹配的 */
        char with_dot[80];
        const char *nm = d.name;
        if (!std::strchr(d.name, '.')) {
            std::snprintf(with_dot, sizeof(with_dot), "%s.", d.name);
            nm = with_dot;
        }

        if (!wild_match(mask, nm)) continue;
        put_finddata(f, m, d, f->search_idx - 1);
        return 1;
    }

    return rc < 0 ? -1 : 0;
}

int dos_find_first(dos_files *f, const memory *m, const char *spec, u8 attr, int *err)
{
    (void)attr;   /* 属性过滤先不做，全部都列 */

    if (!spec || !spec[0]) {
        if (err) *err = DOS_ERR_NOPATH;
        return -1;
    }

    std::snprintf(f->search_spec, sizeof(f->search_spec), "%s", spec);
    f->search_idx = 0;
    f->search_active = 1;

    int rc = scan_from(f, m);
    if (rc == 0) {
        /* FindFirst 一个都没找到，DOS 报的是"文件不存在"(2)，
         * 而不是 FindNext 那个"没有更多"(18)。这两个码程序会区别对待 */
        if (err) *err = DOS_ERR_NOFILE;
        return -1;
    }
    if (rc < 0 && err) *err = DOS_ERR_NOPATH;
    return rc;
}

int dos_find_next(dos_files *f, const memory *m, int *err)
{
    if (!f->search_active) {
        if (err) *err = DOS_ERR_NOFILE;
        return -1;
    }

    int rc = scan_from(f, m);
    if (rc == 0 && err) *err = DOS_ERR_NOMORE;
    if (rc < 0 && err) *err = DOS_ERR_NOPATH;
    return rc;
}

int dos_file_open(dos_files *f, const char *dos_path, u8 mode, int *err)
{
    for (int i = 0; i < DOS_HANDLES; ++i) {
        if (f->h[i].used) continue;

        host_handle host = f->fs.open(f->fs.ctx, dos_path, mode, err);
        if (host < 0) return -1;

        f->h[i].used = 1;
        f->h[i].mode = mode;
        f->h[i].host = host;
        return DOS_FIRST_HANDLE + i;
    }

    if (err) *err = DOS_ERR_NOFILES;
    return -1;
}

void dos_file_close(dos_files *f, int handle)
{
    const int i = handle - DOS_FIRST_HANDLE;
    if (i < 0 || i >= DOS_HANDLES || !f->h[i].used) return;

    f->fs.close(f->fs.ctx, f->h[i].host);
    f->h[i].used = 0;
    f->h[i].host = -1;
}

int dos_file_read(dos_files *f, int handle, void *buf, int len, int *err)
{
    const int i = handle - DOS_FIRST_HANDLE;
    if (i < 0 || i >= DOS_HANDLES || !f->h[i].used) {
        if (err) *err = DOS_ERR_HANDLE;
        return -1;
    }
    return f->fs.read(f->fs.ctx, f->h[i].host, buf, len);
}

int dos_file_write(dos_files *f, int handle, const void *buf, int len, int *err)
{
    const int i = handle - DOS_FIRST_HANDLE;
    if (i < 0 || i >= DOS_HANDLES || !f->h[i].used) {
        if (err) *err = DOS_ERR_HANDLE;
        return -1;
    }
    return f->fs.write(f->fs.ctx, f->h[i].host, buf, len);
}
