// Day 11 的验收：保护模式下的异常投递与 IRET。
//
// 这件事的难点不在"能不能跳过去"，而在压栈的那一帧必须完全符合约定：
// 顺序、内容、有没有错误码。程序（和调试器）全靠这一帧还原现场。

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

#define GDT_AT   0x0500
#define IDT_AT   0x0300
#define STACK_TOP 0x0800

static void put_bytes(u32 at, const u8 *b, int n)
{
    for (int i = 0; i < n; ++i) pool[at + (u32)i] = b[i];
}

/* 装一个 8 字节的中断门：选择子 + 偏移 + 类型 */
static void put_gate(u32 idt_addr, int n, u16 sel, u32 off, u8 type_attr)
{
    u8 d[8];
    d[0] = (u8)(off & 0xFF);
    d[1] = (u8)((off >> 8) & 0xFF);
    d[2] = (u8)(sel & 0xFF);
    d[3] = (u8)(sel >> 8);
    d[4] = 0;
    d[5] = type_attr;
    d[6] = (u8)((off >> 16) & 0xFF);
    d[7] = (u8)((off >> 24) & 0xFF);
    put_bytes(idt_addr + (u32)n * 8u, d, 8);
}

static u16 rd16(u32 addr) { return (u16)(pool[addr] | (pool[addr + 1] << 8)); }

static void setup()
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    /* GDT：0 空、1 代码段(base 0x1000)、2 数据段(base 0) */
    const u8 null_desc[8] = { 0,0,0,0,0,0,0,0 };
    put_bytes(GDT_AT, null_desc, 8);
    const u8 code_desc[8] = { 0xFF,0xFF, 0x00,0x10,0x00, 0x9A, 0x00, 0x00 };
    put_bytes(GDT_AT + 8, code_desc, 8);
    const u8 data_desc[8] = { 0xFF,0xFF, 0x00,0x00,0x00, 0x92, 0x00, 0x00 };
    put_bytes(GDT_AT + 16, data_desc, 8);

    /* IDT：0 号（#DE）跳 0x200，13 号（#GP）跳 0x300，都指向代码段 */
    put_gate(IDT_AT, 0, 0x08, 0x0200, 0x8E);    /* P=1 DPL=0 16 位中断门 */
    put_gate(IDT_AT, 13, 0x08, 0x0300, 0x8E);

    /* 处理程序就是一条 IRET */
    pool[0x1200] = 0xCF;
    pool[0x1300] = 0xCF;

    std::memset(&r, 0, sizeof(r));
    r.cr0 = CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x0017;
    r.idt_base = IDT_AT;
    r.idt_limit = 0x007F;

    /* CS 和 SS 直接摆成保护模式的样子（选择子必须是合法描述符下标） */
    r.cs.sel = 0x08; r.cs.base = 0x1000; r.cs.limit = 0xFFFF; r.cs.attr = 0x9A;
    r.ss.sel = 0x10; r.ss.base = 0x0000; r.ss.limit = 0xFFFF; r.ss.attr = 0x92;
    r.ds.sel = 0x10; r.ds.base = 0x0000; r.ds.limit = 0xFFFF; r.ds.attr = 0x92;
    r.eip = 0x0100;
    r.esp = STACK_TOP;                 /* 低 16 位就是 SP */
    r.eflags = 0x0202;
}

static void test_divide_by_zero()
{
    setup();
    /* F7 F1 = div cx，CX 是 0 */
    pool[0x1100] = 0xF7;
    pool[0x1101] = 0xF1;
    r.ecx = 0;

    exec_status st = exec_step(&r, &mem);

    CHECK(st.res == EXEC_OK);
    CHECK(r.eip == 0x0200);            /* 跳到了 #DE 的处理程序 */
    CHECK(r.cs.base == 0x1000);        /* 仍在线性 0x1200 附近 */

    /* 压栈顺序：IP、CS、FLAGS（从栈顶往上）。#DE 没有错误码 */
    const u16 sp = (u16)(r.esp & 0xFFFF);
    CHECK(sp == STACK_TOP - 6);
    CHECK(rd16(0x0000u + sp + 0) == 0x0100);   /* 出错指令的 IP */
    CHECK(rd16(0x0000u + sp + 2) == 0x0008);   /* 出错时的 CS 选择子 */
    CHECK((rd16(0x0000u + sp + 4) & 0x0200) != 0);  /* FLAGS 里的 IF 还在 */
}

static void test_iret_returns()
{
    setup();
    pool[0x1100] = 0xF7;
    pool[0x1101] = 0xF1;
    r.ecx = 0;

    exec_step(&r, &mem);               /* 进处理程序 */
    CHECK(r.eip == 0x0200);

    exec_step(&r, &mem);               /* 那条 IRET */
    CHECK(r.eip == 0x0100);            /* 回到出错指令 */
    CHECK(r.cs.sel == 0x0008);
    CHECK((u16)(r.esp & 0xFFFF) == STACK_TOP);
}

static void test_general_protection()
{
    setup();
    /* mov ax, 0x20 ; mov ds, ax —— 0x20 超出 GDT 限长 */
    pool[0x1100] = 0xB8; pool[0x1101] = 0x20; pool[0x1102] = 0x00;
    pool[0x1103] = 0x8E; pool[0x1104] = 0xD8;

    exec_step(&r, &mem);               /* mov ax, 0x20 */
    exec_step(&r, &mem);               /* mov ds, ax → #GP */

    CHECK(r.eip == 0x0300);            /* 跳到 #GP 处理程序 */

    const u16 sp = (u16)(r.esp & 0xFFFF);
    CHECK(sp == STACK_TOP - 8);        /* 比 #DE 多压了一个错误码 */
    CHECK(rd16(0x0000u + sp + 0) == 0x0020);   /* 错误码就是那个选择子 */
    CHECK(rd16(0x0000u + sp + 2) == 0x0103);   /* 出错指令的 IP */
    CHECK(rd16(0x0000u + sp + 4) == 0x0008);   /* CS */
}

static void test_interrupt_gate_clears_if()
{
    setup();
    pool[0x1100] = 0xF7;
    pool[0x1101] = 0xF1;
    r.ecx = 0;

    exec_step(&r, &mem);
    /* 中断门（类型 6）进处理程序时应该把 IF 清掉，防止嵌套 */
    CHECK((r.eflags & 0x0200) == 0);

    /* 而压进栈里的那份 FLAGS 仍然是原来的值，IRET 之后要恢复 */
    const u16 sp = (u16)(r.esp & 0xFFFF);
    CHECK((rd16(0x0000u + sp + 4) & 0x0200) != 0);
}

static void test_no_idt_means_unsupported()
{
    setup();
    r.idt_limit = 0;                   /* 没装 IDT */
    pool[0x1100] = 0xF7;
    pool[0x1101] = 0xF1;
    r.ecx = 0;

    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_UNSUPPORTED);
}

int main()
{
    test_divide_by_zero();
    test_iret_returns();
    test_general_protection();
    test_interrupt_gate_clears_if();
    test_no_idt_means_unsupported();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
