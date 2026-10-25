// Day 13 çéªæ¶ï¼åé¡µã
//
// ä¸ä»¶äºå¿é¡»æç«ï¼
//   1. å¼äºåé¡µä¹åï¼åæ¥çè®¿é®è¿è½æ­£å¸¸å·¥ä½ï¼æç­æ å°ï¼ï¼
//   2. è®¿é®æ²¡æ å°çé¡µä¼æ #PFï¼éè¯¯ç éç P ä½æ¯ 0ï¼
//   3. ååªè¯»é¡µä¼æ #PFï¼éè¯¯ç éç P å W ä½é½æ¯ 1ã

#include <cstdio>
#include <cstring>

#include "cpu/exec.h"
#include "cpu/decode.h"
#include "cpu/mmu.h"

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

#define PD_AT   0x1000
#define PT_AT   0x2000
#define GDT_AT  0x0500
#define IDT_AT  0x0300


static void wr32(u32 addr, u32 v)
{
    pool[addr] = (u8)v; pool[addr+1] = (u8)(v>>8);
    pool[addr+2] = (u8)(v>>16); pool[addr+3] = (u8)(v>>24);
}

/* ç¨ 4KB é¡µè¡¨æç­æ å°å 16 é¡µï¼0x0000â0xFFFFï¼ */
static void setup_paging()
{
    std::memset(pool, 0, sizeof(pool));
    mem.data = pool;
    mem.size = DOS_MEM_SIZE;

    std::memset(&r, 0, sizeof(r));
    r.cr0 = CR0_PE | CR0_PG;
    r.cr3 = PD_AT;

    /* é¡µç®å½ç¬¬ 0 é¡¹ â é¡µè¡¨ */
    wr32(PD_AT, PT_AT | 0x003);          /* P=1 W=1 U=1 */

    /* å 16 é¡µæç­æ å° */
    for (int i = 0; i < 16; ++i)
        wr32(PT_AT + (u32)i * 4, ((u32)i * 0x1000u) | 0x003);

    /* GDTï¼0 ç©ºã1 ä»£ç æ®µ base 0 */
    pool[GDT_AT + 8 + 0] = 0xFF;
    pool[GDT_AT + 8 + 1] = 0xFF;
    pool[GDT_AT + 8 + 5] = 0x9A;
    pool[GDT_AT + 8 + 6] = 0x00;

    /* IDTï¼14 å·ï¼#PFï¼â 0x08:0x0200 */
    const u32 g = IDT_AT + 14 * 8;
    pool[g + 0] = 0x00; pool[g + 1] = 0x02;      /* offset 0x0200 */
    pool[g + 2] = 0x08; pool[g + 3] = 0x00;      /* selector */
    pool[g + 5] = 0x8E;

    pool[0x0200] = 0xCF;                          /* å¤çç¨åºå°±æ¯ IRET */

    r.gdt_base = GDT_AT;
    r.gdt_limit = 0x000F;
    r.idt_base = IDT_AT;
    r.idt_limit = 0x007F;

    r.cs.sel = 0x0008;
    r.cs.base = 0x0000;
    r.cs.limit = 0xFFFF;
    r.cs.attr = 0x9A;
    r.ss.sel = 0x0008;
    r.ss.base = 0;
    r.ss.limit = 0xFFFF;
    r.ds = r.ss;
    r.esp = 0x0800;
    r.eip = 0x0100;
    r.eflags = 0x0202;
}

static void test_identity_works()
{
    setup_paging();
    pool[0x0100] = 0xB8;           /* mov ax, imm16 */
    pool[0x0101] = 0x34;
    pool[0x0102] = 0x12;

    exec_status st = exec_step(&r, &mem);
    CHECK(st.res == EXEC_OK);
    CHECK((r.eax & 0xFFFF) == 0x1234);
    CHECK(r.eip == 0x0103);
}

static void test_data_read_write()
{
    setup_paging();
    wr32(PT_AT + 0, 0x0000 | 0x003);

    CHECK(mmu_r8(&r, &mem, 0x0010) == 0);
    mmu_w8(&r, &mem, 0x0010, 0xAB);
    CHECK(mmu_r8(&r, &mem, 0x0010) == 0xAB);
    CHECK(r.pf_fault == 0);
}

static void test_4k_translation()
{
    setup_paging();
    /* çº¿æ§ 0x3000 æ å°å°ç©ç 0x5000ãæ 0x3000 æ¯å ä¸º
     * 0x1000 æ¯é¡µç®å½ã0x2000 æ¯é¡µè¡¨ï¼æ¿æ¥æµä¼è¢«èªå·±çç»æå¹²æ° */
    wr32(PT_AT + 12, 0x5000 | 0x003);

    mmu_w8(&r, &mem, 0x3000, 0x5A);
    CHECK(pool[0x5000] == 0x5A);          /* è½å°ç©ç 0x5000 */
    CHECK(pool[0x3000] == 0x00);          /* åå°åæ²¡è¢«å */
}

static void test_unmapped_faults()
{
    setup_paging();
    /* æçº¿æ§ 0x1000 é£ä¸é¡µç P ä½æ¸æ */
    wr32(PT_AT + 4, 0x0000);

    u32 phys = 0;
    CHECK(!mmu_translate(&r, &mem, 0x1000, false, &phys));
    CHECK((r.pf_err & PF_PRESENT) == 0);  /* é¡µä¸å­å¨ */
    CHECK(r.pf_lin == 0x1000);
}

static void test_write_protect_faults()
{
    setup_paging();
    /* çº¿æ§ 0x2000 æ å°æåªè¯»ï¼æ²¡æ W ä½ï¼ */
    wr32(PT_AT + 8, 0x2000 | 0x001);

    u32 phys = 0;
    CHECK(mmu_translate(&r, &mem, 0x2000, false, &phys));   /* è¯»æ²¡é®é¢ */

    r.pf_fault = 0;
    CHECK(!mmu_translate(&r, &mem, 0x2000, true, &phys));   /* åæ¥é */
    CHECK((r.pf_err & PF_PRESENT) != 0);                    /* é¡µå¨ï¼æ¯æéé®é¢ */
    CHECK((r.pf_err & PF_WRITE) != 0);
}

static void test_pf_delivered_to_handler()
{
    setup_paging();
    wr32(PT_AT + 4, 0x0000);               /* çº¿æ§ 0x1000 é£é¡µæ¤æ */

    /* æä»£ç æå° 0x1100ï¼å¨æ¤éçé£ä¸é¡µéï¼ï¼IP æè¿å» â åæå°±è¯¥ #PF */
    r.eip = 0x1100;
    exec_status st = exec_step(&r, &mem);

    CHECK(st.res == EXEC_OK);              /* å¼å¸¸è¢«æéäº */
    CHECK(r.eip == 0x0200);                /* è·³å°äº #PF å¤çç¨åº */

    const u16 sp = (u16)(r.esp & 0xFFFF);
    CHECK((u16)(pool[sp] | (pool[sp+1] << 8)) == 0x0000);       /* éè¯¯ç ï¼é¡µä¸å­å¨ */
    CHECK((u16)(pool[sp+2] | (pool[sp+3] << 8)) == 0x1100);     /* åºéæä»¤å°å */
}

int main()
{
    test_identity_works();
    test_data_read_write();
    test_4k_translation();
    test_unmapped_faults();
    test_write_protect_faults();
    test_pf_delivered_to_handler();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
