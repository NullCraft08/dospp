# 致谢与来源

## 本项目的代码

全部原创。仓库中**没有任何第三方模拟器、DOS 实现或操作系统内核的源码**。

`kernel/limine.h` 是 Limine 引导协议的官方头文件（BSD-2-Clause，Copyright (C) 2019-2025
the Limine authors），仅用于描述启动协议的数据结构，不含可执行逻辑。

## 行为规格参考

实现 DOS 服务时，参考了以下项目描述的**行为**（接口语义、错误码、数据结构布局），
但没有复制其代码：

- **fdpp**（FreeDOS 内核的 C++ 移植，Copyright (C) 2018 Stas Sergeev）——
  `INT 21h` 各子功能的参数与错误码约定。
- **FreeDOS 内核**（DOS-C，Copyright (C) 1998-2011 Pasquale J. Villani 等）——
  PSP 布局、内存控制块、DPB 结构等数据结构的定义。
- **IBM PC Technical Reference / Ralf Brown's Interrupt List** ——
  中断与端口的事实标准。

依据 GPL 的"独立创作"原则，这些都是**规格**而非**代码**：一边是可被任何人从文档里
读到的接口约定，另一边是表达方式不同的实现。本项目的虚拟机是后者。

## 引导

- **Limine**：BIOS + UEFI 单一镜像的引导层，本项目直接使用其官方二进制，不自研引导器。