/* 取指和 ModRM 解码。
 *
 * 这里只把字节变成结构，一行都不执行——执行在 exec.cpp。
 * 分开的理由很实际：以后加 386 的 32 位寻址和 SIB，只动这个文件，
 * exec 那边基本不用碰。要是混在一起，改寻址就得重新审一遍执行逻辑。
 */
#ifndef DOSPP_CPU_DECODE_H
#define DOSPP_CPU_DECODE_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"
#include "cpu/mmu.h"

/* 段寄存器编号。用序号而不是指针，这样 ModRM 的解析结果不用依赖 cpu_regs */
enum { SEG_ES, SEG_CS, SEG_SS, SEG_DS, SEG_FS, SEG_GS, SEG_NONE = 0xFF };

typedef struct {
    u8 seg;        /* SEG_* 或 SEG_NONE */
    u8 opsize32;   /* 0x66：操作数变 32 位 */
    u8 adsize32;   /* 0x67：寻址变 32 位（还没做） */
    u8 rep;        /* 0xF2 / 0xF3，先记着，等字符串指令再说 */
    u8 lock;
    u8 count;      /* 吃了几个前缀字节，算指令长度时有用 */
} prefixes;

typedef struct {
    u8 mod, reg, rm;
    u8 is_reg;     /* mod == 3，操作数是寄存器 */
    u8 seg;        /* 内存操作数走哪个段，BP 特例和前缀都处理过了 */
    u32 disp;      /* 位移，已符号扩展 */
    u32 ea;        /* 16 位寻址算出来的偏移 */
} modrm;

/* 取指。实模式下 IP 在段内 64K 回绕，所以每次都要掩码 */
static inline u8 ip_read8(const memory *m, cpu_regs *r)
{
    u16 off = (u16)r->eip;
    r->eip = (r->eip + 1) & 0xFFFF;
    /* 取指也要过页表：程序跑到没映射的地方应该 #PF，
     * 而不是悄悄读到内存里的垃圾指令 */
    return mmu_r8(r, m, r->cs.base + off);
}

static inline u16 ip_read16(const memory *m, cpu_regs *r)
{
    u16 lo = ip_read8(m, r);
    u16 hi = ip_read8(m, r);
    return (u16)(lo | (hi << 8));
}

static inline u32 ip_read32(const memory *m, cpu_regs *r)
{
    u32 lo = ip_read16(m, r);
    u32 hi = ip_read16(m, r);
    return lo | (hi << 16);
}

/* 寄存器编号 → 值。0=ax 1=cx 2=dx 3=bx 4=sp 5=bp 6=si 7=di。
 * 8 位有个坑：4..7 不是 sp/bp/si/di，而是 ah/ch/dh/bh。
 * 8086 当年就这么排的，看着别扭但改不了。 */
static inline u16 reg16_get(const cpu_regs *r, int i)
{
    switch (i) {
    case 0: return reg_ax(r);
    case 1: return reg_cx(r);
    case 2: return reg_dx(r);
    case 3: return reg_bx(r);
    case 4: return (u16)(r->esp & 0xFFFF);
    case 5: return (u16)(r->ebp & 0xFFFF);
    case 6: return (u16)(r->esi & 0xFFFF);
    case 7: return (u16)(r->edi & 0xFFFF);
    }
    return 0;
}

static inline void reg16_set(cpu_regs *r, int i, u16 v)
{
    switch (i) {
    case 0: reg_set_ax(r, v); break;
    case 1: reg_set_cx(r, v); break;
    case 2: reg_set_dx(r, v); break;
    case 3: reg_set_bx(r, v); break;
    case 4: r->esp = (r->esp & 0xFFFF0000u) | v; break;
    case 5: r->ebp = (r->ebp & 0xFFFF0000u) | v; break;
    case 6: r->esi = (r->esi & 0xFFFF0000u) | v; break;
    case 7: r->edi = (r->edi & 0xFFFF0000u) | v; break;
    }
}

