/* INT 21h 的服务实现。
 *
 * 已实现：
 *   AH=01/08  读键（带回显/不回显）
 *   AH=1A     设置 DTA
 *   AH=3B     切换目录
 *   AH=3D/3E/3F/40  打开/关闭/读/写
 *   AH=4E/4F  找第一个/下一个
 *   AH=09     打印 $ 结尾字符串
 *   AH=4C     退出
 *
 * 约定：失败时置 CF 并把错误码放 AX，成功时清 CF。程序基本都靠 jc 判断。
 */
#ifndef DOSPP_DOS_INT21_H
#define DOSPP_DOS_INT21_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"
#include "dos/files.h"

typedef void (*vm_putc_fn)(void *ctx, char ch);

/* 返回 0..255；返回 -1 表示当前没有输入。真实运行接 stdin（会阻塞），
 * 测试接一个预先排好的队列 */
typedef int (*vm_getc_fn)(void *ctx);

typedef struct {
    vm_putc_fn putc;
    void *putc_ctx;
    vm_getc_fn getc;
    void *getc_ctx;
    dos_files *files;
} int21_env;

/* 返回 0：继续执行。返回 1：程序退出，退出码写进 *exit_code。 */
int int21_call(cpu_regs *r, const memory *m, int21_env *env, u8 *exit_code);

#endif /* DOSPP_DOS_INT21_H */
