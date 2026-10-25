# DOS++（dospp）

一个**可启动、可分发的开源操作系统**：现代 x86_64 内核，加上一个**自研的用户态 DOS
虚拟机**。虚拟机对 guest 呈现的 CPU 目标是 **i486SX 的完整整数指令集**。

> **状态：Day 16 —— 内核线 M0 完成：Limine 引导进长模式，启动信息走串口。**
> 每天做了什么见[进展](#进展)。

## 目标

| 承诺 | 含义 | 验收方式 |
|---|---|---|
| **可启动**（bootable） | 裸机与 QEMU 都能起，不依赖宿主操作系统 | 把 ISO 挂进虚拟机或写入 U 盘，看到启动画面 |
| **可分发**（distributable） | 提供可直接使用的镜像，源码可重建出同一个镜像 | 任何人拿到 ISO 就能启动；在构建容器里能复现 |
| **开源**（open source） | GPL-3.0-or-later，无第三方二进制组件 | 全部源码在仓库内 |

## 系统构成

DOS 兼容性**不进入内核**，而由用户空间的一个独立虚拟机提供：

```
用户空间
├── shell / 工具
└── dosvm                  ← 自研的用户态 DOS 虚拟机
    ├── cpu/               i486SX 解释器
    ├── dev/               VGA 文本、键盘、块设备
    └── dos/               INT 21h 服务实现
────────────────────────────────────────────
内核
├── mm/  irq/  sched/      现代内核基础
├── dev/                   帧缓冲、键盘、块设备、网络
├── fs/                    FAT（供本系统自身使用）
└── arch/x86_64/           UEFI、长模式、分页
```

内核只给虚拟机三样东西：一块共享帧缓冲、一条输入事件通道、一个块设备。
内核完全不知道虚拟机里在跑什么。

## 为什么自己实现虚拟机

不移植 DOSBox，也不借用 v86 / QEMU 的核心。三个理由：

1. **许可干净。** DOSBox 与 QEMU 的 CPU 核心是 GPLv2，和 GPLv3 项目混在一起有
   v2-only 的兼容风险。全部自研就没有这个问题。
2. **能砍掉不需要的部分。** 完整 PC 模拟要图形、DMA、声卡、软驱、保护模式扩展器，
   而我们起步只需要 8086 实模式 + 文本 + 键盘 + 块设备。
3. **调试体验可控。** 自己写的解释器可以单步、可以打日志；移植来的黑盒核心出问题，
   只能对着别人的代码猜。

## CPU 目标：i486SX

虚拟机对 guest 呈现一台 **i486SX**：486 的完整整数指令集，**没有浮点协处理器**。

| 世代 | 承诺内容 | 里程碑 |
|---|---|---|
| 8086 / 80186 | 实模式完整指令集 | VM0 |
| 80286 | 保护模式指令（`LGDT`/`LIDT`/`LLDT`/`LTR`/`ARPL`） | VM4 |
| 386 | 32 位寄存器与寻址、`ModRM`+`SIB`、保护模式、分页、V86、特权级 | VM4–VM5 |
| 486 | `BSWAP`、`CMPXCHG`、`XADD`、`INVD`、`WBINVD`、`INVLPG`、`CR4` | VM6 |
| x87 FPU | **不提供**（SX 的定义就是没有 FPU） | 见[以后的事](#以后的事) |

因为报的是 SX，程序查询协处理器时会得到"不存在"，于是走各自的软件浮点路径——这是
1985 到 1995 年间绝大多数 DOS 软件本来就准备好了的退路。

实现上要保证：`CR0.EM` / `CR0.TS` 正确设置，`INT 11h` 设备字 bit 1 为 0，让浮点
指令触发"设备不可用"异常，而不是执行或崩溃。

## 兼容性范围

分三层，只有第一层是承诺：

| 层级 | 内容 | 状态 |
|---|---|---|
| **承诺** | i486SX 完整整数指令集（实模式 / 保护模式 / V86）、`.COM` / `.EXE`、`INT 21h` 常用子集、VGA 文本、键盘 | VM0–VM6 |
| **计划** | VGA 模式 13h、`INT 10h` 图形子集、块设备与文件访问 | 不设时间 |
| **以后** | 见[以后的事](#以后的事) | 不承诺 |

## 进展

| Day | 做了什么 | 验收 |
|---|---|---|
| 1 | `include/dospp/types.h`：类型词汇表 + 编译期自检 | 语法检查通过 |
| 2 | meson 构建 + `vm/main.cpp` | `meson compile` 通过，`dosvm.exe` 输出正常 |
| 3 | `vm/cpu/regs.h`、`vm/cpu/mem.h` + 单元测试 | `meson test` 全绿 |
| 4 | `vm/cpu/decode.h`、`exec.cpp`：取指、前缀、ModRM、MOV/INT/HLT | 25 项断言，涵盖段覆盖与 bp 走 SS |
| 5 | `vm/dos/loader.cpp`、`int21.cpp`、`vm.cpp`、`main.cpp` | 跑通 `hello.com`，输出 `Hello, World!` |
| 6 | `vm/dos/{hostfs,files}.cpp`：句柄表 + 宿主文件系统接口；`AH=3D/3E/3F/40` | 跑通 `cat.com`，读出真实宿主文件 |
| 7 | `AH=4E/4F` 目录查找 + DTA、`AH=1A` 设 DTA、`AH=3B` 切目录、`AH=01/08` 读键 | 95 项断言全绿 |
| 8 | `vm/dos/exe.cpp` MZ 装载 + 重定位；`psp.cpp` 构造 PSP；执行器补 `MOV Sreg` | 跑通 `hello.exe`，120 项断言全绿 |
| 9 | `vm/dev/vga.cpp` 文本缓冲；`vm/dos/int10.cpp` 模式/光标/卷屏/电传 | 跑通 `tel.com`，232 项断言全绿 |
| 10 | `0F` 扩展区：`LGDT`/`LIDT`/`SGDT`/`SIDT`、`MOV CRn`、`CLTS`、`EA` 远跳；段装载按 `PE` 分岔 | 252 项断言全绿 |
| 11 | 异常投递（IDT 门、压栈帧、错误码）、`IRET`、`PUSH`/`POP` 组、`DIV`/`IDIV` | 271 项断言全绿 |
| 12 | `CPL`/`DPL`/`RPL` 三方检查、`LLDT`/`LTR`、LDT 查表 | 290 项断言全绿 |
| 13 | `cpu/mmu.h` 两级页表、4 MB 大页、`#PF` 错误码；全部取指与数据访问过页表 | 309 项断言全绿 |
| 14 | V86 段语义、敏感指令受 IOPL 限制、`INT` 反射回虚拟机、`IRET` 保 VM 位 | 324 项断言全绿 |
| 15 | `BSWAP`/`CMPXCHG`/`XADD`/`INVD`/`WBINVD`/`INVLPG`/`CR4` | 337 项断言全绿 |
| 16 | `kernel/`：Limine 协议、长模式入口、串口控制台、链接脚本、可引导 ISO | QEMU 启动并打印 `DOS++ kernel M0` / 254 MiB 可用内存 |

## 以后的事

以下功能技术上都能做，只是不在当前承诺范围内。按"解锁什么"排序：

| 功能 | 解锁什么 | 为什么现在不做 |
|---|---|---|
| **VGA 模式 13h** | 简单图形程序 | 成本最低（约 500 行），但要先把 CPU 核和设备接口做稳 |
| **VGA 寄存器级 + VESA** | 图形程序正确运行 | 2000+ 行，寄存器级调试代价高 |
| **动态重编译（JIT）** | 图形与保护模式程序流畅运行 | 解释器够跑办公软件；JIT 是独立工程 |
| **x87 FPU** | 电子表格、CAD、科学计算、3D 游戏 | 办公软件都有软件浮点退路；真要做可用宿主 80 位 `long double` |
| **DMA + Sound Blaster + OPL2** | 有声游戏 | DMA 不是独立功能，是被声卡和软驱拖出来的依赖 |
| **MMX / SSE** | 极少数晚期程序 | 超出 i486SX 目标 |

这张表的意义是：**它们不是"永远不做"，而是"没有排期"**。

为了保证将来都是增量而不是重来，架构上守三条：

1. CPU 核用**指令表驱动**，不在 `switch` 里写死某一代 CPU。
2. 段寄存器从一开始就带**描述符缓存**（`base`/`limit`/`attr`），实模式和保护模式共用。
3. 设备模型挂在**统一接口**后面（端口 I/O + 内存映射两个钩子）。

## 路线图

两条并行线，最后合流。

**内核线**

| 里程碑 | 目标 | 状态 |
|---|---|---|
| M0 | 引导进入长模式，帧缓冲输出 | **完成**（Limine 进长模式，串口输出；帧缓冲已读到参数，画字要等字体） |
| M1 | IDT + 带寄存器转储的异常处理 | 未开始 |
| M2 | 物理/虚拟内存管理 + 内核堆 | 未开始 |
| M3 | 帧缓冲控制台 + 键盘驱动 | 未开始 |
| M4 | 用户空间进程模型（ring 3、系统调用、ELF 加载） | 未开始 |
| M5 | virtio-blk + FAT，从磁盘加载用户程序 | 未开始 |
| M6 | shell | 未开始 |
| M7 | 可启动 ISO = 首个可分发版本 | 未开始 |

**虚拟机线**（可完全独立于内核开发，先在宿主机上跑）

| 里程碑 | 目标 | 状态 |
|---|---|---|
| VM0 | 8086 实模式 + `.COM` + `AH=09` / `AH=4C` | **完成**（能装载并执行 .COM，AH=09 / AH=4C 可用） |
| VM1 | `INT 21h` 文件与目录子集 + 键盘输入 | **完成** |
| VM2 | `.EXE` 加载 + PSP | **完成** |
| VM3 | 虚拟 VGA 文本 + `INT 10h` / `INT 16h` 子集 | **完成**（`INT 10h` 已实现；`INT 16h` 随键盘并入 VM1） |
| VM4 | 286/386 保护模式：GDT/LDT、特权级、异常与错误码 | **完成**（描述符、特权级、LDT/TSS、异常与 IRET 齐备；特权级栈切换留待 VM5） |
| VM5 | 分页 + V86 + 中断反射（可运行 CWSDPMI / DOS/4GW） | **完成** |
| VM6 | 486 指令补全（`BSWAP`/`CMPXCHG`/`XADD`/`INVLPG`） | **完成** |
| VM7 | 接入 dospp 内核（共享帧缓冲 + 输入通道 + 块设备） | 未开始 |

**合流点**：M7 与 VM7 都完成时，首个"能跑 DOS 程序的可启动 ISO"成立。

VM5 是虚拟机线的分水岭：只有 V86 与中断反射正确，保护模式程序才能在需要时调回
实模式 BIOS，DOS 扩展器（DOS/4GW、CWSDPMI）才真正可用。

## 发行计划

| 产物 | 说明 |
|---|---|
| `dospp-<ver>-x86_64.iso` | 光盘镜像，BIOS 与 UEFI 双引导 |
| `dospp-<ver>-x86_64.img` | 磁盘镜像，可直接写入 U 盘 |
| `dospp-<ver>-src.tar.xz` | 源码包，可完整重建 |
| `SHA256SUMS` | 校验和 |

引导层使用 **Limine**（BIOS + UEFI 单一二进制），不自研引导器。版本号采用语义化版本；
M7 之前不发布任何版本。

## 这不是什么

- **不是 DOSBox 的替代品。** 不追求游戏兼容性，游戏相关功能列在[以后的事](#以后的事)。
- **不支持浮点。** CPU 目标是 i486SX，依赖 x87 的程序跑不了。
- **不承诺在现代 PC 上开箱即用。** 首个版本面向 QEMU / VirtualBox，真机支持按设备
  逐个推进。
- **不是 fdpp 的 fork。** 仓库里没有 fdpp 源码，只在文档里参考它的 DOS 语义描述。
- **不是 Linux 发行版。** 没有 POSIX 兼容层。

## 构建

依赖：`clang` 或 `gcc`、`nasm`、`meson`、`ninja`、`xorriso`、`qemu-system-x86_64`。
内核用 **C17**，工具与用户空间用 **C++20**。

```sh
meson setup build
meson compile -C build
meson test -C build        # 十二组单元测试，337 项断言
./build/vm/dosvm.exe tests/progs/hello.com              # 跑一个真实的 .COM
./build/vm/dosvm.exe tests/progs/hello.exe              # 跑一个真实的 .EXE
./build/vm/dosvm.exe tests/progs/tel.com                # 走 INT 10h 写虚拟屏幕
./build/vm/dosvm.exe --root tests/progs tests/progs/cat.com   # 读宿主文件
```

### 内核

内核单独一套流程，用 `kernel/Makefile`：

```sh
cd kernel
make OUT=build ZIG=/path/to/zig          # 编译并链接出 build/kernel.elf
```

编译器是 clang（`--target=x86_64-unknown-none`），链接器借 **Zig 自带的 lld**。
这不是选型偏好，是环境所迫：MSYS2 的 lld 只编了 COFF 后端，连 ELF 对象都认不出；
mingw 的 GNU ld 又写死了 PE 逻辑，出不了 ELF。

ISO 用 limine 官方二进制加 xorriso 打：

```sh
xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot \
    -boot-load-size 4 -boot-info-table \
    --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
    --protective-msdos-label iso -o dospp.iso
limine bios-install dospp.iso
qemu-system-x86_64 -m 256M -cdrom dospp.iso -serial stdio -display none
```

`OUT` 可以指到仓库外。Windows 的「受控文件夹访问」会拦住未经允许的程序写「文档」目录，
Zig 恰好是这种新程序——踩过一次，症状是 `cannot open output file ... Permission denied`,
而同一目录下 clang 写得好好的。

### 网络与证书

如果本机用 hosts 做了加速（Watt Toolkit / Steam++ 之类），`github.com` 会指向
`127.0.0.1` 上的本地反代。git 默认的 openssl 后端不认它的自签证书，报错长这样：

```
fatal: unable to access '...': SSL certificate problem:
       unable to get local issuer certificate
```

让 git 改用 Windows 证书库即可（只需要配一次，写在仓库里）：

```sh
git config http.sslBackend schannel
```

这条配置对正常的 HTTPS 也有效，加速关掉之后不用改回来。

## 可重现构建

"可分发"等于"别人能构建出同一个镜像"。构建环境**以容器为准，不以文档为准**：
`docker/` 固定工具链版本，`.github/workflows/release.yml` 在打 tag 时于容器内产出
ISO、源码包与校验和。

## 许可

GPL-3.0-or-later。

本项目**不包含任何第三方模拟器或 DOS 实现代码**，虚拟机为独立实现。文档中参考的
DOS 行为描述来源于 fdpp（Copyright (C) 2018 Stas Sergeev）与 FreeDOS 内核（DOS-C），
仅作规格参考。详见 `docs/CREDITS.md`。
