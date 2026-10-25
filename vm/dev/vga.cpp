#include "dev/vga.h"

static u32 cell_off(u8 row, u8 col)
{
    return VGA_TEXT_BASE + ((u32)row * VGA_COLS + col) * 2u;
}

void vga_init(vga_text *v, u8 *mem)
{
    v->mem = mem;
    v->row = 0;
    v->col = 0;
    v->attr = VGA_ATTR_DEFAULT;
    v->dirty = 0;

    /* 加电时屏幕是黑的（属性 0x07 的空格），不是随机数据 */
    for (u32 i = 0; i < VGA_CELLS; ++i) {
        v->mem[VGA_TEXT_BASE + i * 2 + 0] = ' ';
        v->mem[VGA_TEXT_BASE + i * 2 + 1] = VGA_ATTR_DEFAULT;
    }
}

u8 vga_get_char(const vga_text *v, u8 row, u8 col)
{
    return v->mem[cell_off(row, col) + 0];
}

u8 vga_get_attr(const vga_text *v, u8 row, u8 col)
{
    return v->mem[cell_off(row, col) + 1];
}

void vga_set_cell(vga_text *v, u8 row, u8 col, u8 ch, u8 attr)
{
    if (row >= VGA_ROWS || col >= VGA_COLS) return;
    v->mem[cell_off(row, col) + 0] = ch;
    v->mem[cell_off(row, col) + 1] = attr;
    v->dirty = 1;
}

void vga_clear(vga_text *v, u8 attr)
{
    for (u8 r = 0; r < VGA_ROWS; ++r)
        for (u8 c = 0; c < VGA_COLS; ++c)
            vga_set_cell(v, r, c, ' ', attr);

    v->row = 0;
    v->col = 0;
    v->attr = attr;
}

void vga_scroll_up(vga_text *v, u8 top, u8 left, u8 bottom, u8 right, u8 lines, u8 attr)
{
    if (bottom >= VGA_ROWS) bottom = VGA_ROWS - 1;
    if (right >= VGA_COLS) right = VGA_COLS - 1;
    if (top > bottom || left > right) return;

    const u8 height = (u8)(bottom - top + 1);

    if (lines == 0 || lines >= height) {
        for (u8 r = top; r <= bottom; ++r)
            for (u8 c = left; c <= right; ++c)
                vga_set_cell(v, r, c, ' ', attr);
        return;
    }

    for (u8 r = top; r <= (u8)(bottom - lines); ++r)
        for (u8 c = left; c <= right; ++c)
            vga_set_cell(v, r, c, vga_get_char(v, (u8)(r + lines), c),
                         vga_get_attr(v, (u8)(r + lines), c));

    for (u8 r = (u8)(bottom - lines + 1); r <= bottom; ++r)
        for (u8 c = left; c <= right; ++c)
            vga_set_cell(v, r, c, ' ', attr);
}

void vga_scroll_down(vga_text *v, u8 top, u8 left, u8 bottom, u8 right, u8 lines, u8 attr)
{
    if (bottom >= VGA_ROWS) bottom = VGA_ROWS - 1;
    if (right >= VGA_COLS) right = VGA_COLS - 1;
    if (top > bottom || left > right) return;

    const u8 height = (u8)(bottom - top + 1);

    if (lines == 0 || lines >= height) {
        for (u8 r = top; r <= bottom; ++r)
            for (u8 c = left; c <= right; ++c)
                vga_set_cell(v, r, c, ' ', attr);
        return;
    }

    for (u8 r = bottom; r >= (u8)(top + lines); --r)
        for (u8 c = left; c <= right; ++c)
            vga_set_cell(v, r, c, vga_get_char(v, (u8)(r - lines), c),
                         vga_get_attr(v, (u8)(r - lines), c));

    for (u8 r = top; r < (u8)(top + lines); ++r)
        for (u8 c = left; c <= right; ++c)
            vga_set_cell(v, r, c, ' ', attr);
}

void vga_putc(vga_text *v, u8 ch)
{
    switch (ch) {
    case 0x07:                      /* BEL：宿主终端响不响不管，光标不动 */
        return;

    case 0x08:                      /* 退格 */
        if (v->col > 0) --v->col;
        vga_set_cell(v, v->row, v->col, ' ', v->attr);
        return;

    case 0x0A:                      /* 换行：只下移，不回列。回列是 CR 的事 */
        if (v->row + 1 >= VGA_ROWS) {
            vga_scroll_up(v, 0, 0, VGA_ROWS - 1, VGA_COLS - 1, 1, v->attr);
        } else {
            ++v->row;
        }
        return;

    case 0x0D:                      /* 回车 */
        v->col = 0;
        return;

    default:
        break;
    }

    vga_set_cell(v, v->row, v->col, ch, v->attr);
    if (++v->col >= VGA_COLS) {
        v->col = 0;
        if (v->row + 1 >= VGA_ROWS)
            vga_scroll_up(v, 0, 0, VGA_ROWS - 1, VGA_COLS - 1, 1, v->attr);
        else
            ++v->row;
    }
}

void vga_render(const vga_text *v, vga_putc_fn putc, void *ctx)
{
    for (u8 r = 0; r < VGA_ROWS; ++r) {
        int last = -1;
        for (int c = VGA_COLS - 1; c >= 0; --c)
            if (vga_get_char(v, r, (u8)c) != ' ') { last = c; break; }

        for (int c = 0; c <= last; ++c) {
            u8 ch = vga_get_char(v, r, (u8)c);
            putc(ctx, (char)(ch ? ch : ' '));
        }
        putc(ctx, '\n');
    }
}
