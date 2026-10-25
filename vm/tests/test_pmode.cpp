// Day 10 的验收：保护模式的地基。
//
// 这里不跑完整程序，只验证三件事：
//   1. LGDT / LIDT 能把这些表的位置存下来并读回去；
//   2. CR0 的 PE 位能通过指令置上；
//   3. PE 置上之后，mov ds, ax 的语义从"乘 16"变成"查描述符"。
//
// 第 3 条是重点。同一个段寄存器的两条装载路径，错了就是整套保护模式全歪。

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

#define GDT_AT     0x0500
#define GDTR_AT    0x0600
#define SEL_CODE   0x0008
#define SEL_DATA   0x0010

static void put_bytes(u32 addr, const u8 *b, int n)
{
    for (int i = 0; i < n; ++i) pool[addr + (u32)i] = b[i];
}

static void setup()
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    std::memset(&r, 0, sizeof(r));
    seg_set_real(&r.cs, 0x0000);
    seg_set_real(&r.ds, 0x0000);
    seg_set_real(&r.es, 0x0000);
    seg_set_real(&r.ss, 0x0000);
    r.eip = 0x0100;

    /* 描述符 0：空 */
    const u8 null_desc[8] = { 0,0,0,0,0,0,0,0 };
    put_bytes(GDT_AT, null_desc, 8);

    /* 描述符 1（选择子 0x08）：代码段，base=0x1000，limit=0xFFFF，access=0x9A */
    const u8 code_desc[8] = { 0xFF,0xFF, 0x00,0x10,0x00, 0x9A, 0x00, 0x00 };
    put_bytes(GDT_AT + 8, code_desc, 8);

    /* 描述符 2（选择子 0x10）：数据段，base=0x20000，limit=0xFFFF，access=0x92 */
    const u8 data_desc[8] = { 0xFF,0xFF, 0x00,0x00,0x02, 0x92, 0x00, 0x00 };
    put_bytes(GDT_AT + 16, data_desc, 8);

    /* GDTR 的内容：limit = 23（三个描述符减一），base = 0x500 */
    pool[GDTR_AT + 0] = 0x17;
    pool[GDTR_AT + 1] = 0x00;
    pool[GDTR_AT + 2] = 0x00;
    pool[GDTR_AT + 3] = 0x05;
    pool[GDTR_AT + 4] = 0x00;
    pool[GDTR_AT + 5] = 0x00;
}

/* 把一段机器码摆到 0x100，从那儿开始执行 */
static void load(const u8 *code, int n)
{
    for (int i = 0; i < n; ++i) pool[0x100 + i] = code[i];
    r.eip = 0x0100;
}

static void test_lgdt_sgdt()
{
    setup();
    /* lgdt [0x0600] */
    const u8 code[] = { 0x0F, 0x01, 0x16, 0x00, 0x06 };
    load(code, sizeof(code));
    exec_step(&r, &mem);

    CHECK(r.gdt_base == GDT_AT);
    CHECK(r.gdt_limit == 0x0017);

    /* 再用 sgdt 写到另一个地址，确认读出来的是同一份内容 */
    const u8 code2[] = { 0x0F, 0x01, 0x06, 0x00, 0x07 };   /* sgdt [0x0700] */
    load(code2, sizeof(code2));
    exec_step(&r, &mem);

    CHECK(pool[0x0700] == 0x17);        /* limit 低字节 */
    CHECK(pool[0x0701] == 0x00);
    CHECK(pool[0x0702] == 0x00);        /* base 低字节：0x500 */
    CHECK(pool[0x0703] == 0x05);
}

static void test_set_cr0_pe()
{
    setup();
    /* mov eax, 1 ; mov cr0, eax */
    const u8 code[] = {
        0x66, 0xB8, 0x01, 0x00, 0x00, 0x00,   /* mov eax, 1 */
        0x0F, 0x22, 0xC0,                      /* mov cr0, eax */
    };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    exec_step(&r, &mem);
    CHECK((r.cr0 & CR0_PE) != 0);

    /* 再读回来：mov edx, cr0 */
    const u8 code2[] = { 0x0F, 0x20, 0xC2 };
    load(code2, sizeof(code2));
    exec_step(&r, &mem);
    CHECK((r.edx & CR0_PE) != 0);
}

static void test_real_mode_unchanged()
{
    setup();
    /* 实模式下 mov ds, ax 还是乘 16 */
    const u8 code[] = { 0xB8, 0x34, 0x12, 0x8E, 0xD8 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    exec_step(&r, &mem);
    CHECK(r.ds.base == 0x12340);
    CHECK(r.ds.limit == 0xFFFF);
}

static void test_protected_mode_segment_load()
{
    setup();
    r.cr0 |= CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x0017;

    /* mov ax, 0x10 ; mov ds, ax —— 选择子 0x10 是数据段描述符 */
    const u8 code[] = { 0xB8, 0x10, 0x00, 0x8E, 0xD8 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    exec_step(&r, &mem);

    CHECK(r.ds.sel == SEL_DATA);
    CHECK(r.ds.base == 0x20000);        /* 来自描述符，不是 0x10 * 16 */
    CHECK(r.ds.limit == 0xFFFF);
    CHECK((r.ds.attr & 0xFF) == 0x92);
}

static void test_null_selector()
{
    setup();
    r.cr0 |= CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x0017;

    /* 选择子 0 是空描述符，装载后 base/limit 都该归零 */
    const u8 code[] = { 0xB8, 0x00, 0x00, 0x8E, 0xD8 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    exec_step(&r, &mem);

    CHECK(r.ds.base == 0);
    CHECK(r.ds.limit == 0);
}

static void test_far_jump()
{
    setup();
    r.cr0 |= CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x0017;

    /* jmp far 0008:0000 —— 跳进代码段描述符 */
    const u8 code[] = { 0xEA, 0x00, 0x00, 0x08, 0x00 };
    load(code, sizeof(code));
    exec_status st = exec_step(&r, &mem);

    CHECK(st.res == EXEC_OK);
    CHECK(r.cs.base == 0x1000);         /* 代码段描述符的 base */
    CHECK(r.eip == 0x0000);
}

static void test_descriptor_out_of_range()
{
    setup();
    r.cr0 |= CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x0007;               /* 只够一个描述符 */

    /* 选择子 0x10 超出了限长，应该装载失败而不是读越界 */
    const u8 code[] = { 0xB8, 0x10, 0x00, 0x8E, 0xD8 };
    load(code, sizeof(code));
    exec_step(&r, &mem);
    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_UNSUPPORTED);
}

int main()
{
    test_lgdt_sgdt();
    test_set_cr0_pe();
    test_real_mode_unchanged();
    test_protected_mode_segment_load();
    test_null_selector();
    test_far_jump();
    test_descriptor_out_of_range();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
