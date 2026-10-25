/* i486 的寄存器组。
 *
 * 先说个容易踩的坑：段寄存器不是一个 16 位数。它有"看得见"和"看不见"
 * 两半——看得见的是选择子，看不见的是 base/limit/权限，也就是描述符缓存。
 * 实模式和保护模式的区别，本质就是往这两半里填什么。
 *
 * 所以段寄存器从一开始就用 seg_reg 这个结构，别图省事写裸 u16。
 * 等以后加保护模式再返工，等于把整个 CPU 核重写。
 */
#ifndef DOSPP_CPU_REGS_H
#define DOSPP_CPU_REGS_H

#include "dospp/types.h"

typedef struct {
    u16 sel;     /* 看得见的那半：选择子，实模式里就是段值 */
    u32 base;    /* 看不见的那半：段基址 */
    u32 limit;   /* 段限长 */
    u16 attr;    /* 访问权限、D/B 位这些标志 */
} seg_reg;

typedef struct {
    /* 通用寄存器统一按 32 位存，16 位和 8 位的视图用下面的函数取。
     * i486 的 eax 本来就这么宽，先按 8086 只写低 16 位，后面还是要全改 */
    u32 eax, ebx, ecx, edx;
    u32 esi, edi, ebp, esp;
    u32 eip;
    u32 eflags;

    seg_reg cs, ds, es, ss, fs, gs;

    /* 386 的控制寄存器。眼下只有 CR0 的 PE 位真的有用：
     * 它一位决定"mov ds, ax"到底是乘 16 还是查描述符 */
    u32 cr0, cr2, cr3, cr4;

    /* 描述符表的基址和限长，由 LGDT / LIDT 填 */
    u32 gdt_base;
    u16 gdt_limit;
    u32 idt_base;
    u16 idt_limit;

    /* LDT 和 TSS 的选择子，以及在 LTR / LLDT 时解析出来的位置。
     * 这两个表的描述符本身住在 GDT 里——所以要先用 GDT 查到它们，
     * 之后别人的选择子才能指向它们内部的条目 */
    u16 ldtr;
    u32 ldt_base;
    u16 ldt_limit;

    u16 tr;
    u32 tss_base;
    u16 tss_limit;

    /* 本指令执行期间发生的页错误。延迟到指令跑完再投递，
     * 这样指令边界是清晰的——真机是立即中止，我们在教学精度上够用 */
    u8  pf_fault;
    u32 pf_lin;
    u32 pf_err;
} cpu_regs;

/* EFLAGS 里我们用到的几位 */
#define EFLAGS_CF   0x00000001u
#define EFLAGS_ZF   0x00000040u
#define EFLAGS_IF   0x00000200u
#define EFLAGS_IOPL_SHIFT 12
#define EFLAGS_IOPL_MASK  0x00003000u
#define EFLAGS_VM   0x00020000u   /* 虚拟 8086 模式 */

/* CR0 的几位。现在只关心 PE——置 1 就进保护模式 */
#define CR0_PE  (1u << 0)
#define CR0_MP  (1u << 1)
#define CR0_EM  (1u << 2)
#define CR0_TS  (1u << 3)
#define CR0_PG  (1u << 31)

/* ---- 16 位视图 ---- */
static inline u16 reg_ax(const cpu_regs *r) { return (u16)(r->eax & 0xFFFF); }
static inline u16 reg_bx(const cpu_regs *r) { return (u16)(r->ebx & 0xFFFF); }
static inline u16 reg_cx(const cpu_regs *r) { return (u16)(r->ecx & 0xFFFF); }
static inline u16 reg_dx(const cpu_regs *r) { return (u16)(r->edx & 0xFFFF); }

static inline void reg_set_ax(cpu_regs *r, u16 v) { r->eax = (r->eax & 0xFFFF0000u) | v; }
static inline void reg_set_bx(cpu_regs *r, u16 v) { r->ebx = (r->ebx & 0xFFFF0000u) | v; }
static inline void reg_set_cx(cpu_regs *r, u16 v) { r->ecx = (r->ecx & 0xFFFF0000u) | v; }
static inline void reg_set_dx(cpu_regs *r, u16 v) { r->edx = (r->edx & 0xFFFF0000u) | v; }

