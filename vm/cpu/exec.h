/* 指令执行。
 *
 * 现在只有 MOV、INT、HLT、NOP 这几种，够把 .COM 里最简单的程序跑起来。
 * 每加一条指令，都要在 tests/test_exec.cpp 里补一条断言——这地方错一点都不行，
 * 而且错了以后表现出来的症状往往离现场很远。
 */
#ifndef DOSPP_CPU_EXEC_H
#define DOSPP_CPU_EXEC_H

#include "dospp/types.h"
#include "cpu/regs.h"
#include "cpu/mem.h"

typedef enum {
    EXEC_OK = 0,
    EXEC_INT,          /* 碰到 INT n，号在 intno 里；服务由外层处理 */
    EXEC_HALT,
    EXEC_UNSUPPORTED   /* 还没实现的 opcode，号在 opcode 里 */
} exec_result;

typedef struct {
    exec_result res;
    u8 intno;
    u8 opcode;
} exec_status;

/* 执行一条指令。返回时 eip 已经指向下一条 */
exec_status exec_step(cpu_regs *r, const memory *m);

#endif /* DOSPP_CPU_EXEC_H */