static inline u32 reg32_get(const cpu_regs *r, int i)
{
    switch (i) {
    case 0: return r->eax;
    case 1: return r->ecx;
    case 2: return r->edx;
    case 3: return r->ebx;
    case 4: return r->esp;
    case 5: return r->ebp;
    case 6: return r->esi;
    case 7: return r->edi;
    }
    return 0;
}

static inline void reg32_set(cpu_regs *r, int i, u32 v)
{
    switch (i) {
    case 0: r->eax = v; break;
    case 1: r->ecx = v; break;
    case 2: r->edx = v; break;
    case 3: r->ebx = v; break;
    case 4: r->esp = v; break;
    case 5: r->ebp = v; break;
    case 6: r->esi = v; break;
    case 7: r->edi = v; break;
    }
}

static inline u8 reg8_get(const cpu_regs *r, int i)
{
    switch (i) {
    case 0: return reg_al(r);
    case 1: return reg_cl(r);
    case 2: return reg_dl(r);
    case 3: return reg_bl(r);
    case 4: return reg_ah(r);
    case 5: return reg_ch(r);
    case 6: return reg_dh(r);
    case 7: return reg_bh(r);
    }
    return 0;
}

static inline void reg8_set(cpu_regs *r, int i, u8 v)
{
    switch (i) {
    case 0: reg_set_al(r, v); break;
    case 1: reg_set_cl(r, v); break;
    case 2: reg_set_dl(r, v); break;
    case 3: reg_set_bl(r, v); break;
    case 4: reg_set_ah(r, v); break;
    case 5: reg_set_ch(r, v); break;
    case 6: reg_set_dh(r, v); break;
    case 7: reg_set_bh(r, v); break;
    }
}

/* 解析 ModRM 和它后面的位移。
 *
 * 16 位寻址只有 8 种组合，照着 8086 的表填就行。有两个地方容易错：
 *   - mod=00 且 rm=110 不是 [bp]，而是 disp16 直接寻址；
 *   - 基址是 bp 的默认走 SS 段，其余走 DS。
 */
static inline void decode_modrm(const memory *m, cpu_regs *r, const prefixes *p, modrm *mo)
{
    u8 b = ip_read8(m, r);
    mo->mod = (u8)((b >> 6) & 3);
    mo->reg = (u8)((b >> 3) & 7);
    mo->rm  = (u8)(b & 7);
    mo->seg = SEG_DS;
    mo->disp = 0;
    mo->ea = 0;
    mo->is_reg = (u8)(mo->mod == 3);
    if (mo->is_reg) return;

    if (mo->mod == 1) {
        mo->disp = (u32)(i8)ip_read8(m, r);
    } else if (mo->mod == 2) {
        mo->disp = (u32)ip_read16(m, r);
    }

    if (mo->mod == 0 && mo->rm == 6) {
        /* 直接寻址：只有 disp16 */
        mo->disp = (u32)ip_read16(m, r);
        mo->seg = SEG_DS;
        mo->ea = mo->disp;
    } else {
        u32 base = 0;
        switch (mo->rm) {
        case 0: base = (u32)reg16_get(r, 3) + reg16_get(r, 6); mo->seg = SEG_DS; break; /* bx+si */
        case 1: base = (u32)reg16_get(r, 3) + reg16_get(r, 7); mo->seg = SEG_DS; break; /* bx+di */
        case 2: base = (u32)reg16_get(r, 5) + reg16_get(r, 6); mo->seg = SEG_SS; break; /* bp+si */
        case 3: base = (u32)reg16_get(r, 5) + reg16_get(r, 7); mo->seg = SEG_SS; break; /* bp+di */
        case 4: base = reg16_get(r, 6);                        mo->seg = SEG_DS; break; /* si */
        case 5: base = reg16_get(r, 7);                        mo->seg = SEG_DS; break; /* di */
        case 6: base = reg16_get(r, 5);                        mo->seg = SEG_SS; break; /* bp */
        case 7: base = reg16_get(r, 3);                        mo->seg = SEG_DS; break; /* bx */
        }
        mo->ea = base + mo->disp;
    }

    if (p->seg != SEG_NONE) mo->seg = p->seg;
}

#endif /* DOSPP_CPU_DECODE_H */
