#include "cpu/exec.h"
#include "cpu/decode.h"
#include "cpu/mmu.h"

/* 段序号 → 段基址。SEG_NONE 不该走到这里，真走到了就当 DS 处理，
 * 让程序先跑起来，总比直接崩掉好定位。 */
static u32 seg_base(const cpu_regs *r, u8 seg)
{
    switch (seg) {
    case SEG_ES: return r->es.base;
    case SEG_CS: return r->cs.base;
    case SEG_SS: return r->ss.base;
    case SEG_FS: return r->fs.base;
    case SEG_GS: return r->gs.base;
    case SEG_DS:
    default:     return r->ds.base;
    }
}

/* 解析 8 字节段描述符。386 的格式很别扭：
 * limit 分两半（0-15 和 48-51 位），base 分三段（16-39 位），
 * 中间夹着 access 和 flags。别指望一次看懂，照着位域拆就行 */
static void desc_parse(const u8 *d, u32 *base, u32 *limit, u8 *access, u8 *flags)
{
    *limit  = (u32)d[0] | ((u32)d[1] << 8) | (((u32)d[6] & 0x0Fu) << 16);
    *base   = (u32)d[2] | ((u32)d[3] << 8) | ((u32)d[4] << 16) | ((u32)d[7] << 24);
    *access = d[5];
    *flags  = (u8)(d[6] >> 4);
}

/* 装载一个段寄存器。
 *
 * 这是整个 CPU 核里语义分裂最厉害的地方：同一条 mov ds, ax，
 * 实模式下是"基址 = 段值 × 16"，保护模式下是"用选择子去查描述符"。
 * 分岔点就是 CR0 的 PE 位，一位定生死。
 *
 * 现在只查 GDT。选择子的 TI 位（bit 2）指向 LDT 的情况还没做，
 * 等真跑保护模式程序时再补——那时候得先能 LGDT 出个 LDT 描述符。
 */
static bool load_seg_checked(cpu_regs *r, const memory *m, seg_reg *s, u16 sel, bool check_priv)
{
    /* 实模式，或者 V86：两者段语义完全一样（基址 = 段值 × 16，限长 64K）。
     * V86 特别的地方在于"虽然 PE=1，但段还是实模式的"——这正是 DOS 扩展器
     * 能在保护模式程序里调 BIOS 的原因 */
    if (!(r->cr0 & CR0_PE) || (r->eflags & EFLAGS_VM)) {
        seg_set_real(s, sel);
        return true;
    }

    if (sel == 0) {                    /* 空选择子：合法，表示"没有段" */
        s->sel = 0; s->base = 0; s->limit = 0; s->attr = 0;
        return true;
    }

    /* 选择子的 bit 2 决定查哪张表：0 是 GDT，1 是 LDT */
    const bool use_ldt = (sel & 0x0004) != 0;
    const u32 table_base = use_ldt ? r->ldt_base : r->gdt_base;
    const u16 table_limit = use_ldt ? r->ldt_limit : r->gdt_limit;

    if (use_ldt && r->ldt_limit == 0) return false;   /* 还没 LLDT 过 */

    const u32 idx = (u32)(sel >> 3);
    const u32 off = idx * 8u;
    if (off + 8u > (u32)table_limit + 1u) return false;

    u8 d[8];
    for (int i = 0; i < 8; ++i)
        d[i] = mem_read8(m, table_base, off + (u32)i);

    u32 base, limit;
    u8 access, flags;
    desc_parse(d, &base, &limit, &access, &flags);

    if (check_priv) {
        /* S 位是 0 的是"系统描述符"（LDT、TSS、门），不能当段寄存器用 */
        if (!(access & 0x10)) return false;

        const u8 dpl = (u8)((access >> 5) & 3);
        const u8 cpl = (u8)(r->cs.sel & 3);
        const u8 rpl = (u8)(sel & 3);

        if (access & 0x08) {
            /* 代码段：非一致代码段要求 DPL == CPL，且 RPL <= CPL。
             * 一致代码段的规则不一样（允许低特权调用高特权），
             * 等真遇到再说，现在按非一致处理 */
            if (dpl != cpl || rpl > cpl) return false;
        } else {
            /* 数据段：三方检查，取 CPL 和 RPL 里大的那个跟 DPL 比。
             * 说白了就是"你的特权够不够，以及你声明想要的特权够不够" */
            const u8 eff = (cpl > rpl) ? cpl : rpl;
            if (eff > dpl) return false;
        }
    }

    if (flags & 0x08) limit = (limit << 12) | 0xFFFu;   /* G 位：4K 粒度 */

    s->sel = sel;
    s->base = base;
    s->limit = limit;
    s->attr = (u16)(((u16)flags << 8) | access);
    return true;
}

