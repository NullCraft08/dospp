/* 可执行文件装载。
 *
 * 只认两种格式：.COM（没有头，一坨代码）和 .EXE（MZ 头 + 重定位表）。
 * 用头两个字节区分，跟 DOS 的做法一致。
 */
#ifndef DOSPP_DOS_LOADER_H
#define DOSPP_DOS_LOADER_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"

/* .COM 的装载段，同时也是它的 PSP 段 */
#define DOS_COM_SEG    0x0100u
#define DOS_COM_START  0x0100u
#define DOS_COM_STACK  0xFFFEu

/* 自动识别格式并装载。成功返回 true，寄存器已经摆成"刚进程序"的状态 */
bool prog_load(cpu_regs *r, const memory *m, const u8 *image, u32 len);

/* 只装 .COM，测试里单独用 */
bool com_load(cpu_regs *r, const memory *m, const u8 *image, u32 len);

#endif /* DOSPP_DOS_LOADER_H */
