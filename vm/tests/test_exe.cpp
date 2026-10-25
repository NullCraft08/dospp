// Day 8 的验收：MZ 头解析、重定位、PSP 构造。

#include <cstdio>
#include <cstring>
#include <string>

#include "vm.h"
#include "dos/exe.h"
#include "dos/psp.h"

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

static void reset()
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;
    std::memset(&r, 0, sizeof(r));
}

/* ---------------- 手工构造的 MZ ---------------- */

/* 32 字节头 + 4 字节映像：映像开头是个 word，重定位表指向它 */
static const u8 mz_reloc[] = {
    'M','Z',
    0x3C,0x00,          /* e_cblp */
    0x01,0x00,          /* e_cp */
    0x01,0x00,          /* e_crlc = 1 */
    0x02,0x00,          /* e_cparhdr = 2（32 字节） */
    0x00,0x00,          /* e_minalloc */
    0xFF,0xFF,          /* e_maxalloc */
    0x00,0x00,          /* e_ss */
    0xFE,0xFF,          /* e_sp = FFFE */
    0x00,0x00,          /* e_csum */
    0x00,0x00,          /* e_ip */
    0x00,0x00,          /* e_cs */
    0x1C,0x00,          /* e_lfarlc = 0x1C */
    0x00,0x00,          /* e_ovno */
    0x00,0x00,          /* 重定位条目：段 0、偏移 0 */
    0x00,0x00,
    0x00,0x00,          /* 映像：一个待重定位的 word */
};

static void test_relocation()
{
    reset();
    CHECK(exe_load(&r, &mem, mz_reloc, sizeof(mz_reloc), 0x0200));

    /* 映像在 PSP 段 + 0x10（即 0x210），那个 0 应该被加成了 0x210 */
    CHECK(mem_read16(&mem, 0x2100, 0x0000) == 0x0210);

    /* CS = 映像段 + e_cs；DS 指向 PSP 段，不是映像段 */
    CHECK(r.cs.base == 0x2100);
    CHECK(r.ds.base == 0x2000);
    CHECK(r.eip == 0x0000);
    CHECK((r.esp & 0xFFFF) == 0xFFFE);
}

static void test_psp_fields()
{
    reset();
    CHECK(exe_load(&r, &mem, mz_reloc, sizeof(mz_reloc), 0x0200));

    const u32 base = 0x2000;
    /* 0x00 必须是 CD 20（int 20h） */
    CHECK(mem_read8(&mem, base, PSP_INT20) == 0xCD);
    CHECK(mem_read8(&mem, base, PSP_INT20 + 1) == 0x20);

    /* 0x50 必须是 CD 21 CB */
    CHECK(mem_read8(&mem, base, PSP_CALL5C) == 0xCD);
    CHECK(mem_read8(&mem, base, PSP_CALL5C + 1) == 0x21);
    CHECK(mem_read8(&mem, base, PSP_CALL5C + 2) == 0xCB);

    /* 0x02 内存顶端 */
    CHECK(mem_read16(&mem, base, PSP_MEMTOP) == (u16)PSP_MEM_TOP);

    /* 0x80 命令行长度 0，紧跟一个 CR */
    CHECK(mem_read8(&mem, base, PSP_CMDLINE) == 0x00);
    CHECK(mem_read8(&mem, base, PSP_CMDLINE + 1) == 0x0D);
}

static void test_bad_magic()
{
    reset();
    u8 bad[4] = { 'Z', 'M', 0, 0 };
    CHECK(!exe_load(&r, &mem, bad, sizeof(bad), 0x0200));
}

static void test_too_short()
{
    reset();
    u8 short_img[8] = { 'M','Z', 0,0, 0,0, 0,0 };
    CHECK(!exe_load(&r, &mem, short_img, sizeof(short_img), 0x0200));
}

/* ---------------- 能跑的 hello.exe ---------------- */

static const u8 hello_exe[] = {
    'M','Z',
    0x3C,0x00, 0x01,0x00, 0x00,0x00, 0x02,0x00,
    0x00,0x00, 0xFF,0xFF, 0x00,0x00, 0xFE,0xFF,
    0x00,0x00, 0x00,0x00, 0x00,0x00, 0x1C,0x00,
    0x00,0x00, 0x00,0x00, 0x00,0x00,
    /* 映像。头两条是 .EXE 的标配：DS 默认指向 PSP，必须自己设成代码段，
     * 否则 mov dx, offset msg 加 int 21h 打出来的是 PSP 里的垃圾 */
    0x8C,0xC8,          /* mov ax, cs */
    0x8E,0xD8,          /* mov ds, ax */
    0xB4,0x09,          /* mov ah, 9 */
    0xBA,0x10,0x00,     /* mov dx, 0010h */
    0xCD,0x21,          /* int 21h */
    0xB8,0x00,0x4C,     /* mov ax, 4C00h */
    0xCD,0x21,          /* int 21h */
    'H','e','l','l','o',' ','f','r','o','m',' ','E','X','E','!','$'
};

static std::string out;
static void sink(void *, char ch) { out.push_back(ch); }

static host_handle f_open(void *, const char *, int, int *err) { if (err) *err = 2; return -1; }
static int f_read(void *, host_handle, void *, int) { return 0; }
static int f_write(void *, host_handle, const void *, int len) { return len; }
static void f_close(void *, host_handle) {}
static int f_readdir(void *, const char *, int, host_dirent *) { return 0; }
static int f_chdir(void *, const char *) { return 0; }

static void test_run_hello_exe()
{
    hostfs_ops ops;
    ops.ctx = nullptr;
    ops.open = f_open;
    ops.read = f_read;
    ops.write = f_write;
    ops.close = f_close;
    ops.readdir = f_readdir;
    ops.chdir = f_chdir;

    vm v;
    vm_reset(&v, pool, &ops);
    CHECK(vm_load_program(&v, hello_exe, sizeof(hello_exe)));

    out.clear();
    vm_io io;
    io.putc = sink;
    io.putc_ctx = nullptr;
    io.getc = nullptr;
    io.getc_ctx = nullptr;

    CHECK(vm_run(&v, &io) == 0);
    CHECK(out == "Hello from EXE!");
    CHECK(v.exit_code == 0);
}

int main()
{
    test_relocation();
    test_psp_fields();
    test_bad_magic();
    test_too_short();
    test_run_hello_exe();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
