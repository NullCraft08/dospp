/* Limine 交给我们的已经是长模式了，所以这里没有 32→64 的那套切换。
 *
 * 它只做三件事：铺一个栈、调 kmain、然后原地停住。
 * 栈放在 .bss 里 64 KB —— 早期够用，等要跑多任务时再换成正式的内核栈。
 */
.section .bss
.align 16
stack_bottom:
    .skip 64 * 1024
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %rsp
    xor %rbp, %rbp

    /* System V ABI 要求调用前栈是 16 字节对齐的。
     * 从 stack_top 开始是 16 对齐，call 会压 8 字节返回地址，
     * 所以被调用者看到的 rsp % 16 == 8，正是它期待的样子 */
    call kmain

1:  cli
    hlt
    jmp 1b
.size _start, . - _start
