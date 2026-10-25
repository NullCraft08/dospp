// Day 7 的验收：目录查找（AH=4E/4F）、DTA（AH=1A）、切目录（AH=3B）、
// 读键（AH=01/08）。
//
// 这些不通过 .COM 来测——手写一段带循环的 find 程序太长，
// 编码错了还分不清是 CPU 的错还是服务的错。直接摆好寄存器和内存再调
// int21_call，一样是端到端，但失败时位置明确。

#include <cstdio>
#include <cstring>
#include <string>

#include "dos/int21.h"

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
static dos_files files;

/* 假宿主：四个条目，其中两个 .TXT */
static const host_dirent dir_entries[] = {
    { "TEST.TXT",  100, 0 },
    { "OTHER.TXT", 200, 0 },
    { "READ.ME",    50, 0 },
    { "SUBDIR",      0, 1 },
};
static const int dir_count = (int)(sizeof(dir_entries) / sizeof(dir_entries[0]));

static int fake_readdir(void *, const char *, int idx, host_dirent *out)
{
    if (idx < 0 || idx >= dir_count) return 0;
    *out = dir_entries[idx];
    return 1;
}

static int fake_chdir(void *, const char *path)
{
    return std::strcmp(path, "SUBDIR") == 0 ? 0 : -1;
}

static host_handle fake_open(void *, const char *, int, int *err)
{
    if (err) *err = 2;
    return -1;
}
static int fake_read(void *, host_handle, void *, int) { return 0; }
static int fake_write(void *, host_handle, const void *, int len) { return len; }
static void fake_close(void *, host_handle) {}

static hostfs_ops ops;

static std::string out;

static void sink(void *, char ch) { out.push_back(ch); }

static int key_queue[8];
static int key_count = 0;
static int key_pos = 0;

static int fake_getc(void *)
{
    if (key_pos >= key_count) return -1;
    return key_queue[key_pos++];
}

static void reset_state()
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    std::memset(&r, 0, sizeof(r));
    seg_set_real(&r.cs, 0x0000);
    seg_set_real(&r.ds, 0x0000);
    seg_set_real(&r.es, 0x0000);
    seg_set_real(&r.ss, 0x0000);

    ops.ctx = nullptr;
    ops.open = fake_open;
    ops.read = fake_read;
    ops.write = fake_write;
    ops.close = fake_close;
    ops.readdir = fake_readdir;
    ops.chdir = fake_chdir;

    dos_files_init(&files, &ops);
    dos_files_set_dta(&files, 0x0000, 0x0080);   /* DTA 挪到 0x0080，方便读 */

    out.clear();
    key_pos = 0;
    key_count = 0;
}

static int21_env make_env()
{
    int21_env env;
    env.putc = sink;
    env.putc_ctx = nullptr;
    env.getc = fake_getc;
    env.getc_ctx = nullptr;
    env.files = &files;
    return env;
}

/* 把字符串摆到 DS:off，结尾补 0 */
static void put_str(u16 off, const char *s)
{
    for (int i = 0; ; ++i) {
        mem_write8(&mem, 0x0000, off + (u16)i, (u8)s[i]);
        if (!s[i]) break;
    }
}

static std::string dta_name()
{
    char buf[14] = {0};
    for (int i = 0; i < 13; ++i)
        buf[i] = (char)mem_read8(&mem, 0x0000, 0x0080 + FIND_NAME + (u16)i);
    return std::string(buf);
}

static int cf(const cpu_regs &reg) { return reg.eflags & 1; }

static void test_find_first_txt()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    put_str(0x0200, "*.TXT");
    r.edx = 0x0200;
    r.eax = 0x4E00;               /* AH=4E, AL=属性 0 */
    int21_call(&r, &mem, &env, &code);

    CHECK(cf(r) == 0);
    CHECK(dta_name() == "TEST.TXT");
    CHECK(mem_read32(&mem, 0x0000, 0x0080 + FIND_SIZE) == 100);

    /* 下一个应该是 OTHER.TXT */
    r.eax = 0x4F00;
    int21_call(&r, &mem, &env, &code);
    CHECK(cf(r) == 0);
    CHECK(dta_name() == "OTHER.TXT");

    /* 再下一个：没有了，应该置 CF 并报 18。
     * 注意这里必须重设 AH：上一次调用的返回值是放在 AX 里的，
     * AH 已经被清成 0 了。真 DOS 程序每次都得这么写 */
    r.eax = 0x4F00;
    int21_call(&r, &mem, &env, &code);
    CHECK(cf(r) == 1);
    CHECK((r.eax & 0xFFFF) == DOS_ERR_NOMORE);
}

static void test_find_first_no_match()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    put_str(0x0200, "*.XYZ");
    r.edx = 0x0200;
    r.eax = 0x4E00;
    int21_call(&r, &mem, &env, &code);

    CHECK(cf(r) == 1);
    CHECK((r.eax & 0xFFFF) == DOS_ERR_NOFILE);
}

static void test_find_all()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    put_str(0x0200, "*.*");
    r.edx = 0x0200;
    r.eax = 0x4E00;
    int21_call(&r, &mem, &env, &code);
    CHECK(cf(r) == 0);

    int n = 1;
    for (;;) {
        r.eax = 0x4F00;
        int21_call(&r, &mem, &env, &code);
        if (cf(r)) break;
        ++n;
        if (n > 10) break;
    }
    CHECK(n == dir_count);   /* 四个条目全列出来 */
}

static void test_set_dta()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    r.eax = 0x1A00;
    r.edx = 0x0300;
    int21_call(&r, &mem, &env, &code);

    CHECK(files.dta_off == 0x0300);

    put_str(0x0200, "READ.ME");
    r.edx = 0x0200;
    r.eax = 0x4E00;
    int21_call(&r, &mem, &env, &code);
    CHECK(cf(r) == 0);

    /* 名字应该写在新 DTA 里 */
    char buf[14] = {0};
    for (int i = 0; i < 13; ++i)
        buf[i] = (char)mem_read8(&mem, 0x0000, 0x0300 + FIND_NAME + (u16)i);
    CHECK(std::string(buf) == "READ.ME");
}

static void test_chdir()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    put_str(0x0200, "SUBDIR");
    r.edx = 0x0200;
    r.eax = 0x3B00;
    int21_call(&r, &mem, &env, &code);
    CHECK(cf(r) == 0);

    put_str(0x0200, "NOPE");
    r.edx = 0x0200;
    r.eax = 0x3B00;
    int21_call(&r, &mem, &env, &code);
    CHECK(cf(r) == 1);
}

static void test_read_key()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    key_queue[0] = 'a';
    key_queue[1] = 'b';
    key_count = 2;

    r.eax = 0x0800;            /* 不回显 */
    int21_call(&r, &mem, &env, &code);
    CHECK((r.eax & 0xFFFF) == 'a');
    CHECK(out.empty());        /* 没回显，输出应该是空的 */

    r.eax = 0x0100;            /* 回显 */
    int21_call(&r, &mem, &env, &code);
    CHECK((r.eax & 0xFFFF) == 'b');
    CHECK(out == "b");
}

static void test_read_key_eof()
{
    reset_state();
    int21_env env = make_env();
    u8 code;

    key_count = 0;             /* 没有输入了 */
    r.eax = 0x0800;
    int21_call(&r, &mem, &env, &code);
    CHECK((r.eax & 0xFFFF) == 0x1A);   /* Ctrl-Z */
}

int main()
{
    test_find_first_txt();
    test_find_first_no_match();
    test_find_all();
    test_set_dta();
    test_chdir();
    test_read_key();
    test_read_key_eof();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
