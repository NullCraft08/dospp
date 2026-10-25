/* INT 10h：视频服务。
 *
 * 只实现文本模式那部分。图形模式（13h、VESA）列在 README 的"以后的事"里，
 * 不在这条路上。
 *
 * 这些功能看着多，其实都围着 vga_text 那点状态转：光标、属性、滚动。
 * 真 DOS 里它们最终落在显存和 CRTC 寄存器上，我们落在内存里那块 0xB8000。
 */
#ifndef DOSPP_DOS_INT10_H
#define DOSPP_DOS_INT10_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "dev/vga.h"

typedef struct {
    vga_text *vga;
} int10_env;

void int10_call(cpu_regs *r, int10_env *env);

#endif /* DOSPP_DOS_INT10_H */
