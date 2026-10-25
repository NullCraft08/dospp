// Day 12 的验收：特权级检查、LDT、TSS。
//
// 规则本身很简单——"你能碰的东西，DPL 必须 >= 你的特权"——但三方（CPL/RPL/DPL）
// 只要有一个漏判，程序就能越权读到别的段。所以这里把每条组合都测一遍。

#include <cstdio>
#include <cstring>

#include "cpu/exec.h"
#include "cpu/decode.h"

static int failures = 0;
static int checks = 0;

#define CHECK(expr)                                                     \
    do {                                                                \
        ++checks;                                                       \
        if (!(expr)) {                                                  \
            ++failures;                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);  \
        }                                                               \
    } while (0)

static u8 pool[DOS_MEM_SIZE];
static memory mem;
static cpu_regs r;

#define GDT_AT  0x0500
#define IDT_AT  0x0300
#define LDT_AT  0x0700
#define TSS_AT  0x0900

/* 拼一个 8 字节描述符。access 的低 4 位是类型，上面是 S/DPL/P */
static void put_desc(u32 at, u32 base, u32 limit, u8 access, u8 flags)
{
    pool[at + 0] = (u8)(limit & 0xFF);
    pool[at + 1] = (u8)((limit >> 8) & 0xFF);
    pool[at + 2] = (u8)(base & 0xFF);
    pool[at + 3] = (u8)((base >> 8) & 0xFF);
    pool[at + 4] = (u8)((base >> 16) & 0xFF);
    pool[at + 5] = access;
    pool[at + 6] = (u8)(((flags & 0x0F) << 4) | ((limit >> 16) & 0x0F));
    pool[at + 7] = (u8)((base >> 24) & 0xFF);
}

static void put_gate(int n, u16 sel, u32 off)
{
    const u32 at = IDT_AT + (u32)n * 8u;
    pool[at + 0] = (u8)(off & 0xFF);
    pool[at + 1] = (u8)((off >> 8) & 0xFF);
    pool[at + 2] = (u8)(sel & 0xFF);
    pool[at + 3] = (u8)(sel >> 8);
    pool[at + 5] = 0x8E;
    pool[at + 6] = (u8)((off >> 16) & 0xFF);
    pool[at + 7] = (u8)((off >> 24) & 0xFF);
}

static void setup(int cpl)
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    /* GDT */
    put_desc(GDT_AT + 0,  0,        0,     0x00, 0);        /* 0x00 空 */
    put_desc(GDT_AT + 8,  0x1000,   0xFFFF, 0x9A, 0);       /* 0x08 代码 DPL0 */
    put_desc(GDT_AT + 16, 0x0000,   0xFFFF, 0x92, 0);       /* 0x10 数据 DPL0 */
    put_desc(GDT_AT + 24, 0x1000,   0xFFFF, 0xFA, 0);       /* 0x18 代码 DPL3 */
    put_desc(GDT_AT + 32, 0x30000,  0xFFFF, 0x92, 0);       /* 0x20 数据 DPL0 */
    put_desc(GDT_AT + 40, 0x40000,  0xFFFF, 0xF2, 0);       /* 0x28 数据 DPL3 */
    put_desc(GDT_AT + 48, LDT_AT,   0x000F, 0x82, 0);       /* 0x30 LDT 描述符 */
    put_desc(GDT_AT + 56, TSS_AT,   0x0067, 0x89, 0);       /* 0x38 TSS 描述符 */

    /* LDT 第一条：数据段 DPL0，base 0x50000 */
    put_desc(LDT_AT, 0x50000, 0xFFFF, 0x92, 0);

    put_gate(13, 0x08, 0x0200);        /* #GP 处理程序 */
    pool[0x1200] = 0xCF;               /* 就是一条 IRET */

    std::memset(&r, 0, sizeof(r));
    r.cr0 = CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x003F;
    r.idt_base = IDT_AT;
    r.idt_limit = 0x007F;

    r.cs.sel = (u16)(0x08 | cpl);
    r.cs.base = 0x1000;
    r.cs.limit = 0xFFFF;
    r.cs.attr = 0x9A;
    r.ss.sel = 0x10;
    r.ss.base = 0;
    r.ss.limit = 0xFFFF;
    r.esp = 0x0800;
    r.eip = 0x0100;
    r.eflags = 0x0202;
}