/* 段装载的常用形式：带特权检查 */
static bool load_seg(cpu_regs *r, const memory *m, seg_reg *s, u16 sel)
{
    return load_seg_checked(r, m, s, sel, true);
}
/* ModRM 的 reg 字段在段寄存器指令里是段序号：0=ES 1=CS 2=SS 3=DS 4=FS 5=GS */
static seg_reg *seg_by_index(cpu_regs *r, int i)
{
    switch (i) {
    case 0: return &r->es;
    case 1: return &r->cs;
    case 2: return &r->ss;
    case 3: return &r->ds;
    case 4: return &r->fs;
    case 5: return &r->gs;
    }
    return nullptr;
}

static u8 read_rm8(const modrm &mo, cpu_regs *r, const memory *m)
{
    if (mo.is_reg) return reg8_get(r, mo.rm);
    return mmu_r8(r, m, seg_base(r, mo.seg) + mo.ea);
}

static void write_rm8(const modrm &mo, cpu_regs *r, const memory *m, u8 v)
{
    if (mo.is_reg) { reg8_set(r, mo.rm, v); return; }
    mmu_w8(r, m, seg_base(r, mo.seg) + mo.ea, v);
}

static u16 read_rm16(const modrm &mo, cpu_regs *r, const memory *m)
{
    if (mo.is_reg) return reg16_get(r, mo.rm);
    return mmu_r16(r, m, seg_base(r, mo.seg) + mo.ea);
}

static void write_rm16(const modrm &mo, cpu_regs *r, const memory *m, u16 v)
{
    if (mo.is_reg) { reg16_set(r, mo.rm, v); return; }
    mmu_w16(r, m, seg_base(r, mo.seg) + mo.ea, v);
}

/* 前缀可以叠着来。真机上最多 15 字节，这里不较真，扫到不是前缀就停 */
static bool is_prefix(u8 b)
{
    switch (b) {
    case 0x26: case 0x2E: case 0x36: case 0x3E:
    case 0x64: case 0x65:
    case 0x66: case 0x67:
    case 0xF0: case 0xF2: case 0xF3:
        return true;
    default:
        return false;
    }
}

static void apply_prefix(prefixes *p, u8 b)
{
    switch (b) {
    case 0x26: p->seg = SEG_ES; break;
    case 0x2E: p->seg = SEG_CS; break;
    case 0x36: p->seg = SEG_SS; break;
    case 0x3E: p->seg = SEG_DS; break;
    case 0x64: p->seg = SEG_FS; break;
    case 0x65: p->seg = SEG_GS; break;
    case 0x66: p->opsize32 = 1; break;
    case 0x67: p->adsize32 = 1; break;
    case 0xF0: p->lock = 1; break;
    case 0xF2:
    case 0xF3: p->rep = b; break;
    default: break;
    }
    p->count++;
}

/* ---- 栈 ---- */

static void push16(cpu_regs *r, const memory *m, u16 v)
{
    u16 sp = (u16)(r->esp & 0xFFFF);
    sp = (u16)(sp - 2);
    mmu_w16(r, m, r->ss.base + sp, v);
    r->esp = (r->esp & 0xFFFF0000u) | sp;
}

