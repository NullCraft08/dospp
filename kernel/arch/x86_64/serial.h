/* COM1 串口。M0 阶段这是我们唯一的"显示器"——
 * 帧缓冲要等有字体了才谈得上输出文字。 */
#ifndef DOSPP_KERNEL_SERIAL_H
#define DOSPP_KERNEL_SERIAL_H

#include "dospp/types.h"

#define COM1 0x3F8

void serial_init(void);
void serial_putc(char ch);
void serial_puts(const char *s);
void serial_hex(u64 v);
void serial_dec(u64 v);

#endif
