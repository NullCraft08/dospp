#include "arch/x86_64/serial.h"

/* 这几个端口操作是内核里唯一的内联汇编，别扩散出去 */
static inline void outb(u16 port, u8 val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* 关中断 */
    outb(COM1 + 3, 0x80);   /* 打开 DLAB */
    outb(COM1 + 0, 0x03);   /* 除数低字节：38400 baud */
    outb(COM1 + 1, 0x00);   /* 除数高字节 */
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* 打开并清空 FIFO */
    outb(COM1 + 4, 0x0B);   /* IRQ 打开、RTS/DSR 置位 */
}

static int tx_ready(void)
{
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putc(char ch)
{
    /* 忙等发送寄存器空。串口比 CPU 慢几个数量级，
     * 但这个循环一次也就几微秒，M0 阶段不值得为它搞中断 */
    while (!tx_ready()) { }
    outb(COM1, (u8)ch);
}

void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') serial_putc('\r');   /* 终端要 CRLF 才换行 */
        serial_putc(*s++);
    }
}

void serial_hex(u64 v)
{
    static const char digits[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        const u8 nib = (u8)((v >> shift) & 0xF);
        if (shift < 60 && nib == 0 && v < (1ull << shift)) continue;   /* 去掉前导零 */
        serial_putc(digits[nib]);
    }
}

void serial_dec(u64 v)
{
    char buf[24];
    int i = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) serial_putc(buf[--i]);
}
