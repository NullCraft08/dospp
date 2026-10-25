// Day 3 的验收：段换算、寄存器视图、内存读写。
//
// 这些东西看着简单，但段基址算错、小端拼错、8 位视图取错高低字节，
// 最后表现出来的都是"DOS 程序行为诡异"这种没法定位的问题。
// 所以宁可现在写一堆枯燥的断言，也别等到跑 .COM 的时候抓瞎。

#include <cstdio>

#include "cpu/regs.h"
#include "cpu/mem.h"

static int failures = 0;
static int checks = 0;

#define CHECK(expr)                                                    \
    do {                                                               \
        ++checks;                                                      \
        if (!(expr)) {                                                 \
            ++failures;                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        }                                                              \
    } while (0)

static u8 mem_pool[DOS_MEM_SIZE];

static void test_segment_base()
{
    cpu_regs r{};
    seg_set_real(&r.ds, 0x1234);
    CHECK(r.ds.base == 0x12340);
    CHECK(r.ds.limit == 0xFFFF);
    CHECK(r.ds.sel == 0x1234);

    seg_set_real(&r.es, 0x0000);
    CHECK(r.es.base == 0x00000);
}

static void test_linear()
{
    /* 最常见的一步换算：1234:0010 就是 12340 + 10 */
    CHECK(mem_linear(0x12340, 0x0010) == 0x12350);

    /* 1 MiB 的最后一字节 */
    CHECK(mem_linear(0xF0000, 0xFFFF) == 0xFFFFF);

    /* 越过 1 MiB 要绕回低端：FFFF:0010 = 0x100000，掩掉高 4 位就是 0 */
    CHECK(mem_linear(0xFFFF0, 0x0010) == 0x00000);
}

static void test_reg_views()
{
    cpu_regs r{};
    r.eax = 0x12345678;
    CHECK(reg_ax(&r) == 0x5678);
    CHECK(reg_ah(&r) == 0x56);
    CHECK(reg_al(&r) == 0x78);

    /* 改低 8 位不能碰到高位 */
    reg_set_al(&r, 0xAA);
    CHECK(r.eax == 0x123456AA);
    reg_set_ah(&r, 0xBB);
    CHECK(r.eax == 0x1234BBAA);

    /* 16 位视图同理 */
    reg_set_ax(&r, 0x9999);
    CHECK(r.eax == 0x12349999);
    reg_set_dx(&r, 0xABCD);
    CHECK(reg_dx(&r) == 0xABCD);
    CHECK(reg_dh(&r) == 0xAB);
    CHECK(reg_dl(&r) == 0xCD);
}

static void test_memory()
{
    memory m{};
    m.data = mem_pool;
    m.size = DOS_MEM_SIZE;

    /* 8 位往返 */
    mem_write8(&m, 0xB8000, 0x0000, 0x41);
    CHECK(mem_read8(&m, 0xB8000, 0x0000) == 0x41);

    /* 16/32 位必须是小端：先写 0x12345678，再逐字节看 */
    mem_write32(&m, 0x10000, 0x0100, 0x12345678);
    CHECK(mem_read8(&m, 0x10000, 0x0100) == 0x78);
    CHECK(mem_read8(&m, 0x10000, 0x0101) == 0x56);
    CHECK(mem_read8(&m, 0x10000, 0x0102) == 0x34);
    CHECK(mem_read8(&m, 0x10000, 0x0103) == 0x12);
    CHECK(mem_read16(&m, 0x10000, 0x0100) == 0x5678);
    CHECK(mem_read32(&m, 0x10000, 0x0100) == 0x12345678);

    /* 段值参与换算：1234:0010 应该落到线性 0x12350 */
    mem_write8(&m, 0x12340, 0x0010, 0x7E);
    CHECK(mem_pool[0x12350] == 0x7E);
}

int main()
{
    test_segment_base();
    test_reg_views();
    test_linear();
    test_memory();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
