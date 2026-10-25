// Day 9 的验收：0xB8000 那块字符缓冲、光标、滚动，以及 INT 10h 的几个功能。

#include <cstdio>
#include <cstring>
#include <string>

#include "cpu/mem.h"
#include "dev/vga.h"
#include "dos/int10.h"

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
static vga_text vga;

static void reset()
{
    std::memset(pool, 0, sizeof(pool));
    vga_init(&vga, pool);
}

static u8 cell_char(int row, int col)
{
    return pool[VGA_TEXT_BASE + (row * VGA_COLS + col) * 2];
}
static u8 cell_attr(int row, int col)
{
    return pool[VGA_TEXT_BASE + (row * VGA_COLS + col) * 2 + 1];
}

static void test_clear()
{
    reset();
    for (int c = 0; c < VGA_COLS; ++c)
        CHECK(cell_char(0, c) == ' ');
    CHECK(cell_attr(0, 0) == VGA_ATTR_DEFAULT);
    CHECK(vga.row == 0 && vga.col == 0);
}

static void test_putc_advances()
{
    reset();
    vga_putc(&vga, 'A');
    vga_putc(&vga, 'B');
    CHECK(cell_char(0, 0) == 'A');
    CHECK(cell_char(0, 1) == 'B');
    CHECK(vga.col == 2);
}

static void test_crlf()
{
    reset();
    vga_putc(&vga, 'A');
    vga_putc(&vga, 0x0D);           /* CR：回列首 */
    CHECK(vga.col == 0);
    vga_putc(&vga, 0x0A);           /* LF：下一行 */
    CHECK(vga.row == 1);
    CHECK(vga.col == 0);
}

static void test_backspace()
{
    reset();
    vga_putc(&vga, 'X');
    vga_putc(&vga, 0x08);
    CHECK(vga.col == 0);
    CHECK(cell_char(0, 0) == ' ');  /* 退格把字符擦掉了 */
}

static void test_wrap_and_scroll()
{
    reset();
    /* 写满一行多一点，应该自动换到下一行 */
    for (int i = 0; i < VGA_COLS; ++i) vga_putc(&vga, 'x');
    CHECK(vga.row == 1);
    CHECK(vga.col == 0);

    /* 跑到最后一行再换行，整屏应该上卷一行 */
    vga.row = VGA_ROWS - 1;
    vga.col = 0;
    for (int c = 0; c < VGA_COLS; ++c) vga_set_cell(&vga, VGA_ROWS - 1, c, 'z', 0x07);
    vga_putc(&vga, 0x0A);           /* 最后一行再下移 → 触发滚动 */

    /* 原来最后一行应该落到倒数第二行 */
    CHECK(cell_char(VGA_ROWS - 2, 0) == 'z');
    CHECK(cell_char(VGA_ROWS - 1, 0) == ' ');
}

static void test_direct_memory_write()
{
    reset();
    /* 模拟程序绕过 INT 10h 直接写显存 */
    pool[VGA_TEXT_BASE + 0] = 'D';
    pool[VGA_TEXT_BASE + 1] = 0x4E;
    CHECK(vga_get_char(&vga, 0, 0) == 'D');
    CHECK(vga_get_attr(&vga, 0, 0) == 0x4E);
}

static void test_int10_teletype()
{
    reset();
    cpu_regs r;
    std::memset(&r, 0, sizeof(r));
    int10_env env;
    env.vga = &vga;

    const char *s = "Hi";
    for (const char *q = s; *q; ++q) {
        r.eax = (u32)((0x0E << 8) | (u8)*q);
        int10_call(&r, &env);
    }
    CHECK(cell_char(0, 0) == 'H');
    CHECK(cell_char(0, 1) == 'i');
    CHECK(vga.col == 2);
}

static void test_int10_cursor()
{
    reset();
    cpu_regs r;
    std::memset(&r, 0, sizeof(r));
    int10_env env;
    env.vga = &vga;

    r.eax = 0x0200;
    r.edx = (u16)((5 << 8) | 10);   /* DH=行 5，DL=列 10 */
    int10_call(&r, &env);
    CHECK(vga.row == 5 && vga.col == 10);

    r.eax = 0x0300;
    int10_call(&r, &env);
    CHECK(((r.edx >> 8) & 0xFF) == 5);
    CHECK((r.edx & 0xFF) == 10);
}

static void test_int10_write_char_attr()
{
    reset();
    cpu_regs r;
    std::memset(&r, 0, sizeof(r));
    int10_env env;
    env.vga = &vga;

    vga.row = 2;
    vga.col = 3;
    r.eax = 0x0920;                 /* AH=09，字符是空格 */
    r.ebx = 0x1F;                   /* BL=属性：蓝底白字 */
    r.ecx = 4;                      /* CR 次 */
    int10_call(&r, &env);

    for (int i = 0; i < 4; ++i)
        CHECK(cell_attr(2, 3 + i) == 0x1F);
    /* AH=09 不该动光标 */
    CHECK(vga.col == 3);
}

static void test_int10_scroll()
{
    reset();
    cpu_regs r;
    std::memset(&r, 0, sizeof(r));
    int10_env env;
    env.vga = &vga;

    for (int i = 0; i < 5; ++i)
        vga_set_cell(&vga, (u8)i, 0, (u8)('0' + i), 0x07);

    /* 整个屏幕往上卷 2 行 */
    r.eax = 0x0602;                 /* AH=06，AL=2 */
    r.ebx = 0x0700;
    r.ecx = 0x0000;                 /* 左上 0,0 */
    r.edx = (u16)(((VGA_ROWS - 1) << 8) | (VGA_COLS - 1));
    int10_call(&r, &env);

    CHECK(cell_char(0, 0) == '2');  /* 原来的第 2 行上来了 */
    CHECK(cell_char(1, 0) == '3');
    CHECK(cell_char(4, 0) == ' ');
}

static std::string rendered;
static void sink(void *, char ch) { rendered.push_back(ch); }

static void test_render()
{
    reset();
    vga_set_cell(&vga, 0, 0, 'O', 0x07);
    vga_set_cell(&vga, 0, 1, 'K', 0x07);
    vga_set_cell(&vga, 1, 0, 'X', 0x07);

    rendered.clear();
    vga_render(&vga, sink, nullptr);

    /* 第一行是 "OK"，第二行是 "X"，后面 23 行是空行 */
    CHECK(rendered.rfind("OK\nX\n", 0) == 0);
    int lines = 0;
    for (char c : rendered) if (c == '\n') ++lines;
    CHECK(lines == VGA_ROWS);
}

int main()
{
    test_clear();
    test_putc_advances();
    test_crlf();
    test_backspace();
    test_wrap_and_scroll();
    test_direct_memory_write();
    test_int10_teletype();
    test_int10_cursor();
    test_int10_write_char_attr();
    test_int10_scroll();
    test_render();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
