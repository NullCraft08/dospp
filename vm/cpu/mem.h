/* 实模式的 1 MiB 内存，外加段到线性地址的换算。
 *
 * 换算本身只有一行"基址 + 偏移"，但里面藏着两个必须照顾的特例：
 *
 *   1. 基址 + 偏移有可能超过 1 MiB。FFFF:FFFF 算出来是 0x10FFEF，
 *      而 8086 的地址线只有 20 根，多出来的部分会绕回低端。所以这儿
 *      统一按 20 位掩码处理——这是 8086 的原始行为，也是最不容易
 *      让老程序出错的默认。
 *
 *   2. A20 门那套是 286 之后才有的。等真需要访问 HMA（0x100000 以上）
 *      再单独处理，现在做了也没人用。
 *
 * 另外，这里不做越界检查：DOS_MEM_SIZE 就是 1 MiB，任何线性地址
 * 都已经被掩码压在这个范围内。要是哪天要模拟少于 1 MiB 的机器，
 * 得回来加检查。
 */
#ifndef DOSPP_CPU_MEM_H
#define DOSPP_CPU_MEM_H

#include "dospp/types.h"

#define DOS_MEM_SIZE  (1024u * 1024u)
#define DOS_ADDR_MASK (DOS_MEM_SIZE - 1u)

typedef struct {
    u8 *data;    /* 必须指向至少 DOS_MEM_SIZE 字节 */
    u32 size;    /* 目前只支持 DOS_MEM_SIZE */
} memory;

/* 基址 + 偏移，按 20 位地址线绕回 */
static inline u32 mem_linear(u32 base, u32 off)
{
    return (base + off) & DOS_ADDR_MASK;
}

static inline u8 mem_read8(const memory *m, u32 base, u32 off)
{
    return m->data[mem_linear(base, off)];
}

static inline void mem_write8(const memory *m, u32 base, u32 off, u8 v)
{
    m->data[mem_linear(base, off)] = v;
}

/* 16 位和 32 位按小端拼。x86 是小端，这个不用商量 */
static inline u16 mem_read16(const memory *m, u32 base, u32 off)
{
    return (u16)(mem_read8(m, base, off) | ((u16)mem_read8(m, base, off + 1) << 8));
}

static inline u32 mem_read32(const memory *m, u32 base, u32 off)
{
    return (u32)mem_read16(m, base, off)
         | ((u32)mem_read16(m, base, off + 2) << 16);
}

static inline void mem_write16(const memory *m, u32 base, u32 off, u16 v)
{
    mem_write8(m, base, off, (u8)(v & 0xFF));
    mem_write8(m, base, off + 1, (u8)((v >> 8) & 0xFF));
}

static inline void mem_write32(const memory *m, u32 base, u32 off, u32 v)
{
    mem_write16(m, base, off, (u16)(v & 0xFFFF));
    mem_write16(m, base, off + 2, (u16)((v >> 16) & 0xFFFF));
}

#endif /* DOSPP_CPU_MEM_H */
