/* Limine æ ELF çæ®µæ p_vaddr æ å°å¥½ãé¾å¨é«ååºæ¯ 64 ä½åæ ¸çå¸¸è§åæ³ï¼
 * å¥½å¤æ¯ç¨æ·ç©ºé´æ°¸è¿ç¢°ä¸å°åæ ¸å°åââè¿ä¸ªå³å®è¶æ©åè¶çäºã
 *
 * æ³¨ææ¯ä¸ªæ®µé½ ALIGN(0x1000)ï¼Limine ä¸åè®¸ä¸¤ä¸ªæéä¸åç PT_LOAD
 * å±äº«åä¸ä¸ªåå­é¡µãä¸å¯¹é½çè¯å®ä¼ç´æ¥ PANICï¼
 *   "Attempted to load ELF file with PHDRs with different permissions
 *    sharing the same memory page."
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4);   /* R     */
    data   PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
    . = 0xffffffff80000000;

    .text : ALIGN(0x1000) {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(0x1000) {
        *(.rodata .rodata.*)
    } :rodata

    /* è¯·æ±æ è®°å¿é¡»å¨æä»¶éæåå®¹ï¼PROGBITSï¼ï¼æä»¥æ¾å¨ .data éï¼
     * ä¸è½æ¾å° .bss åé¢ââé£ä¸æ®µæä»¶éæ ¹æ¬ä¸å å­è */
    .data : ALIGN(0x1000) {
        *(.data .data.*)
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :data

    .bss : ALIGN(0x1000) {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}
