// Day 14 çéªæ¶ï¼V86 æ¨¡å¼ä¸ä¸­æ­åå°ã
//
// V86 çè¦ç¹å°±ä¸å¥è¯ï¼**PE=1ï¼ä½æ®µè¿æ¯å®æ¨¡å¼ç**ã
// å ä¸"æææä»¤å IOPL éå¶"å"ä¸­æ­åå°ç»å®¿ä¸»å¤ç"ï¼
// è¿ä¸æ¡åé½äºï¼DOS æ©å±å¨é£ç§"ä¿æ¤æ¨¡å¼ä¸»ç¨åº + å¶å°è° BIOS"çç»ææè·å¾èµ·æ¥ã

#include <cstdio>
#include <cstring>

#include "cpu/exec.h"
#include "cpu/decode.h"

static int failures = 0;
static int checks = 0;

#define CHECK(expr)                                                     \
    do {                                                                \
        ++checks;                                                       \
        if (!(expr)) {                                                  \
            ++failures;                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);  \
        }                                                               \
    } while (0)

static u8 pool[DOS_MEM_SIZE];
static memory mem;
static cpu_regs r;

#define GDT_AT  0x0500
#define IDT_AT  0x0300

static void put_gate(int n, u16 sel, u32 off)
{
    const u32 at = IDT_AT + (u32)n * 8u;
    pool[at + 0] = (u8)(off & 0xFF);
    pool[at + 1] = (u8)((off >> 8) & 0xFF);
    pool[at + 2] = (u8)(sel & 0xFF);
    pool[at + 3] = (u8)(sel >> 8);
    pool[at + 5] = 0x8E;
}

static void setup()
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    /* GDTï¼0 ç©ºã1 ä»£ç æ®µ base 0 */
    pool[GDT_AT + 8 + 0] = 0xFF;
    pool[GDT_AT + 8 + 1] = 0xFF;
    pool[GDT_AT + 8 + 5] = 0x9A;

    put_gate(13, 0x08, 0x0200);       /* #GP å¤çç¨åº */
    pool[0x0200] = 0xCF;              /* IRET */

    std::memset(&r, 0, sizeof(r));
    r.cr0 = CR0_PE;
    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x000F;
    r.idt_base = IDT_AT;
    r.idt_limit = 0x007F;

    /* V86 éç CS æ¯å®æ¨¡å¼è¯­ä¹ï¼åºå = æ®µå¼ Ã 16 */
    seg_set_real(&r.cs, 0x0000);
    seg_set_real(&r.ss, 0x0000);
    r.esp = 0x0800;
    r.eip = 0x0100;
    r.eflags = EFLAGS_VM | 0x0202;    /* VM=1ï¼IF=1ï¼IOPL=0 */
}

static void test_segment_is_real_mode()
{
    setup();
    /* mov ax, 1234h ; mov ds, axãPE=1 ä½ VM=1ï¼æä»¥ DS åºè¯¥æ¯ 0x12340 èä¸æ¯æ¥æè¿°ç¬¦ */
    pool[0x0100] = 0xB8; pool[0x0101] = 0x34; pool[0x0102] = 0x12;
    pool[0x0103] = 0x8E; pool[0x0104] = 0xD8;

    exec_step(&r, &mem);
    exec_step(&r, &mem);

    CHECK(r.ds.base == 0x12340);
    CHECK(r.ds.limit == 0xFFFF);
    CHECK(r.ds.sel == 0x1234);
}

static void test_int_is_reflected()
{
    setup();
    pool[0x0100] = 0xCD; pool[0x0101] = 0x21;    /* int 21h */

    exec_status st = exec_step(&r, &mem);

    /* ä¸æ¯æéå° IDTï¼èæ¯å¼¹åç»èææºââè¿å°±æ¯ä¸­æ­åå° */
    CHECK(st.res == EXEC_INT);
    CHECK(st.intno == 0x21);
    CHECK(r.eip == 0x0102);
}

static void test_cli_traps_without_iopl()
{
    setup();
    pool[0x0100] = 0xFA;                          /* cli */

    exec_step(&r, &mem);

    /* IOPL=0ï¼V86 é cli æ¯æææä»¤ â #GP */
    CHECK(r.eip == 0x0200);
}

static void test_cli_allowed_with_iopl3()
{
    setup();
    r.eflags = EFLAGS_VM | EFLAGS_IF | (3u << EFLAGS_IOPL_SHIFT);
    pool[0x0100] = 0xFA;                          /* cli */

    exec_status st = exec_step(&r, &mem);

    CHECK(st.res == EXEC_OK);
    CHECK((r.eflags & EFLAGS_IF) == 0);           /* IF ççè¢«æ¸äº */
    CHECK(r.eip == 0x0101);                       /* æ²¡è·³å¼å¸¸ */
}

static void test_sti_traps()
{
    setup();
    r.eflags = EFLAGS_VM | (1u << EFLAGS_IOPL_SHIFT);   /* IOPL=1 */
    pool[0x0100] = 0xFB;                          /* sti */

    exec_step(&r, &mem);
    CHECK(r.eip == 0x0200);
}

static void test_pushf_hides_vm()
{
    setup();
    pool[0x0100] = 0x9C;                          /* pushf */

    exec_step(&r, &mem);
    const u16 sp = (u16)(r.esp & 0xFFFF);
    const u16 pushed = (u16)(pool[sp] | (pool[sp + 1] << 8));
    CHECK((pushed & 0x0200) != 0);                /* IF å¨ */
    CHECK((pushed & 0x0002) != 0);                /* bit1 æ 1 */
}

static void test_iret_keeps_vm()
{
    setup();
    /* æå·¥æä¸å¸§ï¼IP=0x0150, CS=0, FLAGS=0x0202ï¼ç¶å iret */
    r.esp = 0x0800;
    pool[0x0800] = 0x50; pool[0x0801] = 0x01;
    pool[0x0802] = 0x00; pool[0x0803] = 0x00;
    pool[0x0804] = 0x02; pool[0x0805] = 0x02;
    r.esp = 0x0800;

    pool[0x0100] = 0xCF;                          /* iret */
    exec_step(&r, &mem);

    CHECK(r.eip == 0x0150);
    /* 16 ä½ IRET æ¹ä¸äº VM ä½ */
    CHECK((r.eflags & EFLAGS_VM) != 0);
}

int main()
{
    test_segment_is_real_mode();
    test_int_is_reflected();
    test_cli_traps_without_iopl();
    test_cli_allowed_with_iopl3();
    test_sti_traps();
    test_pushf_hides_vm();
    test_iret_keeps_vm();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
