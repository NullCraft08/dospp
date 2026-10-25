// 句柄表的单元测试。这一层不涉及 CPU，直接调函数最省事。

#include <cstdio>
#include <cstring>

#include "dos/files.h"

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

/* 一个只有两个文件的假宿主：TEST.TXT 和 EMPTY.BIN */
static host_handle fake_open_impl(void *, const char *path, int, int *err)
{
    if (std::strcmp(path, "TEST.TXT") == 0 || std::strcmp(path, "EMPTY.BIN") == 0)
        return 1;
    if (err) *err = DOS_ERR_NOFILE;
    return -1;
}

static int fake_read(void *, host_handle, void *buf, int len)
{
    const char *data = "abc";
    int n = len < 3 ? len : 3;
    std::memcpy(buf, data, (size_t)n);
    return n;
}

static int fake_write(void *, host_handle, const void *, int len) { return len; }
static void fake_close(void *, host_handle) {}
static int fake_readdir(void *, const char *, int, host_dirent *) { return 0; }
static int fake_chdir(void *, const char *) { return 0; }

static hostfs_ops make_fake()
{
    hostfs_ops ops;
    ops.ctx = nullptr;
    ops.open = fake_open_impl;
    ops.read = fake_read;
    ops.write = fake_write;
    ops.close = fake_close;
    ops.readdir = fake_readdir;
    ops.chdir = fake_chdir;
    return ops;
}

static void test_open_close()
{
    dos_files f;
    hostfs_ops ops = make_fake();
    dos_files_init(&f, &ops);

    int err = 0;
    int h = dos_file_open(&f, "TEST.TXT", 0, &err);
    CHECK(h == DOS_FIRST_HANDLE);        /* 第一个文件句柄必须是 5 */
    CHECK(h == 5);

    int h2 = dos_file_open(&f, "EMPTY.BIN", 0, &err);
    CHECK(h2 == 6);

    dos_file_close(&f, h);

    /* 关掉的槽位应该能被复用 */
    int h3 = dos_file_open(&f, "EMPTY.BIN", 0, &err);
    CHECK(h3 == 5);
}

static void test_missing_file()
{
    dos_files f;
    hostfs_ops ops = make_fake();
    dos_files_init(&f, &ops);

    int err = 0;
    int h = dos_file_open(&f, "NOPE.TXT", 0, &err);
    CHECK(h == -1);
    CHECK(err == DOS_ERR_NOFILE);
}

static void test_invalid_handle()
{
    dos_files f;
    hostfs_ops ops = make_fake();
    dos_files_init(&f, &ops);

    u8 buf[4];
    int err = 0;
    CHECK(dos_file_read(&f, 99, buf, 4, &err) == -1);
    CHECK(err == DOS_ERR_HANDLE);

    /* 标准设备句柄不该进这张表，用它们读应该也被拒绝 */
    err = 0;
    CHECK(dos_file_read(&f, 1, buf, 4, &err) == -1);
    CHECK(err == DOS_ERR_HANDLE);
}

static void test_exhaust_handles()
{
    dos_files f;
    hostfs_ops ops = make_fake();
    dos_files_init(&f, &ops);

    int err = 0;
    int count = 0;
    while (dos_file_open(&f, "TEST.TXT", 0, &err) >= 0) ++count;

    CHECK(count == DOS_HANDLES);
    CHECK(err == DOS_ERR_NOFILES);
}

int main()
{
    test_open_close();
    test_missing_file();
    test_invalid_handle();
    test_exhaust_handles();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