/* mov ax, sel ; mov ds, ax */
static exec_status load_ds(u16 sel)
{
    pool[0x1100] = 0xB8; pool[0x1101] = (u8)(sel & 0xFF); pool[0x1102] = (u8)(sel >> 8);
    pool[0x1103] = 0x8E; pool[0x1104] = 0xD8;
    r.eip = 0x0100;
    exec_step(&r, &mem);
    return exec_step(&r, &mem);
}

static void test_data_ok()
{
    setup(0);
    CHECK(load_ds(0x10).res == EXEC_OK);
    CHECK(r.ds.base == 0x0000);
    CHECK(r.ds.sel == 0x10);
}

static void test_dpl3_from_cpl0()
{
    setup(0);
    /* CPL=0 读 DPL=3 的段是允许的——特权高的人什么都能碰 */
    CHECK(load_ds(0x2B).res == EXEC_OK);      /* 0x28 | RPL3 */
    CHECK(r.ds.base == 0x40000);
}

static void test_cpl3_cannot_read_dpl0()
{
    setup(3);
    /* CPL=3 想读 DPL=0 的数据段 → #GP */
    exec_status st = load_ds(0x10);
    CHECK(st.res == EXEC_OK);                 /* #GP 被投递了，所以不是"未实现" */
    CHECK(r.eip == 0x0200);                   /* 跳到了 #GP 处理程序 */

    const u16 sp = (u16)(r.esp & 0xFFFF);
    CHECK((u16)(pool[sp] | (pool[sp + 1] << 8)) == 0x0010);   /* 错误码 = 选择子 */
}

static void test_cpl3_can_read_dpl3()
{
    setup(3);
    CHECK(load_ds(0x2B).res == EXEC_OK);
    CHECK(r.ds.base == 0x40000);
}

static void test_rpl_checks()
{
    setup(0);
    /* CPL=0 但 RPL=3 去读 DPL=0 的段：max(0,3)=3 > 0 → 拒绝。
     * 这条是防"高特权代码被低特权程序骗着去访问"的经典漏洞 */
    load_ds(0x13);                            /* 0x10 | RPL3 */
    CHECK(r.eip == 0x0200);
}

static void test_far_jump_priv()
{
    setup(0);
    /* 从 CPL=0 远跳到 DPL=3 的代码段：非一致代码段要求 DPL == CPL → #GP */
    pool[0x1100] = 0xEA; pool[0x1101] = 0x00; pool[0x1102] = 0x00;
    pool[0x1103] = 0x18; pool[0x1104] = 0x00;
    r.eip = 0x0100;
    exec_step(&r, &mem);
    CHECK(r.eip == 0x0200);
}

static void test_ldt()
{
    setup(0);
    /* lldt ax，ax = 0x30（LDT 描述符在 GDT 里的选择子） */
    pool[0x1100] = 0xB8; pool[0x1101] = 0x30; pool[0x1102] = 0x00;
    pool[0x1103] = 0x0F; pool[0x1104] = 0x00; pool[0x1105] = 0xD0;   /* lldt ax */
    r.eip = 0x0100;
    exec_step(&r, &mem);
    exec_step(&r, &mem);

    CHECK(r.ldtr == 0x0030);
    CHECK(r.ldt_base == LDT_AT);
    CHECK(r.ldt_limit == 0x000F);

    /* 选择子 0x04：index 0、TI=1 → 走 LDT 第一条 */
    CHECK(load_ds(0x04).res == EXEC_OK);
    CHECK(r.ds.base == 0x50000);
}

static void test_tss()
{
    setup(0);
    /* ltr ax，ax = 0x38 */
    pool[0x1100] = 0xB8; pool[0x1101] = 0x38; pool[0x1102] = 0x00;
    pool[0x1103] = 0x0F; pool[0x1104] = 0x00; pool[0x1105] = 0xD8;   /* ltr ax */
    r.eip = 0x0100;
    exec_step(&r, &mem);
    exec_step(&r, &mem);

    CHECK(r.tr == 0x0038);
    CHECK(r.tss_base == TSS_AT);
}

int main()
{
    test_data_ok();
    test_dpl3_from_cpl0();
    test_cpl3_cannot_read_dpl0();
    test_cpl3_can_read_dpl3();
    test_rpl_checks();
    test_far_jump_priv();
    test_ldt();
    test_tss();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
