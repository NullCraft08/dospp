/* M0：能启动、能说话。
 *
 * 这一步不画图、不管内存、不开中断。它只证明一件事：
 * **Limine 真的把控制权交给了我们的代码，而且我们能看到启动信息。**
 *
 * 输出走串口——帧缓冲要先把字体啃下来才谈得上写文字，那是 M3 的事。
 */

#include "limine.h"
#include "arch/x86_64/serial.h"

/* 协议版本。宏展开成 24 字节的标记，Limine 靠它判断内核想要哪版协议。
 * 注意它是个初始化列表，必须赋给一个具名数组，不能再当表达式用一次 */
__attribute__((used, section(".limine_requests")))
static volatile u64 base_revision[3] = LIMINE_BASE_REVISION(3);

/* 请求帧缓冲，用来验证 Limine 确实按我们的要求准备了启动信息 */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

/* 请求内存映射，M2 做物理内存管理时要用 */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

/* 这三个标记告诉 Limine"请求从哪到哪" */
/* 这两个标记本身也是初始化列表，同样要具名 */
__attribute__((used, section(".limine_requests_start")))
static volatile u64 requests_start[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile u64 requests_end[2] = LIMINE_REQUESTS_END_MARKER;

void kmain(void)
{
    serial_init();
    serial_puts("\n");
    serial_puts("DOS++ kernel M0\n");
    serial_puts("---------------\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_puts("[!] bootloader does not support base revision 3\n");
        return;
    }
    serial_puts("protocol   : limine, base revision 3\n");

    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
        const struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
        serial_puts("framebuffer: ");
        serial_dec(fb->width);
        serial_puts("x");
        serial_dec(fb->height);
        serial_puts(" @ ");
        serial_dec(fb->bpp);
        serial_puts("bpp, pitch ");
        serial_dec(fb->pitch);
        serial_puts(" bytes\n");
    } else {
        serial_puts("[!] no framebuffer\n");
    }

    if (memmap_request.response) {
        serial_puts("memmap     : ");
        serial_dec(memmap_request.response->entry_count);
        serial_puts(" regions\n");

        u64 usable = 0;
        for (u64 i = 0; i < memmap_request.response->entry_count; ++i) {
            const struct limine_memmap_entry *e = memmap_request.response->entries[i];
            if (e->type == LIMINE_MEMMAP_USABLE) usable += e->length;
        }
        serial_puts("usable ram : ");
        serial_dec(usable / (1024 * 1024));
        serial_puts(" MiB\n");
    } else {
        serial_puts("[!] no memory map\n");
    }

    serial_puts("\nM0 alive. halting.\n");
}