static u16 pop16(cpu_regs *r, const memory *m)
{
    u16 sp = (u16)(r->esp & 0xFFFF);
    u16 v = mmu_r16(r, m, r->ss.base + sp);
    r->esp = (r->esp & 0xFFFF0000u) | (u16)(sp + 2);
    return v;
}

/* ---- 异常 / 中断投递 ----
 *
 * 实模式走的是"把中断交给虚拟机去处理"的老路（INT 21h 那些），
 * 保护模式必须查 IDT 里的门描述符，然后压一帧再跳。
 *
 * 压栈顺序（从高地址往低地址）：FLAGS、CS、IP、[错误码]。
 * 这也正是 IRET 弹出的顺序——错误码不会被 IRET 弹掉，得由处理程序自己清。
 *
 * 返回 false 表示"没有可用的处理程序"，调用方就当未实现处理。
 */
static bool raise_irq(cpu_regs *r, const memory *m, u8 n, u32 err, bool has_err, u16 fault_ip)
{
    if (!(r->cr0 & CR0_PE)) return false;
    if (r->idt_limit == 0) return false;

    const u32 off = (u32)n * 8u;
    if (off + 8u > (u32)r->idt_limit + 1u) return false;

    u8 d[8];
    for (int i = 0; i < 8; ++i)
        d[i] = mem_read8(m, r->idt_base, off + (u32)i);

    if (!(d[5] & 0x80)) return false;              /* P 位为 0，门不存在 */

    const u16 sel = (u16)(d[2] | (d[3] << 8));
    const u32 goff = (u32)d[0] | ((u32)d[1] << 8) | ((u32)d[6] << 16) | ((u32)d[7] << 24);
    const u8 type = (u8)(d[5] & 0x1F);

    push16(r, m, (u16)(r->eflags & 0xFFFF));
    push16(r, m, r->cs.sel);
    push16(r, m, fault_ip);
    if (has_err) push16(r, m, (u16)err);

    /* 中断门会清 IF，陷入门不清。注意有两套类型值：
     * 286 的是 0x06/0x07，386 的是 0x0E/0x0F，两套都得认 */
    if (type == 0x06 || type == 0x0E) r->eflags &= ~0x00000200u;

    /* 这里不能再做特权检查：门的 DPL 已经在投递时校验过，
     * 硬件此时是无条件信任门里那个选择子的 */
    if (!load_seg_checked(r, m, &r->cs, sel, false)) return false;
    r->eip = goff;
    return true;
}

