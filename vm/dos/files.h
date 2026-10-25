/* DOS 句柄表 + 目录查找状态。
 *
 * 句柄 0–4 是标准设备，由 INT 21h 直接处理，不进这张表。
 * 第一个可用句柄是 5，跟真 DOS 一致——有些程序会硬编码比较句柄值。
 */
#ifndef DOSPP_DOS_FILES_H
#define DOSPP_DOS_FILES_H

#include "dospp/types.h"
#include "dos/hostfs.h"
#include "cpu/mem.h"

#define DOS_HANDLES      20
#define DOS_FIRST_HANDLE 5

/* DOS 错误码，只列现在用到的 */
#define DOS_ERR_NOFILE   2
#define DOS_ERR_NOPATH   3
#define DOS_ERR_NOFILES  4
#define DOS_ERR_ACCESS   5
#define DOS_ERR_HANDLE   6
#define DOS_ERR_NOMORE   18   /* 没有更多文件了 */

/* FINDDATA 的字段偏移。这个结构是 DOS 的公开契约，
 * 程序会按偏移直接读，所以数字不能自己定 */
#define FIND_ATTR   21
#define FIND_TIME   22
#define FIND_DATE   24
#define FIND_SIZE   26
#define FIND_NAME   30
#define FIND_LEN    43

typedef struct {
    u8  used;
    u8  mode;           /* 0 读、1 写、2 读写 */
    host_handle host;
} dos_file;

typedef struct {
    dos_file h[DOS_HANDLES];
    hostfs_ops fs;

    /* DTA（磁盘传输区）。默认在 PSP:0080，程序可以用 AH=1A 改 */
    u32 dta_base;
    u16 dta_off;

    /* 目录查找的游标。存的是"上一次给到第几条" */
    char search_spec[128];
    int  search_idx;
    u8   search_active;
} dos_files;

void dos_files_init(dos_files *f, const hostfs_ops *fs);
void dos_files_set_dta(dos_files *f, u32 base, u16 off);

int  dos_file_open(dos_files *f, const char *dos_path, u8 mode, int *err);
void dos_file_close(dos_files *f, int handle);
int  dos_file_read(dos_files *f, int handle, void *buf, int len, int *err);
int  dos_file_write(dos_files *f, int handle, const void *buf, int len, int *err);

/* 找第一个/下一个。成功返回 1 并把 FINDDATA 写进 DTA；
 * 没有了返回 0；出错返回 -1 并给错误码 */
int dos_find_first(dos_files *f, const memory *m, const char *spec, u8 attr, int *err);
int dos_find_next(dos_files *f, const memory *m, int *err);

#endif /* DOSPP_DOS_FILES_H */
