/* 虚拟机的外壳：把装载器、CPU 核、INT 10h、INT 21h 串成能跑的整体。
 *
 * 这一层刻意做得很薄：摆初始状态、循环取指执行、把中断分发给服务。
 */
#ifndef DOSPP_VM_H
#define DOSPP_VM_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"
#include "dos/int21.h"
#include "dos/int10.h"
#include "dos/files.h"
#include "dev/vga.h"

/* 单次运行的指令上限。防死循环：程序写错（或我们实现错）会原地打转，
 * 没有这个上限，测试是挂住而不是失败，而挂住更难查 */
#define VM_STEP_LIMIT 5000000u

/* 输入输出接在这两个回调上：真实运行接 stdio，测试接内存缓冲 */
typedef struct {
    vm_putc_fn putc;
    void *putc_ctx;
    vm_getc_fn getc;      /* 可以为 nullptr，那就是"没有键盘输入" */
    void *getc_ctx;
} vm_io;

typedef struct {
    cpu_regs regs;
    memory mem;
    dos_files files;
    vga_text vga;
    int exit_code;
    u32 steps;
    u8 bad_opcode;          /* vm_run 返回 -1 时，是哪个 opcode 没实现 */
    u8 last_other_int;      /* 收到但不是 10h/21h 的中断号，便于排查缺什么 */
} vm;

/* pool 至少 1 MiB，由调用方提供；fs 是宿主文件系统，测试可以塞个假的 */
void vm_reset(vm *v, u8 *pool, const hostfs_ops *fs);

/* 自动识别 .COM / .EXE */
bool vm_load_program(vm *v, const u8 *image, u32 len);

/* 0 = 正常退出；-1 = 遇到没实现的指令；-2 = 超步数上限 */
int vm_run(vm *v, const vm_io *io);

#endif /* DOSPP_VM_H */
