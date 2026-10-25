#include "dos/int10.h"

void int10_call(cpu_regs *r, int10_env *env)
{
    vga_text *v = env->vga;

    const u8 ah = (u8)((r->eax >> 8) & 0xFF);
    const u8 al = (u8)(r->eax & 0xFF);
    const u8 bh = (u8)((r->ebx >> 8) & 0xFF);
    const u8 bl = (u8)(r->ebx & 0xFF);
    const u8 ch = (u8)((r->ecx >> 8) & 0xFF);
    const u8 cl = (u8)(r->ecx & 0xFF);
    const u8 dh = (u8)((r->edx >> 8) & 0xFF);
    const u8 dl = (u8)(r->edx & 0xFF);

    switch (ah) {
    case 0x00:                        /* 设置模式。文本模式就是清屏 */
        v->attr = VGA_ATTR_DEFAULT;
        vga_clear(v, VGA_ATTR_DEFAULT);
        /* 真 DOS 会返回 AL = 模式 | 0x80（表示清屏了），照做 */
        r->eax = (r->eax & 0xFFFF0000u) | (u16)(al | 0x80);
        return;

    case 0x02:                        /* 定位光标 */
        if (dh < VGA_ROWS) v->row = dh;
        if (dl < VGA_COLS) v->col = dl;
        return;

    case 0x03: {                      /* 读光标位置 */
        r->edx = (r->edx & 0xFFFF0000u) | ((u16)v->row << 8) | v->col;
        /* CX 是光标形状，给个常见值（起始 7、结束 8） */
        r->ecx = (r->ecx & 0xFFFF0000u) | 0x0607u;
        return;
    }

    case 0x05:                        /* 设置活动页。只有一页，忽略 */
        return;

    case 0x06:                        /* 窗口上卷，AL=行数（0 表示清空） */
        vga_scroll_up(v, ch, cl, dh, dl, al, bh ? bh : VGA_ATTR_DEFAULT);
        return;

    case 0x07:                        /* 窗口下卷 */
        vga_scroll_down(v, ch, cl, dh, dl, al, bh ? bh : VGA_ATTR_DEFAULT);
        return;

    case 0x08: {                      /* 读光标处的字符和属性 */
        u8 c = vga_get_char(v, v->row, v->col);
        u8 a = vga_get_attr(v, v->row, v->col);
        r->eax = (r->eax & 0xFFFF0000u) | ((u16)a << 8) | c;
        return;
    }

    case 0x09: {                      /* 写字符+属性 CX 次，光标不动 */
        u8 attr = bl;
        for (u16 i = 0; i < (u16)((r->ecx & 0xFFFF)); ++i) {
            u8 col = (u8)(v->col + i);
            if (col >= VGA_COLS) break;
            vga_set_cell(v, v->row, col, al, attr);
        }
        return;
    }

    case 0x0A:                        /* 只写字符，属性沿用原值 */
        for (u16 i = 0; i < (u16)((r->ecx & 0xFFFF)); ++i) {
            u8 col = (u8)(v->col + i);
            if (col >= VGA_COLS) break;
            vga_set_cell(v, v->row, col, al, vga_get_attr(v, v->row, col));
        }
        return;

    case 0x0E:                        /* 电传输出：最常用的那个 */
        vga_putc(v, al);
        return;

    case 0x0F: {                      /* 读当前模式 */
        /* AH=列数，AL=模式（3 = 80×25 彩色文本），BH=页号 */
        r->eax = (r->eax & 0xFFFF0000u) | ((u16)VGA_COLS << 8) | 0x03;
        r->ebx = (r->ebx & 0xFFFF0000u);
        return;
    }

    default:
        /* 其余（图形模式、调色板、字体……）静默忽略。
         * 程序通常会继续往下跑，只是画不出东西 */
        return;
    }
}
