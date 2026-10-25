/* 分页：线性地址 → 物理地址。
 *
 * 32 位两级页表：CR3 指向页目录（PDE），PDE 指向页表（PTE），PTE 指向页。
 * PDE 的 bit 7（PS）置位时是 4 MB 大页，省掉一级。
 *
 * 这里有个刻意的简化：**GDT、IDT 和页表本身的访问不做翻译**，直接当物理地址用。
 * 真机上 GDT/IDT 是线性地址、要走分页的。但我们的测试都用恒等映射，
 * 而且这块逻辑一改，异常处理路径自己就会先崩——先保住"能跑"再谈精确。
 */
#ifndef DOSPP_CPU_MMU_H
#define DOSPP_CPU_MMU_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"

/* #PF 错误码的位 */
#define PF_PRESENT  0x01u   /* 1 = 页存在但权限不够，0 = 页不存在 */
#define PF_WRITE    0x02u   /* 1 = 写操作引起 */
#define PF_USER     0x04u   /* 1 = 用户态访问引起 */

static inline bool mmu_enabled(const cpu_regs *r)
{
    return (r->cr0 & CR0_PG) && (r->cr0 & CR0_PE);
}

/* 返回 false 表示页错误，错误码和线性地址写进 cpu_regs */
static inline bool mmu_translate(cpu_regs *r, const memory *m, u32 lin, bool is_write, u32 *out)
{
    if (!mmu_enabled(r)) {
        *out = lin;
        return true;
    }

    const u32 dir_off = (lin >> 22) * 4u;
    const u32 pde = mem_read32(m, 0, (r->cr3 & ~0xFFFu) + dir_off);

    if (!(pde & 1u)) {                       /* 页目录项不存在 */
        r->pf_fault = 1;
        r->pf_lin = lin;
        r->pf_err = (u32)((is_write ? PF_WRITE : 0u) | ((r->cs.sel & 3) ? PF_USER : 0u));
        return false;
    }

    /* 4 MB 大页：直接由 PDE 给出物理基址 */
    if (pde & 0x80u) {
        if (is_write && !(pde & 2u)) {
            r->pf_fault = 1; r->pf_lin = lin;
            r->pf_err = PF_PRESENT | PF_WRITE | ((r->cs.sel & 3) ? PF_USER : 0u);
            return false;
        }
        *out = (pde & 0xFFC00000u) | (lin & 0x003FFFFFu);
        return true;
    }

    const u32 pte_addr = (pde & ~0xFFFu) + (((lin >> 12) & 0x3FFu) * 4u);
    const u32 pte = mem_read32(m, 0, pte_addr);

    if (!(pte & 1u)) {
        r->pf_fault = 1;
        r->pf_lin = lin;
        r->pf_err = (u32)((is_write ? PF_WRITE : 0u) | ((r->cs.sel & 3) ? PF_USER : 0u));
        return false;
    }

    if (is_write && !(pte & 2u)) {           /* 只读页上写 */
        r->pf_fault = 1; r->pf_lin = lin;
        r->pf_err = PF_PRESENT | PF_WRITE | ((r->cs.sel & 3) ? PF_USER : 0u);
        return false;
    }

    *out = (pte & ~0xFFFu) | (lin & 0xFFFu);
    return true;
}

/* ---- 走分页的访问器。出错时返回 0 并把状态记进 cpu_regs ---- */

static inline u8 mmu_r8(cpu_regs *r, const memory *m, u32 lin)
{
    u32 phys;
    if (!mmu_translate(r, m, lin, false, &phys)) return 0;
    return m->data[phys & DOS_ADDR_MASK];
}

static inline void mmu_w8(cpu_regs *r, const memory *m, u32 lin, u8 v)
{
    u32 phys;
    if (!mmu_translate(r, m, lin, true, &phys)) return;
    m->data[phys & DOS_ADDR_MASK] = v;
}

static inline u16 mmu_r16(cpu_regs *r, const memory *m, u32 lin)
{
    return (u16)(mmu_r8(r, m, lin) | ((u16)mmu_r8(r, m, lin + 1) << 8));
}

static inline void mmu_w16(cpu_regs *r, const memory *m, u32 lin, u16 v)
{
    mmu_w8(r, m, lin, (u8)(v & 0xFF));
    mmu_w8(r, m, lin + 1, (u8)(v >> 8));
}

static inline u32 mmu_r32(cpu_regs *r, const memory *m, u32 lin)
{
    return (u32)mmu_r16(r, m, lin) | ((u32)mmu_r16(r, m, lin + 2) << 16);
}

static inline void mmu_w32(cpu_regs *r, const memory *m, u32 lin, u32 v)
{
    mmu_w16(r, m, lin, (u16)(v & 0xFFFF));
    mmu_w16(r, m, lin + 2, (u16)(v >> 16));
}

#endif /* DOSPP_CPU_MMU_H */
