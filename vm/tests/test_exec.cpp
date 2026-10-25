// Day 4 的验收：取指、ModRM 解码、几条最基本的指令。
//
// 全是手写的机器码，不加载任何 .COM。这样出错时能立刻知道是哪条指令的问题，
// 而不是"跑了个程序结果不对"。

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

/* 把机器码摆到 CS:0x100，然后把 IP 指过去。段全用 0，
 * 这样线性地址就等于偏移，断言里好算 */
static void load(const u8 *code, int n)
{
    std::memset(pool, 0, sizeof(pool));
    std::memset(&r, 0, sizeof(r));
    for (int i = 0; i < n; ++i) pool[0x100 + i] = code[i];

    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    seg_set_real(&r.cs, 0x0000);
    seg_set_real(&r.ds, 0x0000);
    seg_set_real(&r.es, 0x0000);
    seg_set_real(&r.ss, 0x0000);
    r.eip = 0x0100;
}

static void test_mov_imm16()
{
    const u8 code[] = { 0xB8, 0x34, 0x12 };
    load(code, sizeof(code));
    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_OK);
    CHECK(reg_ax(&r) == 0x1234);
    CHECK(r.eip == 0x0103);
}

static void test_mov_imm8_high()
{
    /* B4 09 就是 DOS 里最熟的 mov ah, 9 */
    const u8 code[] = { 0xB4, 0x09 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    CHECK(reg_ah(&r) == 0x09);
    /* 只动高 8 位，低字节必须还是 0 */
    CHECK(reg_al(&r) == 0x00);
    CHECK(r.eip == 0x0102);
}

static void test_two_instructions()
{
    const u8 code[] = { 0xB8, 0x34, 0x12, 0xBB, 0x78, 0x56 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    exec_step(&r, &mem);
    CHECK(reg_ax(&r) == 0x1234);
    CHECK(reg_bx(&r) == 0x5678);
    CHECK(r.eip == 0x0106);
}

static void test_mov_ax_from_mem()
{
    /* 8B 07 = mov ax, [bx]。BX 指向 0x0200，那边先放个 0xBEEF */
    const u8 code[] = { 0x8B, 0x07 };
    load(code, sizeof(code));
    mem_write16(&mem, 0x0000, 0x0200, 0xBEEF);
    reg_set_bx(&r, 0x0200);
    exec_step(&r, &mem);
    CHECK(reg_ax(&r) == 0xBEEF);
}

static void test_mov_mem_from_ax()
{
    /* 89 07 = mov [bx], ax */
    const u8 code[] = { 0x89, 0x07 };
    load(code, sizeof(code));
    reg_set_ax(&r, 0xCAFE);
    reg_set_bx(&r, 0x0200);
    exec_step(&r, &mem);
    CHECK(mem_read16(&mem, 0x0000, 0x0200) == 0xCAFE);
}

static void test_mov_mem_imm()
{
    /* C7 07 34 12 = mov word [bx], 0x1234 */
    const u8 code[] = { 0xC7, 0x07, 0x34, 0x12 };
    load(code, sizeof(code));
    reg_set_bx(&r, 0x0200);
    exec_step(&r, &mem);
    CHECK(mem_read16(&mem, 0x0000, 0x0200) == 0x1234);
    CHECK(r.eip == 0x0104);
}

static void test_disp16_direct()
{
    /* 8B 06 34 12 = mov ax, [0x1234]。mod=00 rm=110 是直接寻址，
     * 不是 [bp]——这个特例值得单独测一条 */
    const u8 code[] = { 0x8B, 0x06, 0x34, 0x12 };
    load(code, sizeof(code));
    mem_write16(&mem, 0x0000, 0x1234, 0x5A5A);
    exec_step(&r, &mem);
    CHECK(reg_ax(&r) == 0x5A5A);
}

static void test_bp_uses_ss_segment()
{
    /* 8B 46 00 = mov ax, [bp+0]。基址是 bp，所以默认走 SS 段。
     * 这里故意让 DS 和 SS 指向不同基址，看它到底取哪个 */
    const u8 code[] = { 0x8B, 0x46, 0x00 };
    load(code, sizeof(code));
    seg_set_real(&r.ds, 0x0000);
    seg_set_real(&r.ss, 0x1000);          /* 线性 0x10000 */
    reg16_set(&r, 5, 0x0010);   /* 5 = bp，具名 setter 只给了 ax/bx/cx/dx */

    mem_write16(&mem, 0x00000, 0x0010, 0x1111);   /* DS 那边 */
    mem_write16(&mem, 0x10000, 0x0010, 0x2222);   /* SS 那边 */

    exec_step(&r, &mem);
    CHECK(reg_ax(&r) == 0x2222);
}

static void test_segment_override()
{
    /* 26 8B 07 = mov ax, es:[bx] */
    const u8 code[] = { 0x26, 0x8B, 0x07 };
    load(code, sizeof(code));
    seg_set_real(&r.ds, 0x0000);
    seg_set_real(&r.es, 0x2000);          /* 线性 0x20000 */
    reg_set_bx(&r, 0x0010);

    mem_write16(&mem, 0x00000, 0x0010, 0x1111);
    mem_write16(&mem, 0x20000, 0x0010, 0x3333);

    exec_step(&r, &mem);
    CHECK(reg_ax(&r) == 0x3333);
    CHECK(r.eip == 0x0103);
}

static void test_int21()
{
    const u8 code[] = { 0xCD, 0x21 };
    load(code, sizeof(code));
    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_INT);
    CHECK(st.intno == 0x21);
    CHECK(r.eip == 0x0102);
}

static void test_hlt()
{
    const u8 code[] = { 0xF4 };
    load(code, sizeof(code));
    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_HALT);
}

