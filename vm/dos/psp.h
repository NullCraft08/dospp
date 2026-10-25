/* PSP（程序段前缀）。
 *
 * 每个 DOS 程序启动时，DS/ES 指向自己那 256 字节的 PSP。程序不一定会用，
 * 但有些会：比如 0x2C 取环境段、0x80 读命令行。这些是公开契约，
 * 偏移错了程序就会读到垃圾。
 */
#ifndef DOSPP_DOS_PSP_H
#define DOSPP_DOS_PSP_H

#include "dospp/types.h"
#include "cpu/mem.h"

#define PSP_SIZE      0x0100
#define PSP_INT20     0x0000   /* CD 20 */
#define PSP_MEMTOP    0x0002   /* 可用内存顶端（段） */
#define PSP_ENVSEG    0x002C   /* 环境段 */
#define PSP_CALL5C    0x0050   /* CD 21 CB，老程序用它间接调 INT 21h */
#define PSP_FCB1      0x005C
#define PSP_FCB2      0x006C
#define PSP_CMDLINE   0x0080

/* 1 MiB 内存的顶端，用段表示 */
#define PSP_MEM_TOP   0xFFFFu

/* 在 seg 处铺一个最小可用的 PSP */
void psp_build(const memory *m, u16 seg);

#endif /* DOSPP_DOS_PSP_H */
