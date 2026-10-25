// dosvm 的入口：读一个 .COM 或 .EXE，跑起来，把输出接到终端。
//
// 用法：dosvm [--root 宿主角落] <file.com|file.exe>
// DOS 里的文件路径会拼到 --root 后面，默认是当前目录。

#include <cstdio>
#include <cstring>
#include <vector>

#include "vm.h"
#include "dos/hostfs.h"

static u8 pool[DOS_MEM_SIZE];

static void put_to_stdout(void *ctx, char ch)
{
    (void)ctx;
    std::putchar(ch);
    std::fflush(stdout);
}

static int get_from_stdin(void *ctx)
{
    (void)ctx;
    int c = std::getchar();      /* 会阻塞，这正好符合 DOS 的读键语义 */
    return c == EOF ? -1 : c;
}

static bool read_file(const char *path, std::vector<u8> &out)
{
    std::FILE *f = std::fopen(path, "rb");
    if (!f) return false;

    u8 buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.insert(out.end(), buf, buf + n);

    std::fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    const char *root = ".";
    const char *com = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        } else {
            com = argv[i];
        }
    }

    if (!com) {
        std::fprintf(stderr, "usage: dosvm [--root DIR] <program.com|program.exe>\n");
        return 2;
    }

    std::vector<u8> image;
    if (!read_file(com, image) || image.empty()) {
        std::fprintf(stderr, "dosvm: cannot read %s\n", com);
        return 2;
    }

    hostfs_ops fs;
    hostfs_stdio_init(&fs, root);

    vm v;
    vm_reset(&v, pool, &fs);

    if (!vm_load_program(&v, image.data(), (u32)image.size())) {
        std::fprintf(stderr, "dosvm: %s is not a valid .COM or .EXE\n", com);
        return 2;
    }

    vm_io io;
    io.putc = put_to_stdout;
    io.putc_ctx = nullptr;
    io.getc = get_from_stdin;
    io.getc_ctx = nullptr;

    int rc = vm_run(&v, &io);

    /* 程序如果往 0xB8000 写过东西，就把整屏渲染出来。
     * 没碰过显存的（比如只用 INT 21h 输出的）就不刷，免得糊一屏空白 */
    if (v.vga.dirty) {
        std::printf("\n--- screen ---\n");
        vga_render(&v.vga, put_to_stdout, nullptr);
        std::printf("--------------\n");
    }

    std::fflush(stdout);
    std::printf("\n");

    switch (rc) {
    case 0:
        std::printf("[exit %d, %u steps]\n", v.exit_code, v.steps);
        return v.exit_code;
    case -1:
        std::printf("[unimplemented opcode 0x%02X, %u steps]\n", v.bad_opcode, v.steps);
        return 3;
    case -2:
        std::printf("[step limit reached, %u steps]\n", v.steps);
        return 4;
    default:
        std::printf("[internal error %d]\n", rc);
        return 5;
    }
}