static void test_unknown_opcode()
{
    /* 0F 本身现在是合法前缀（后面跟着扩展指令），
     * 要测"未实现"得用一个真正没做的扩展字节 */
    const u8 code[] = { 0x0F, 0xFF };
    load(code, sizeof(code));
    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_UNSUPPORTED);
    CHECK(st.opcode == 0x0F);
}

static void test_seg_reg_access()
{
    /* mov ax,1234h / mov ds,ax / mov ax,ds。
     * 这三条是 .EXE 程序的标配：DS 默认指向 PSP，
     * 必须自己把它设成数据段，否则所有数据访问都落在 PSP 上 */
    const u8 code[] = { 0xB8, 0x34, 0x12, 0x8E, 0xD8, 0x8C, 0xD8 };
    load(code, sizeof(code));

    exec_step(&r, &mem);              /* mov ax, 1234h */
    CHECK(reg_ax(&r) == 0x1234);

    exec_step(&r, &mem);              /* mov ds, ax */
    CHECK(r.ds.sel == 0x1234);
    CHECK(r.ds.base == 0x12340);      /* 实模式：段值乘 16 */

    exec_step(&r, &mem);              /* mov ax, ds，读回来应该还是 1234h */
    CHECK(reg_ax(&r) == 0x1234);
}

static void test_opsize32()
{
    /* 66 B8 78 56 34 12 = mov eax, 0x12345678 */
    const u8 code[] = { 0x66, 0xB8, 0x78, 0x56, 0x34, 0x12 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    CHECK(r.eax == 0x12345678);
    CHECK(r.eip == 0x0106);
}

static void test_486_instructions()
{
    /* 0F C8 = bswap eax */
    const u8 bswap_code[] = { 0x0F, 0xC8 };
    load(bswap_code, sizeof(bswap_code));
    r.eax = 0x12345678;
    exec_step(&r, &mem);
    CHECK(r.eax == 0x78563412);
    CHECK(r.eip == 0x0102);

    /* 0F B1 CB = cmpxchg bx, cx。相等时要写入并置 ZF */
    const u8 cmpxchg_code[] = { 0x0F, 0xB1, 0xCB };
    load(cmpxchg_code, sizeof(cmpxchg_code));
    reg_set_ax(&r, 0x1111);
    reg_set_bx(&r, 0x1111);
    reg_set_cx(&r, 0x2222);
    exec_step(&r, &mem);
    CHECK(reg_bx(&r) == 0x2222);          /* 源写进去了 */
    CHECK((r.eflags & EFLAGS_ZF) != 0);   /* ZF = 1 */

    /* 不相等时：目标不动，把目标读回累加器，ZF 清 0 */
    load(cmpxchg_code, sizeof(cmpxchg_code));
    reg_set_ax(&r, 0x1111);
    reg_set_bx(&r, 0x9999);
    reg_set_cx(&r, 0x2222);
    r.eflags |= EFLAGS_ZF;                /* 先置上，看它会不会被清 */
    exec_step(&r, &mem);
    CHECK(reg_bx(&r) == 0x9999);          /* 目标没动 */
    CHECK(reg_ax(&r) == 0x9999);          /* 累加器拿到旧值 */
    CHECK((r.eflags & EFLAGS_ZF) == 0);   /* ZF = 0 */

    /* 0F C1 CB = xadd bx, cx */
    const u8 xadd_code[] = { 0x0F, 0xC1, 0xCB };
    load(xadd_code, sizeof(xadd_code));
    reg_set_bx(&r, 0x0010);
    reg_set_cx(&r, 0x0005);
    exec_step(&r, &mem);
    CHECK(reg_bx(&r) == 0x0015);          /* 和落进目标 */
    CHECK(reg_cx(&r) == 0x0010);          /* 源拿到目标旧值 */

    /* 0F 08 / 0F 09 = invd / wbinvd，当空操作 */
    const u8 invd_code[] = { 0x0F, 0x08, 0x0F, 0x09 };
    load(invd_code, sizeof(invd_code));
    exec_step(&r, &mem);
    exec_step(&r, &mem);
    CHECK(r.eip == 0x0104);

    /* 0F 01 3E 00 01 = invlpg [0x0100]，也是空操作 */
    const u8 invlpg_code[] = { 0x0F, 0x01, 0x3E, 0x00, 0x01 };
    load(invlpg_code, sizeof(invlpg_code));
    exec_step(&r, &mem);
    CHECK(r.eip == 0x0105);

    /* 0F 22 E0 = mov cr4, eax；0F 20 E0 = mov eax, cr4。
     * 两条要连着跑——load() 会把寄存器清空，中间重载就白设了 */
    const u8 cr4_code[] = { 0x0F, 0x22, 0xE0, 0x0F, 0x20, 0xE0 };
    load(cr4_code, sizeof(cr4_code));
    r.eax = 0x00000010;
    exec_step(&r, &mem);
    CHECK(r.cr4 == 0x00000010);

    r.eax = 0;
    exec_step(&r, &mem);
    CHECK(r.eax == 0x00000010);
}

int main()
{
    test_mov_imm16();
    test_mov_imm8_high();
    test_two_instructions();
    test_mov_ax_from_mem();
    test_mov_mem_from_ax();
    test_mov_mem_imm();
    test_disp16_direct();
    test_bp_uses_ss_segment();
    test_segment_override();
    test_int21();
    test_hlt();
    test_unknown_opcode();
    test_seg_reg_access();
    test_486_instructions();
    test_opsize32();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