exec_status exec_step(cpu_regs *r, const memory *m)
{
    exec_status st;
    st.res = EXEC_OK;
    st.intno = 0;
    st.opcode = 0;

    /* 出错时压栈要压"出错那条指令的地址"，不是压到一半的 IP */
    const u16 ip0 = (u16)r->eip;
    r->pf_fault = 0;

    prefixes p;
    p.seg = SEG_NONE;
    p.opsize32 = 0;
    p.adsize32 = 0;
    p.rep = 0;
    p.lock = 0;
    p.count = 0;

    /* 先啃前缀。这里用"看一眼再决定吃不吃"的写法，
     * 而不是先读进来再退回，省得为了倒退 eip 写额外的逻辑 */
    for (;;) {
        u16 off = (u16)r->eip;
        u8 b = mmu_r8(r, m, r->cs.base + off);
        if (!is_prefix(b)) break;
        ip_read8(m, r);
        apply_prefix(&p, b);
    }

    u8 op = ip_read8(m, r);
    st.opcode = op;

    /* 取指就已经页错误了。此时 op 是 mmu_r8 失败时返回的 0，
     * 拿它去分发会跑到"未实现指令"分支，把 #PF 吞掉——
     * 必须在这里先投递，否则程序会看到一个莫名其妙的三重故障 */
    if (r->pf_fault) {
        const u32 err = r->pf_err;
        r->pf_fault = 0;
        if (!raise_irq(r, m, 14, err, true, ip0)) st.res = EXEC_UNSUPPORTED;
        return st;
    }

    switch (op) {
    case 0x90:                       /* NOP */
        break;

    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
        /* MOV r8, imm8 */
        int idx = op & 7;
        reg8_set(r, idx, ip_read8(m, r));
        break;
    }

    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
        /* MOV r16/r32, imm。0x66 前缀时是 32 位 */
        int idx = op & 7;
        if (p.opsize32) reg32_set(r, idx, ip_read32(m, r));
        else            reg16_set(r, idx, ip_read16(m, r));
        break;
    }

    case 0x88: {                     /* MOV r/m8, r8 */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        write_rm8(mo, r, m, reg8_get(r, mo.reg));
        break;
    }

    case 0x89: {                     /* MOV r/m16, r16 */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        write_rm16(mo, r, m, reg16_get(r, mo.reg));
        break;
    }

    case 0x8A: {                     /* MOV r8, r/m8 */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        reg8_set(r, mo.reg, read_rm8(mo, r, m));
        break;
    }

    case 0x8B: {                     /* MOV r16, r/m16 */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        reg16_set(r, mo.reg, read_rm16(mo, r, m));
        break;
    }

    case 0xF6:                       /* 8 位乘除组，只做 div / idiv */
    case 0xF7: {                     /* 16 位乘除组 */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        const bool wide = (op == 0xF7);

        if (mo.reg == 6 || mo.reg == 7) {          /* DIV / IDIV */
            if (wide) {
                u16 d = read_rm16(mo, r, m);
                if (d == 0) {
                    if (!raise_irq(r, m, 0, 0, false, ip0)) st.res = EXEC_UNSUPPORTED;
                    break;
                }
                u32 num = ((u32)(u16)(r->edx & 0xFFFF) << 16) | (u16)(r->eax & 0xFFFF);
                u32 q = num / d;
                if (q > 0xFFFFu) {
                    if (!raise_irq(r, m, 0, 0, false, ip0)) st.res = EXEC_UNSUPPORTED;
                    break;
                }
                r->eax = (r->eax & 0xFFFF0000u) | (u16)q;
                r->edx = (r->edx & 0xFFFF0000u) | (u16)(num % d);
            } else {
                u8 d = read_rm8(mo, r, m);
                if (d == 0) {
                    if (!raise_irq(r, m, 0, 0, false, ip0)) st.res = EXEC_UNSUPPORTED;
                    break;
                }
                u16 num = (u16)(r->eax & 0xFFFF);
                r->eax = (r->eax & 0xFFFF0000u) | (u8)(num / d);
                r->eax = (r->eax & 0xFFFF00FFu) | ((u32)(u8)(num % d) << 8);
            }
        } else {
            st.res = EXEC_UNSUPPORTED;
        }
        break;
    }

    case 0xC6: {                     /* MOV r/m8, imm8（reg 字段必须是 0） */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        if (mo.reg != 0) { st.res = EXEC_UNSUPPORTED; break; }
        write_rm8(mo, r, m, ip_read8(m, r));
        break;
    }

    case 0xC7: {                     /* MOV r/m16, imm16（reg 字段必须是 0） */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        if (mo.reg != 0) { st.res = EXEC_UNSUPPORTED; break; }
        write_rm16(mo, r, m, ip_read16(m, r));
        break;
    }

    case 0x8C: {                     /* MOV r/m16, Sreg */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        seg_reg *s = seg_by_index(r, mo.reg);
        if (!s) { st.res = EXEC_UNSUPPORTED; break; }
        write_rm16(mo, r, m, s->sel);
        break;
    }

    case 0x8E: {                     /* MOV Sreg, r/m16 */
        modrm mo;
        decode_modrm(m, r, &p, &mo);
        seg_reg *s = seg_by_index(r, mo.reg);
        if (!s || s == &r->cs) { st.res = EXEC_UNSUPPORTED; break; }
        const u16 sel = read_rm16(mo, r, m);
        if (!load_seg(r, m, s, sel)) {
            /* 选择子非法：真机抛 #GP(13)，错误码就是那个选择子。
             * 这里不能糊弄成"未实现指令"——程序靠异常号区分故障类型 */
            if (!raise_irq(r, m, 13, sel, true, ip0)) st.res = EXEC_UNSUPPORTED;
        }
        break;
    }

    case 0xEA: {                     /* JMP far ptr16:16 */
        u16 off = ip_read16(m, r);
        u16 seg = ip_read16(m, r);
        if (!load_seg(r, m, &r->cs, seg)) {
            /* 跳到非法代码段（越权、越界、非代码描述符）都是 #GP，
             * 错误码是目标选择子 */
            if (!raise_irq(r, m, 13, seg, true, ip0)) st.res = EXEC_UNSUPPORTED;
            break;
        }
        r->eip = off;
        break;
    }

    case 0x0F: {                     /* 两字节 opcode：386 的扩展区 */
        u8 op2 = ip_read8(m, r);

        switch (op2) {
        case 0x01: {                  /* LGDT / LIDT / SGDT / SIDT / SMSW / LMSW */
            modrm mo;
            decode_modrm(m, r, &p, &mo);
            if (mo.is_reg) { st.res = EXEC_UNSUPPORTED; break; }

            const u32 ea = mo.ea;
            const u32 sbase = seg_base(r, mo.seg);

            if (mo.reg == 2 || mo.reg == 3) {       /* LGDT / LIDT：6 字节 */
                u16 limit = mmu_r16(r, m, sbase + ea);
                u32 base = mmu_r32(r, m, sbase + ea + 2);
                if (mo.reg == 2) { r->gdt_base = base; r->gdt_limit = limit; }
                else             { r->idt_base = base; r->idt_limit = limit; }
            } else if (mo.reg == 0 || mo.reg == 1) { /* SGDT / SIDT */
                u32 base = (mo.reg == 0) ? r->gdt_base : r->idt_base;
                u16 limit = (mo.reg == 0) ? r->gdt_limit : r->idt_limit;
                mmu_w16(r, m, sbase + ea, limit);
                mmu_w32(r, m, sbase + ea + 2, base);
            } else if (mo.reg == 7) {                /* INVLPG：刷新某个页的 TLB 项 */
                /* 我们没有 TLB 缓存，直接无视 */
            } else if (mo.reg == 6) {                /* LMSW：只改 CR0 低 4 位 */
                r->cr0 = (r->cr0 & ~0x0Fu) | (mmu_r16(r, m, sbase + ea) & 0x0Fu);
            } else {
                st.res = EXEC_UNSUPPORTED;
            }
            break;
        }

        case 0x00: {                  /* LLDT / LTR / SLDT / STR */
            modrm mo;
            decode_modrm(m, r, &p, &mo);
            const u32 sbase = seg_base(r, mo.seg);
            const u16 v = read_rm16(mo, r, m);

            if (mo.reg == 2 || mo.reg == 3) {          /* LLDT / LTR */
                /* 选择子指向 GDT 里的一个系统描述符，里面存着这张表的
                 * 基址和限长。拿到之后，别人的选择子才能指向表内部 */
                const u32 idx = (u32)(v >> 3);
                const u32 doff = idx * 8u;
                if (v == 0 || doff + 8u > (u32)r->gdt_limit + 1u) {
                    if (!raise_irq(r, m, 13, v, true, ip0)) st.res = EXEC_UNSUPPORTED;
                    break;
                }

                u8 d[8];
                for (int i = 0; i < 8; ++i) d[i] = mem_read8(m, r->gdt_base, doff + (u32)i);

                u32 tbase, tlimit;
                u8 access, flags;
                desc_parse(d, &tbase, &tlimit, &access, &flags);
                if (flags & 0x08) tlimit = (u16)((tlimit << 12) | 0xFFFu);

                if (mo.reg == 2) { r->ldtr = v; r->ldt_base = tbase; r->ldt_limit = tlimit; }
                else             { r->tr = v;   r->tss_base = tbase; r->tss_limit = tlimit; }
            } else if (mo.reg == 0 || mo.reg == 1) {    /* SLDT / STR */
                write_rm16(mo, r, m, (mo.reg == 0) ? r->ldtr : r->tr);
            } else {
                st.res = EXEC_UNSUPPORTED;
            }
            (void)sbase;
            break;
        }

        case 0x08:                    /* INVD */
        case 0x09:                    /* WBINVD */
            /* 缓存维护指令。我们不模拟缓存，当空操作跳过——
             * 程序调它们只是想说"内存变了"，不影响结果 */
            break;

        case 0x06:                    /* CLTS */
            r->cr0 &= ~CR0_TS;
            break;

        case 0x20: {                  /* MOV r32, CRn */
            modrm mo;
            decode_modrm(m, r, &p, &mo);
            if (!mo.is_reg) { st.res = EXEC_UNSUPPORTED; break; }
            u32 v = 0;
            switch (mo.reg) {
            case 0: v = r->cr0; break;
            case 2: v = r->cr2; break;
            case 3: v = r->cr3; break;
            case 4: v = r->cr4; break;      /* CR4 是 486 才加的 */
            default: st.res = EXEC_UNSUPPORTED; break;
            }
            if (st.res == EXEC_OK) reg32_set(r, mo.rm, v);
            break;
        }

        case 0x22: {                  /* MOV CRn, r32 */
            modrm mo;
            decode_modrm(m, r, &p, &mo);
            if (!mo.is_reg) { st.res = EXEC_UNSUPPORTED; break; }
            u32 v = reg32_get(r, mo.rm);
            switch (mo.reg) {
            case 0: r->cr0 = v; break;
            case 2: r->cr2 = v; break;
            case 3: r->cr3 = v; break;
            case 4: r->cr4 = v; break;
            default: st.res = EXEC_UNSUPPORTED; break;
            }
            break;
        }

        case 0xB0:                       /* CMPXCHG r/m8, r8 */
        case 0xB1: {                     /* CMPXCHG r/m16, r16 */
            modrm mo;
            decode_modrm(m, r, &p, &mo);
            const bool wide = (op2 == 0xB1);
            const u32 acc = wide ? (r->eax & 0xFFFFu) : (r->eax & 0xFFu);
            const u32 dst = wide ? read_rm16(mo, r, m) : read_rm8(mo, r, m);
            const u32 src = wide ? reg16_get(r, mo.reg) : reg8_get(r, mo.reg);

            /* 相等就写入并置 ZF；不等就只把目标读回累加器。
             * 486 拿这个原语做自旋锁，ZF 的语义不能含糊 */
            if (dst == acc) {
                r->eflags |= EFLAGS_ZF;
                if (wide) write_rm16(mo, r, m, (u16)src);
                else      write_rm8(mo, r, m, (u8)src);
            } else {
                r->eflags &= ~EFLAGS_ZF;
                if (wide) r->eax = (r->eax & 0xFFFF0000u) | (u16)dst;
                else      r->eax = (r->eax & 0xFFFFFF00u) | (u8)dst;
            }
            break;
        }

        case 0xC0:                       /* XADD r/m8, r8 */
        case 0xC1: {                     /* XADD r/m16, r16 */
            modrm mo;
            decode_modrm(m, r, &p, &mo);
            const bool wide = (op2 == 0xC1);

            if (wide) {
                const u16 d = read_rm16(mo, r, m);
                const u16 s = reg16_get(r, mo.reg);
                reg16_set(r, mo.reg, d);           /* 源寄存器留下旧值 */
                write_rm16(mo, r, m, (u16)(d + s));
            } else {
                const u8 d = read_rm8(mo, r, m);
                const u8 s = reg8_get(r, mo.reg);
                reg8_set(r, mo.reg, d);
                write_rm8(mo, r, m, (u8)(d + s));
            }
            break;
        }

        default: {
            /* 0F C8+r 是 BSWAP r32：把 32 位寄存器的字节序整个翻过来 */
            if (op2 >= 0xC8 && op2 <= 0xCF) {
                const int idx = op2 - 0xC8;
                const u32 v = reg32_get(r, idx);
                reg32_set(r, idx,
                          ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
                          ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24));
                break;
            }
            st.res = EXEC_UNSUPPORTED;
            break;
        }
        }
        break;
    }

    case 0xCD: {                     /* INT imm8 */
        const u8 n = ip_read8(m, r);
        if ((r->cr0 & CR0_PE) && !(r->eflags & EFLAGS_VM)) {
            /* 保护模式（非 V86）：走 IDT 的软中断 */
            if (!raise_irq(r, m, n, 0, false, (u16)(ip0 + 2u)))
                st.res = EXEC_UNSUPPORTED;
        } else {
            /* 实模式或 V86：反射给虚拟机。
             * 这就是"中断反射"——V86 里的 DOS/BIOS 调用不会真的去执行
             * 那边的处理程序，而是弹回来让宿主（我们）代劳 */
            st.intno = n;
            st.res = EXEC_INT;
        }
        break;
    }

    case 0xCF: {                     /* IRET */
        u16 ip = pop16(r, m);
        u16 cs = pop16(r, m);
        u16 fl = pop16(r, m);
        if (!load_seg(r, m, &r->cs, cs)) { st.res = EXEC_UNSUPPORTED; break; }
        r->eip = ip;
        /* 16 位 IRET 弹回来的 FLAGS 只有低 16 位，改不了 VM 位——
         * 真机也是这样，要进出 V86 必须用 32 位 IRET。所以这里保住原值，
         * 免得"弹一次就莫名退出 V86" */
        r->eflags = (r->eflags & 0xFFFF0000u) | fl | (r->eflags & EFLAGS_VM);
        break;
    }

    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        push16(r, m, reg16_get(r, op & 7));
        break;

    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        reg16_set(r, op & 7, pop16(r, m));
        break;

    case 0x68:                       /* PUSH imm16 */
        push16(r, m, ip_read16(m, r));
        break;

    case 0x6A:                       /* PUSH imm8（符号扩展到 16 位） */
        push16(r, m, (u16)(i16)(i8)ip_read8(m, r));
        break;

    case 0xFA:                       /* CLI */
    case 0xFB: {                     /* STI */
        const u32 iopl = (r->eflags & EFLAGS_IOPL_MASK) >> EFLAGS_IOPL_SHIFT;
        if ((r->cr0 & CR0_PE) && (r->eflags & EFLAGS_VM) && iopl < 3) {
            /* V86 里改中断开关是敏感操作，IOPL 不够就 #GP。
             * 真机靠这个机制让监控程序能拦住程序关中断 */
            if (!raise_irq(r, m, 13, 0, true, ip0)) st.res = EXEC_UNSUPPORTED;
            break;
        }
        if (op == 0xFA) r->eflags &= ~EFLAGS_IF;
        else            r->eflags |=  EFLAGS_IF;
        break;
    }

    case 0x9C:                       /* PUSHF */
        /* 只压低 16 位。VM 位在 bit 17，本来就不在里面——这正好符合真机：
         * 处理程序不会从栈上"恢复"出一个 V86 上下文来 */
        push16(r, m, (u16)(r->eflags & 0xFFFF));
        break;

    case 0x9D:                       /* POPF */
        r->eflags = (r->eflags & 0xFFFF0000u) | pop16(r, m);
        break;

    case 0xC3:                       /* RET */
        r->eip = pop16(r, m);
        break;

    case 0xF4:                       /* HLT */
        st.res = EXEC_HALT;
        break;

    default:
        st.res = EXEC_UNSUPPORTED;
        break;
    }

    /* 页错误延迟到这里投递：指令已经跑完，边界清晰。
     * 代价是"执行到一半失败"的状态没法完整还原（读写了一半的情况存在）；
     * 对跑 DOS 程序够用，真要精确得像真机那样在访问点立即抛出 */
    if (r->pf_fault && st.res == EXEC_OK) {
        const u32 err = r->pf_err;
        r->pf_fault = 0;
        if (!raise_irq(r, m, 14, err, true, ip0)) st.res = EXEC_UNSUPPORTED;
    }

    return st;
}
