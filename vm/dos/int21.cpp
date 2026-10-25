#include "dos/int21.h"

#include <cstring>

static void set_cf(cpu_regs *r, int on)
{
    if (on) r->eflags |= 0x00000001u;
    else    r->eflags &= ~0x00000001u;
}

static void set_ax(cpu_regs *r, u16 v)
{
    r->eax = (r->eax & 0xFFFF0000u) | v;
}

/* 从 DS:DX 读一个以 0 结尾的字符串 */
static void read_asciiz(const memory *m, u32 seg, u16 off, char *out, int out_size)
{
    int i = 0;
    for (; i < out_size - 1; ++i) {
        u8 ch = mem_read8(m, seg, (u32)off + (u32)i);
        out[i] = (char)ch;
        if (ch == 0) break;
    }
    out[i] = '\0';
}

/* AH=09 的字符串以 '$' 结尾。加一根 64K 保险丝，
 * 程序忘了放 '$' 时宁可打歪，也不能把虚拟机卡死 */
static void print_dollar_string(const memory *m, u32 seg_base, u16 off,
                                vm_putc_fn putc, void *ctx)
{
    for (u32 i = 0; i < 0x10000u; ++i) {
        u8 ch = mem_read8(m, seg_base, (u32)off + i);
        if (ch == '$') return;
        putc(ctx, (char)ch);
    }
}

static void put_bytes(const memory *m, u32 seg, u16 off, u32 count,
                      vm_putc_fn putc, void *ctx)
{
    for (u32 i = 0; i < count; ++i)
        putc(ctx, (char)mem_read8(m, seg, (u32)off + i));
}

/* 找文件类调用的公共收尾：成功清 CF，失败置 CF 并给错误码 */
static void finish_find(cpu_regs *r, int rc, int err)
{
    if (rc == 1) {
        set_cf(r, 0);
        set_ax(r, 0);
    } else {
        set_cf(r, 1);
        set_ax(r, (u16)(rc == 0 ? DOS_ERR_NOMORE : err));
    }
}

int int21_call(cpu_regs *r, const memory *m, int21_env *env, u8 *exit_code)
{
    const u8 ah = (u8)((r->eax >> 8) & 0xFF);
    const u8 al = (u8)(r->eax & 0xFF);
    const u16 bx = (u16)(r->ebx & 0xFFFF);
    const u16 cx = (u16)(r->ecx & 0xFFFF);
    const u16 dx = (u16)(r->edx & 0xFFFF);

    switch (ah) {
    case 0x01:                        /* 读键并回显 */
    case 0x08: {                      /* 读键不回显 */
        int c = env->getc ? env->getc(env->getc_ctx) : -1;
        if (c < 0) c = 0x1A;          /* 没输入就当 Ctrl-Z（EOF） */
        if (ah == 0x01) env->putc(env->putc_ctx, (char)c);
        set_ax(r, (u16)c);
        set_cf(r, 0);
        return 0;
    }

    case 0x09: {
        print_dollar_string(m, r->ds.base, dx, env->putc, env->putc_ctx);
        return 0;
    }

    case 0x1A:                        /* 设置 DTA：DS:DX */
        dos_files_set_dta(env->files, r->ds.base, dx);
        set_cf(r, 0);
        return 0;

    case 0x3B: {                      /* 切换目录 */
        char path[128];
        read_asciiz(m, r->ds.base, dx, path, (int)sizeof(path));

        int rc = env->files->fs.chdir(env->files->fs.ctx, path);
        set_cf(r, rc != 0);
        if (rc != 0) set_ax(r, DOS_ERR_NOPATH);
        return 0;
    }

    case 0x3D: {                      /* 打开文件 */
        char path[128];
        read_asciiz(m, r->ds.base, dx, path, (int)sizeof(path));

        int err = 0;
        int h = dos_file_open(env->files, path, al, &err);
        if (h < 0) { set_cf(r, 1); set_ax(r, (u16)err); }
        else       { set_cf(r, 0); set_ax(r, (u16)h); }
        return 0;
    }

    case 0x3E:                        /* 关闭文件 */
        if (bx < DOS_FIRST_HANDLE) { set_cf(r, 1); set_ax(r, DOS_ERR_HANDLE); return 0; }
        dos_file_close(env->files, bx);
        set_cf(r, 0);
        return 0;

    case 0x3F: {                      /* 读文件 */
        static u8 tmp[0x10000];

        if (bx < DOS_FIRST_HANDLE) {
            /* 标准输入先返回"没读到"，别让程序以为出错 */
            if (bx == 0) { set_cf(r, 0); set_ax(r, 0); return 0; }
            set_cf(r, 1);
            set_ax(r, DOS_ERR_HANDLE);
            return 0;
        }

        int err = 0;
        int n = dos_file_read(env->files, bx, tmp, cx, &err);
        if (n < 0) { set_cf(r, 1); set_ax(r, (u16)err); return 0; }

        for (int i = 0; i < n; ++i)
            mem_write8(m, r->ds.base, (u32)dx + (u32)i, tmp[i]);

        set_cf(r, 0);
        set_ax(r, (u16)n);
        return 0;
    }

    case 0x40: {                      /* 写文件 */
        if (bx == 1 || bx == 2) {     /* stdout / stderr */
            put_bytes(m, r->ds.base, dx, cx, env->putc, env->putc_ctx);
            set_cf(r, 0);
            set_ax(r, cx);
            return 0;
        }
        if (bx < DOS_FIRST_HANDLE) { set_cf(r, 1); set_ax(r, DOS_ERR_HANDLE); return 0; }

        /* cx 最大 0xFFFF，缓冲区正好 64K，装得下，不用再判越界 */
        static u8 tmp2[0x10000];
        for (u32 i = 0; i < cx; ++i)
            tmp2[i] = mem_read8(m, r->ds.base, (u32)dx + i);

        int err = 0;
        int n = dos_file_write(env->files, bx, tmp2, cx, &err);
        if (n < 0) { set_cf(r, 1); set_ax(r, (u16)err); return 0; }

        set_cf(r, 0);
        set_ax(r, (u16)n);
        return 0;
    }

    case 0x4E: {                      /* 找第一个 */
        char spec[128];
        read_asciiz(m, r->ds.base, dx, spec, (int)sizeof(spec));

        int err = 0;
        int rc = dos_find_first(env->files, m, spec, al, &err);
        finish_find(r, rc, err);
        return 0;
    }

    case 0x4F: {                      /* 找下一个 */
        int err = 0;
        int rc = dos_find_next(env->files, m, &err);
        finish_find(r, rc, err);
        return 0;
    }

    case 0x4C:                        /* 退出 */
        if (exit_code) *exit_code = al;
        return 1;

    default:
        /* 没实现的功能静默放过。这会让"程序没反应"变难查，
         * 所以调试时可以在这里加一句 putc 打警告 */
        return 0;
    }
}
