#include "vm.h"
#include "cpu/exec.h"
#include "dos/loader.h"

void vm_reset(vm *v, u8 *pool, const hostfs_ops *fs)
{
    v->mem.data = pool;
    v->mem.size = DOS_MEM_SIZE;
    v->exit_code = 0;
    v->steps = 0;
    v->bad_opcode = 0;
    v->last_other_int = 0;
    regs_reset(&v->regs);
    dos_files_init(&v->files, fs);
    vga_init(&v->vga, pool);
}

bool vm_load_program(vm *v, const u8 *image, u32 len)
{
    if (!prog_load(&v->regs, &v->mem, image, len)) return false;

    /* DTA 默认在 PSP:0080。注意这里要用 DS 而不是 CS：.EXE 的 DS 指向 PSP，
     * CS 指向代码段，两者不是一回事 */
    dos_files_set_dta(&v->files, v->regs.ds.base, 0x0080);
    return true;
}

int vm_run(vm *v, const vm_io *io)
{
    int21_env env;
    env.putc = io->putc;
    env.putc_ctx = io->putc_ctx;
    env.getc = io->getc;
    env.getc_ctx = io->getc_ctx;
    env.files = &v->files;

    while (v->steps < VM_STEP_LIMIT) {
        exec_status st = exec_step(&v->regs, &v->mem);
        ++v->steps;

        switch (st.res) {
        case EXEC_OK:
            continue;

        case EXEC_INT:
            if (st.intno == 0x10) {
                int10_env v10;
                v10.vga = &v->vga;
                int10_call(&v->regs, &v10);
            } else if (st.intno == 0x21) {
                u8 code = 0;
                if (int21_call(&v->regs, &v->mem, &env, &code) == 1) {
                    v->exit_code = code;
                    return 0;
                }
            } else {
                v->last_other_int = st.intno;
            }
            continue;

        case EXEC_HALT:
            return 0;

        case EXEC_UNSUPPORTED:
            v->bad_opcode = st.opcode;
            return -1;
        }

        return -1;
    }

    return -2;
}
