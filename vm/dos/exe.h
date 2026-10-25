/* MZ 格式（.EXE）装载。
 *
 * 和 .COM 的差别不只是多一个头：.EXE 的段是不连续的，代码段、数据段、栈段
 * 各自由头里的 e_cs / e_ss / 默认 DS 决定，所以必须"装载 + 重定位"两步走。
 *
 * 重定位表是一串 (段, 偏移)，每个条目指向映像里的一个 word，装载时要把
 * 装载段加到那个 word 上——这就是 .EXE 能在任意段地址装载的原因。
 */
#ifndef DOSPP_DOS_EXE_H
#define DOSPP_DOS_EXE_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"

/* .EXE 的装载段。跟 .COM 错开，免得两者混用时互相踩 */
#define DOS_EXE_SEG 0x0200u

/* MZ 头的字段偏移，都是 word */
#define MZ_MAGIC    0x00
#define MZ_CBLP     0x02
#define MZ_CP       0x04
#define MZ_CRLC     0x06
#define MZ_CPARHDR  0x08
#define MZ_MINALLOC 0x0A
#define MZ_MAXALLOC 0x0C
#define MZ_SS       0x0E
#define MZ_SP       0x10
#define MZ_CSUM     0x12
#define MZ_IP       0x14
#define MZ_CS       0x16
#define MZ_LFARLC   0x18
#define MZ_OVNO     0x1A

bool exe_load(cpu_regs *r, const memory *m, const u8 *image, u32 len, u16 seg);

#endif /* DOSPP_DOS_EXE_H */
