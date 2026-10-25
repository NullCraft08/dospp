/* 虚拟 VGA 文本模式。
 *
 * 这里不做"模拟一块显卡"，只做 DOS 程序真正会用的那一小块：
 * 0xB8000 上的 80×25 字符+属性数组，加一个光标。
 *
 * 关键设计：直接读写 guest 的 1 MiB 内存，不另开缓冲区。
 * 因为大量程序是绕过 INT 10h 直接往 0xB8000 写的，如果我们在别处
 * 维护一份"屏幕状态"，两边就会打架。让它就是那块内存，最省事也最真。
 */
#ifndef DOSPP_DEV_VGA_H
#define DOSPP_DEV_VGA_H

#include "dospp/types.h"

#define VGA_TEXT_BASE   0xB8000u
#define VGA_COLS        80
#define VGA_ROWS        25
#define VGA_CELLS       (VGA_COLS * VGA_ROWS)
#define VGA_BYTES       (VGA_CELLS * 2)

/* 默认属性：黑底亮灰字，跟真机加电时一样 */
#define VGA_ATTR_DEFAULT 0x07

typedef struct {
    u8 *mem;        /* 指向 1 MiB 内存基址，屏幕缓冲就在 mem + 0xB8000 */
    u8  row, col;   /* 光标位置 */
    u8  attr;       /* 当前属性，写字符时用 */
    u8  dirty;      /* 有内容变化，退出时可以渲染出来 */
} vga_text;

void vga_init(vga_text *v, u8 *mem);

/* 写一个字符并推进光标，处理 CR / LF / BS。这是 INT 10h AH=0E 的核心 */
void vga_putc(vga_text *v, u8 ch);

/* 清屏，光标归位 */
void vga_clear(vga_text *v, u8 attr);

/* 在窗口内上/下卷 n 行（n==0 表示清窗口）。坐标都是含端点的 */
void vga_scroll_up(vga_text *v, u8 top, u8 left, u8 bottom, u8 right, u8 lines, u8 attr);
void vga_scroll_down(vga_text *v, u8 top, u8 left, u8 bottom, u8 right, u8 lines, u8 attr);

/* 直接读/写一个格子的字符和属性。绕过 INT 10h 的程序改的是同一块内存，
 * 所以这里读到的就是程序写进去的 */
u8  vga_get_char(const vga_text *v, u8 row, u8 col);
u8  vga_get_attr(const vga_text *v, u8 row, u8 col);
void vga_set_cell(vga_text *v, u8 row, u8 col, u8 ch, u8 attr);

/* 把 25 行渲染出来，行尾空格去掉。attr 暂时忽略——宿主终端画不了属性 */
typedef void (*vga_putc_fn)(void *ctx, char ch);
void vga_render(const vga_text *v, vga_putc_fn putc, void *ctx);

#endif /* DOSPP_DEV_VGA_H */
