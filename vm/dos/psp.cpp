#include "dos/psp.h"

void psp_build(const memory *m, u16 seg)
{
    const u32 base = (u32)seg << 4;

    /* 先把整块清干净，省得程序读到上一个程序的残留 */
    for (u32 i = 0; i < PSP_SIZE; ++i)
        mem_write8(m, base, i, 0x00);

    /* 0x00 是 INT 20h。程序结束时一条 ret 跳到 PSP:0，正好落在这条指令上 */
    mem_write8(m, base, PSP_INT20 + 0, 0xCD);
    mem_write8(m, base, PSP_INT20 + 1, 0x20);

    /* 0x02 放内存顶端，程序据此知道自己能用多少 */
    mem_write16(m, base, PSP_MEMTOP, (u16)PSP_MEM_TOP);

    /* 0x2C 环境段。我们还没有环境块，写 0 */
    mem_write16(m, base, PSP_ENVSEG, 0x0000);

    /* 0x50 是 CD 21 CB：老程序会远调用这里来间接执行 INT 21h。
     * 三个字节必须精确，差一个就会跳到别处 */
    mem_write8(m, base, PSP_CALL5C + 0, 0xCD);
    mem_write8(m, base, PSP_CALL5C + 1, 0x21);
    mem_write8(m, base, PSP_CALL5C + 2, 0xCB);

    /* 0x80 命令行：第一个字节是长度，后面是内容，以 CR 结尾。
     * 暂时没有参数，就长度 0 + CR */
    mem_write8(m, base, PSP_CMDLINE, 0x00);
    mem_write8(m, base, PSP_CMDLINE + 1, 0x0D);
}
