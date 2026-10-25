#include "dos/exe.h"
#include "dos/psp.h"

static u16 rd16(const u8 *p, u32 off)
{
    return (u16)(p[off] | ((u16)p[off + 1] << 8));
}

bool exe_load(cpu_regs *r, const memory *m, const u8 *image, u32 len, u16 seg)
{
    if (len < 0x1C) return false;
    if (image[MZ_MAGIC] != 'M' || image[MZ_MAGIC + 1] != 'Z') return false;

    const u16 cparhdr = rd16(image, MZ_CPARHDR);
    const u16 crlc    = rd16(image, MZ_CRLC);
    const u16 ss      = rd16(image, MZ_SS);
    const u16 sp      = rd16(image, MZ_SP);
    const u16 ip      = rd16(image, MZ_IP);
    const u16 cs      = rd16(image, MZ_CS);
    const u16 lfarlc  = rd16(image, MZ_LFARLC);

    const u32 hdr_size = (u32)cparhdr * 16u;
    if (hdr_size < 0x1C || hdr_size > len) return false;

    /* 用文件实际长度算映像大小，不信 e_cp/e_cblp。
     * 那两个字段在真实世界的 .EXE 里经常不准（链接器的锅），
     * 按它们算会截断或越界，按文件长度反而稳 */
    const u32 body = len - hdr_size;

    /* 映像装在 PSP 段 + 0x10 处，不是 PSP 段本身。
     * 这两者重合的话，构造 PSP 时那 256 字节会把映像开头覆盖掉——
     * 而且覆盖的正好是重定位表指过的位置，症状是程序跑起来执行到垃圾指令 */
    const u16 image_seg = (u16)(seg + 0x10);
    const u32 base = (u32)image_seg << 4;
    if (base + body > DOS_MEM_SIZE) return false;

    /* 先清零整个 64K 段：映像之后的区域就是 BSS，
     * 程序期望那里是 0 而不是上一个程序的残留 */
    for (u32 i = 0; i < 0x10000u; ++i)
        mem_write8(m, base, i, 0x00);

    for (u32 i = 0; i < body; ++i)
        mem_write8(m, base, i, image[hdr_size + i]);

    /* 重定位：把装载段加到表里每个 word 上 */
    for (u32 k = 0; k < crlc; ++k) {
        const u32 ent = (u32)lfarlc + k * 4u;
        if (ent + 4 > hdr_size) return false;      /* 表必须落在头内 */

        const u16 off = rd16(image, ent);
        const u16 rseg = rd16(image, ent + 2);
        const u32 target = ((u32)rseg << 4) + off;
        if (target + 2 > 0x10000u) return false;

        const u16 v = mem_read16(m, base, target);
        mem_write16(m, base, target, (u16)(v + image_seg));
    }

    psp_build(m, seg);

    /* .EXE 的段是独立的：CS/SS 是"相对装载段的偏移"，
     * 而 DS/ES 指向 PSP（不是代码段），这是很多程序的第一条指令就依赖的东西 */
    seg_set_real(&r->cs, (u16)(image_seg + cs));
    seg_set_real(&r->ss, (u16)(image_seg + ss));
    seg_set_real(&r->ds, seg);
    seg_set_real(&r->es, seg);

    r->eip = ip;
    r->esp = (r->esp & 0xFFFF0000u) | sp;

    return true;
}
