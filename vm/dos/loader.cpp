#include "dos/loader.h"
#include "dos/exe.h"
#include "dos/psp.h"

bool com_load(cpu_regs *r, const memory *m, const u8 *image, u32 len)
{
    /* 0x100 到 0xFFFF 之间是代码区，所以最多 0xFF00 字节。
     * 再大就是 .EXE 的活儿了，不是这里该处理的事 */
    if (len == 0 || len > 0xFF00u) return false;

    const u32 base = (u32)DOS_COM_SEG << 4;

    /* 先清零 64K：.COM 的 BSS 也在这一段里，程序期望它是 0 */
    for (u32 i = 0; i < 0x10000u; ++i)
        mem_write8(m, base, i, 0x00);

    for (u32 i = 0; i < len; ++i)
        mem_write8(m, base, DOS_COM_START + i, image[i]);

    psp_build(m, (u16)DOS_COM_SEG);

    /* 四个段全指同一块：.COM 就是这么糙，代码、数据、栈挤在一个 64K 里 */
    seg_set_real(&r->cs, (u16)DOS_COM_SEG);
    seg_set_real(&r->ds, (u16)DOS_COM_SEG);
    seg_set_real(&r->es, (u16)DOS_COM_SEG);
    seg_set_real(&r->ss, (u16)DOS_COM_SEG);

    r->eip = DOS_COM_START;

    /* 栈顶压一个 0，当作"返回 DOS"的地址。老程序的 main 末尾一条 ret 走这条路，
     * 跳回 PSP:0000——正好是 int 20h。我们还没实现 ret，但地址先摆对 */
    r->esp = (r->esp & 0xFFFF0000u) | DOS_COM_STACK;
    mem_write16(m, base, DOS_COM_STACK, 0x0000);

    return true;
}

bool prog_load(cpu_regs *r, const memory *m, const u8 *image, u32 len)
{
    if (len >= 2 && image[0] == 'M' && image[1] == 'Z')
        return exe_load(r, m, image, len, (u16)DOS_EXE_SEG);

    return com_load(r, m, image, len);
}
