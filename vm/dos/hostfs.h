/* 宿主文件系统接口。
 *
 * INT 21h 不直接碰 stdio，而是走这层回调：真实运行接 stdio，
 * 测试接内存里的假文件系统。两边共用同一份 DOS 逻辑。
 */
#ifndef DOSPP_DOS_HOSTFS_H
#define DOSPP_DOS_HOSTFS_H

#include "dospp/types.h"

/* 宿主句柄用 intptr_t 而不是 int：底层很可能是 FILE* 这类指针，
 * 塞进 int 会在 64 位下被截断，症状是跑到一半突然访问违例 */
typedef intptr_t host_handle;

/* readdir 返回的一条记录 */
typedef struct {
    char name[64];
    u32  size;
    u8   is_dir;
} host_dirent;

typedef struct {
    void *ctx;

    host_handle (*open)(void *ctx, const char *path, int mode, int *err);
    int  (*read)(void *ctx, host_handle fh, void *buf, int len);
    int  (*write)(void *ctx, host_handle fh, const void *buf, int len);
    void (*close)(void *ctx, host_handle fh);

    /* 列举目录。idx 从 0 开始；找到返回 1，到底了返回 0，打不开返回 -1。
     * 用序号而不是迭代器，是因为 INT 21h 的 FindNext 本身就是"取下一个"的语义 */
    int  (*readdir)(void *ctx, const char *dir, int idx, host_dirent *out);

    /* 切换当前目录。0 成功，-1 失败 */
    int  (*chdir)(void *ctx, const char *path);
} hostfs_ops;

void hostfs_stdio_init(hostfs_ops *ops, const char *root);

#endif /* DOSPP_DOS_HOSTFS_H */
