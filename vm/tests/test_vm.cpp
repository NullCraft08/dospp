// 装载并运行真实 .COM。宿主文件系统用假的，不碰磁盘。

#include <cstdio>
#include <cstring>
#include <string>

#include "vm.h"

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

static void append(void *ctx, char ch)
{
    static_cast<std::string *>(ctx)->push_back(ch);
}

/* ---------------- 假宿主文件系统 ---------------- */

struct fake_file {
    const char *name;
    const char *data;
    int pos;
};

static host_handle fake_open(void *ctx, const char *path, int, int *err)
{
    fake_file *f = static_cast<fake_file *>(ctx);
    if (std::strcmp(path, f->name) != 0) {
        if (err) *err = 2;
        return -1;
    }
    f->pos = 0;
    return 1;
}

static int fake_read(void *ctx, host_handle, void *buf, int len)
{
    fake_file *f = static_cast<fake_file *>(ctx);
    int total = (int)std::strlen(f->data);
    int left = total - f->pos;
    int n = len < left ? len : left;
    if (n > 0) {
        std::memcpy(buf, f->data + f->pos, (size_t)n);
        f->pos += n;
    }
    return n;
}

static int fake_write(void *, host_handle, const void *, int len) { return len; }
static void fake_close(void *, host_handle) {}
static int fake_readdir(void *, const char *, int, host_dirent *) { return 0; }
static int fake_chdir(void *, const char *) { return 0; }

static hostfs_ops fake_ops(fake_file *f)
{
    hostfs_ops ops;
    ops.ctx = f;
    ops.open = fake_open;
    ops.read = fake_read;
    ops.write = fake_write;
    ops.close = fake_close;
    ops.readdir = fake_readdir;
    ops.chdir = fake_chdir;
    return ops;
}

static vm_io make_io(std::string *out)
{
    vm_io io;
    io.putc = append;
    io.putc_ctx = out;
    io.getc = nullptr;
    io.getc_ctx = nullptr;
    return io;
}

/* ---------------- 测试用 .COM ---------------- */

static const u8 hello_com[] = {
    0xB4, 0x09,
    0xBA, 0x0C, 0x01,
    0xCD, 0x21,
    0xB8, 0x00, 0x4C,
    0xCD, 0x21,
    'H','e','l','l','o',',',' ','W','o','r','l','d','!','$'
};

/* 打开 TEST.TXT、读、原样打印、退出 */
static const u8 cat_com[] = {
    0xB4, 0x3D,
    0xB0, 0x00,
    0xBA, 0x26, 0x01,
    0xCD, 0x21,
    0x89, 0xC3,
    0xB4, 0x3F,
    0xB9, 0x40, 0x00,
    0xBA, 0x30, 0x01,
    0xCD, 0x21,
    0x89, 0xC1,
    0xB4, 0x40,
    0xBB, 0x01, 0x00,
    0xBA, 0x30, 0x01,
    0xCD, 0x21,
    0xB8, 0x00, 0x4C,
    0xCD, 0x21,
    'T','E','S','T','.','T','X','T', 0x00
};

static void test_hello()
{
    fake_file ff{ "NONE", "", 0 };
    hostfs_ops ops = fake_ops(&ff);

    vm v;
    vm_reset(&v, pool, &ops);
    CHECK(vm_load_program(&v, hello_com, sizeof(hello_com)));

    std::string out;
    vm_io io = make_io(&out);
    CHECK(vm_run(&v, &io) == 0);
    CHECK(out == "Hello, World!");
    CHECK(v.exit_code == 0);
    CHECK(v.steps == 5);
}

static void test_file_cat()
{
    fake_file ff{ "TEST.TXT", "file content ok", 0 };
    hostfs_ops ops = fake_ops(&ff);

    vm v;
    vm_reset(&v, pool, &ops);
    CHECK(vm_load_program(&v, cat_com, sizeof(cat_com)));

    std::string out;
    vm_io io = make_io(&out);
    CHECK(vm_run(&v, &io) == 0);
    CHECK(out == "file content ok");
    CHECK(v.exit_code == 0);
}

static void test_unsupported_opcode()
{
    const u8 code[] = { 0x0F, 0xFF };   /* 真正没实现的扩展 opcode */
    fake_file ff{ "NONE", "", 0 };
    hostfs_ops ops = fake_ops(&ff);

    vm v;
    vm_reset(&v, pool, &ops);
    CHECK(vm_load_program(&v, code, sizeof(code)));

    std::string out;
    vm_io io = make_io(&out);
    CHECK(vm_run(&v, &io) == -1);
    CHECK(v.bad_opcode == 0x0F);
}

static void test_image_too_big()
{
    fake_file ff{ "NONE", "", 0 };
    hostfs_ops ops = fake_ops(&ff);
    vm v;
    vm_reset(&v, pool, &ops);
    CHECK(!vm_load_program(&v, hello_com, 0xFF01u));
}

int main()
{
    test_hello();
    test_file_cat();
    test_unsupported_opcode();
    test_image_too_big();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