/* ---- 8 位视图：只给 ax/bx/cx/dx 这四组，因为 8086 就只有它们能拆 ---- */
static inline u8 reg_al(const cpu_regs *r) { return (u8)(r->eax & 0xFF); }
static inline u8 reg_ah(const cpu_regs *r) { return (u8)((r->eax >> 8) & 0xFF); }
static inline u8 reg_bl(const cpu_regs *r) { return (u8)(r->ebx & 0xFF); }
static inline u8 reg_bh(const cpu_regs *r) { return (u8)((r->ebx >> 8) & 0xFF); }
static inline u8 reg_cl(const cpu_regs *r) { return (u8)(r->ecx & 0xFF); }
static inline u8 reg_ch(const cpu_regs *r) { return (u8)((r->ecx >> 8) & 0xFF); }
static inline u8 reg_dl(const cpu_regs *r) { return (u8)(r->edx & 0xFF); }
static inline u8 reg_dh(const cpu_regs *r) { return (u8)((r->edx >> 8) & 0xFF); }

static inline void reg_set_al(cpu_regs *r, u8 v) { r->eax = (r->eax & ~0xFFu) | v; }
static inline void reg_set_ah(cpu_regs *r, u8 v) { r->eax = (r->eax & ~0xFF00u) | ((u32)v << 8); }
static inline void reg_set_bl(cpu_regs *r, u8 v) { r->ebx = (r->ebx & ~0xFFu) | v; }
static inline void reg_set_bh(cpu_regs *r, u8 v) { r->ebx = (r->ebx & ~0xFF00u) | ((u32)v << 8); }
static inline void reg_set_cl(cpu_regs *r, u8 v) { r->ecx = (r->ecx & ~0xFFu) | v; }
static inline void reg_set_ch(cpu_regs *r, u8 v) { r->ecx = (r->ecx & ~0xFF00u) | ((u32)v << 8); }
static inline void reg_set_dl(cpu_regs *r, u8 v) { r->edx = (r->edx & ~0xFFu) | v; }
static inline void reg_set_dh(cpu_regs *r, u8 v) { r->edx = (r->edx & ~0xFF00u) | ((u32)v << 8); }

/* 实模式下的段装载。每次执行 mov ds, ax 这类指令之后都要调它刷新缓存。
 * 基址 = 段值乘 16 是实模式唯一的换算规则，限长固定 64K。
 * 0x93 是"存在、可读写的数据段"的典型属性值，i486 上真机就是这个味道。 */
static inline void seg_set_real(seg_reg *s, u16 sel)
{
    s->sel = sel;
    s->base = (u32)sel << 4;
    s->limit = 0xFFFF;
    s->attr = 0x0093;
}

/* 上电状态：CS:IP 指向 FFFF:0000，其余段清零。
 * 我们跑 .COM 不走这条路（由加载器直接摆好段和 IP），
 * 但留一个正确的起点，将来跑真机镜像时用得上。 */
static inline void regs_reset(cpu_regs *r)
{
    /* 逐字段清，不用 (cpu_regs){0}。那写法在 C++ 下会触发
     * -Wmissing-field-initializers，而这头文件两边都要编 */
    r->eax = 0; r->ebx = 0; r->ecx = 0; r->edx = 0;
    r->esi = 0; r->edi = 0; r->ebp = 0; r->esp = 0;
    r->eip = 0;

    seg_set_real(&r->cs, 0xFFFF);   /* 上电时 CS:IP 指向 FFFF:0000 */
    seg_set_real(&r->ds, 0);
    seg_set_real(&r->es, 0);
    seg_set_real(&r->ss, 0);
    seg_set_real(&r->fs, 0);
    seg_set_real(&r->gs, 0);

    r->eflags = 0x00000002;         /* bit1 恒为 1，真机就是这样 */

    /* 下面这些也必须清。漏掉的话，vm 结构体落在栈上时就是垃圾值——
     * 而 CR0 的 PG 位一旦被垃圾点亮，分页会毫无征兆地打开，
     * 症状是"第一条指令就取指失败"。这个坑已经踩过一次 */
    r->cr0 = 0;
    r->cr2 = 0;
    r->cr3 = 0;
    r->cr4 = 0;

    r->gdt_base = 0;
    r->gdt_limit = 0;
    r->idt_base = 0;
    r->idt_limit = 0;

    r->ldtr = 0;
    r->ldt_base = 0;
    r->ldt_limit = 0;
    r->tr = 0;
    r->tss_base = 0;
    r->tss_limit = 0;

    r->pf_fault = 0;
    r->pf_lin = 0;
    r->pf_err = 0;
}

#endif /* DOSPP_CPU_REGS_H */
