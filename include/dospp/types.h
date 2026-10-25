/* 全项目共用的类型和几个小宏。
 *
 * 规矩就一条：别的地方不准再写裸的 int / long / char / unsigned，这样用着才不烧心（）。
 *
 * 这个头会被 C 和 C++ 同时包含（内核是 C，dosvm 是 C++）。
 * 改完两边都得编一遍，只试一边的话另一边迟早炸。
 */

#ifndef DOSPP_TYPES_H
#define DOSPP_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* u8/u16/u32/u64 这套短名字敲起来省事，grep 也好找 */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

/* 物理地址和虚拟地址分开定义。现在俩都是 64 位，看着像多余的，但等内核真开了分页，把这俩混用是最容易写出来、又最难查的 bug */
typedef u64 paddr_t;
typedef u64 vaddr_t;

/* 编译器属性。用得最多的是 PACKED，跟硬件结构体打交道时离不了 */
#define PACKED        __attribute__((packed))
#define NORETURN      __attribute__((noreturn))
#define UNUSED        __attribute__((unused))
#define ALIGNED(n)    __attribute__((aligned(n)))
#define SECTION(s)    __attribute__((section(s)))

/* 分支提示。等 CPU 核写出来，主循环里全是 if，那会儿差别就很明显了 */
#define LIKELY(x)     __builtin_expect(!!(x), 1)
#define UNLIKELY(x)   __builtin_expect(!!(x), 0)
#define UNREACHABLE() __builtin_unreachable()

/* C 和 C++ 的写法不一样：C 叫 _Static_assert，C++ 叫 static_assert。
 * 只定义一个的话，另一种语言那边会直接报语法错——这个坑已经踩过了 */
#ifdef __cplusplus
#define STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/* ARRAY_SIZE 只吃一个参数，所以别往里塞带逗号的复合字面量。
 * 比如 ARRAY_SIZE((u8[]){1,2,3}) 是编不过的，先声明成具名数组再传 */
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)      ((a) < (b) ? (a) : (b))
#define MAX(a, b)      ((a) > (b) ? (a) : (b))

/* 下面这些字符串会原样进 CI 日志和别人的终端，
 * 中文在那边基本都是乱码，反而看不出到底哪儿错了（）。 */
STATIC_ASSERT(sizeof(void *) == 8,  "dospp requires a 64-bit build");
STATIC_ASSERT(sizeof(paddr_t) == 8, "paddr_t must be 64-bit");
STATIC_ASSERT(sizeof(u16) == 2,     "u16 must be 16-bit");

#endif /* DOSPP_TYPES_H */

/*我就用中文注释 爱来自中国于2026年10月*/
