# xv6-public

> 学习操作系统最好的方式就是动手实践。

xv6 是 MIT 6.828/6.S081 课程的教学操作系统，在 x86 多处理器上重新实现了 Unix Version 6。本项目以 xv6 为起点，提供了一套面向操作系统实践的学习路径。

**[English](../../README.md)** | **中文**

## 为什么做这个项目

操作系统的学习思路从未改变——阅读源码、理解设计、动手修改、验证结果。但 AI 辅助工具大幅降低了调试和解决问题的成本，让学习循环变得更快。

本项目在原始 xv6-public 基础上做了两件事：

- **工程结构现代化** — 将平铺的源文件重组为现代项目目录（`boot/`, `kernel/`, `user/`, `include/` 等），更贴近真实工程的组织方式
- **Lab 实验体系** — 设计了一套覆盖操作系统核心子系统的实验，配合 AI Skill 可以边学边练

## 目录结构

```
xv6-public/
├── boot/          引导加载程序 (bootasm.S, bootmain.c, kernel.ld)
├── kernel/        内核源码 + 内核头文件
├── user/          用户程序 + 用户库 (cat, echo, sh, ls, grep, ...)
├── include/       内核/用户共享头文件 (types.h, syscall.h, ...)
├── tools/         构建工具 (mkfs, vectors.pl, sign.pl, ...)
├── docs/          文档
├── labs/          Lab 实验设计文档
├── lab-Tests/     Lab 测试代码和验证脚本
├── Makefile       顶层构建文件
└── build/         编译产物目录 (make 自动创建)
```

## 快速开始

### 环境准备

```bash
# Ubuntu / Debian
sudo apt-get install gcc-multilib qemu-system-x86

# macOS (需要交叉编译工具链)
# 参考 https://pdos.csail.mit.edu/6.828/
```

### 编译和运行

```bash
git clone https://github.com/zjb1001/xv6-public.git
cd xv6-public

make              # 编译内核和用户程序
make qemu         # QEMU 运行 (带窗口)
make qemu-nox     # QEMU 运行 (纯终端)
make qemu-gdb     # QEMU + GDB 调试
make clean        # 清理编译产物
```

启动后看到 `$` 提示符即表示进入 xv6 shell，可以运行 `ls`, `cat README`, `echo hello` 等命令。按 `Ctrl-A X` 退出 QEMU。

### GDB 调试

```bash
# 终端 1: 启动 QEMU (等待 GDB 连接)
make qemu-gdb

# 终端 2: 启动 GDB
gdb build/xv6kernel -x .gdbinit
```

## Lab 实验体系

基于 xv6 各子系统设计了一系列 Lab，每个 Lab 包含设计文档和自动化测试：

| 类别 | Lab | 核心知识点 |
|------|-----|-----------|
| **启动** | boot-01-vga | VGA 文本模式输出 |
| | boot-02-stage2 | 两阶段引导加载 |
| | boot-03-memdetect | E820 内存探测 |
| | boot-04-multiboot | GRUB Multiboot 协议 |
| **调度** | sched-01-priority | 静态优先级调度 |
| | sched-02-mlfq | 多级反馈队列 |
| | sched-03-stride | Stride 比例份额调度 |
| **内存** | mm-01-slab | Slab 分配器 |
| | mm-02-cow | Copy-on-Write 写时复制 |
| | mm-03-lazy | Lazy Page Allocation |
| | mm-04-mmap | mmap 内存映射 |
| **文件系统** | fs-01-bigfile | 大文件支持 |
| | fs-02-symlink | 符号链接 |
| | fs-03-lrucache | LRU 缓冲区缓存 |
| | fs-04-crash | 崩溃一致性 |
| **同步** | sync-01-mutex | 互斥锁 |
| | sync-02-rwlock | 读写锁 |
| | sync-03-semaphore | 信号量 |
| **进程** | proc-01-signal | 信号机制 |
| | proc-02-waitpid | waitpid 系统调用 |
| | proc-03-shm | 共享内存 |
| **用户库** | lib-01-malloc | malloc 内存分配器 |
| | lib-02-printf | printf 格式化输出 |
| | lib-03-thread | 用户级线程 |

### 运行 Lab

每个 Lab 在 `lab-Tests/<lab-name>/` 目录下有独立的 Makefile：

```bash
cd lab-Tests/<lab-name>

make              # 应用补丁 + 编译
make qemu-nox     # 应用补丁 + 编译 + 运行
make apply        # 仅应用补丁 (幂等，可重复执行)
make unapply      # 撤销所有补丁 (git restore)
make clean        # 清理编译产物 (不撤销补丁)
```

`apply` 将实验代码补丁到 xv6 源码树中，`unapply` 通过 `git restore` 恢复原始代码。

## AI Skill 增强学习

本项目提供了一套 Claude Code Skill，将 AI 能力与 xv6 学习深度结合：

| Skill | 命令 | 用途 |
|-------|------|------|
| **xv6-dev** | `/xv6-dev` | 开发新功能 — 编码同时获得 OS 概念解释 + 自动代码评审 |
| **xv6-explain** | `/xv6-explain` | 解释代码 — 将源码映射到 OS 教材概念，自动追踪执行路径 |
| **xv6-simulate** | `/xv6-simulate` | 模拟执行 — 逐步追踪系统调用、调度、内存操作的完整过程 |
| **xv6-review** | `/xv6-review` | 代码评审 — 从 OS 设计角度评审变更，分析正确性 |
| **xv6-debug** | `/xv6-debug` | 诊断问题 — 定位启动失败、内核 panic、死锁等问题的根因 |

### 使用示例

```
# 解释一个概念
/xv6-explain xv6 的进程调度是如何工作的？

# 模拟一次系统调用的完整执行过程
/xv6-simulate 追踪 write 系统调用从用户态到内核态的完整路径

# 开发一个新功能
/xv6-dev 在 xv6 中实现信号量机制

# 评审代码变更
/xv6-review 评审最近的 commit

# 诊断运行问题
/xv6-debug xv6 启动后卡在 scheduler 不动
```

## 技术细节

### 内核架构

xv6 实现了完整的 Unix V6 核心功能：

- **进程管理** — PCB (struct proc)、上下文切换 (swtch.S)、Round-Robin 调度
- **虚拟内存** — 两级页表、4KB 页、内核/用户地址空间隔离
- **文件系统** — Unix FFS、inode、缓冲区缓存、预写日志 (WAL)
- **系统调用** — 21 个系统调用，通过 INT 64 进入内核
- **同步机制** — 自旋锁 + 睡眠锁，支持多处理器 (SMP)
- **中断处理** — IDT、trap frame、LAPIC/IOAPIC

### 内存布局

```
物理内存:
0x00000000 ┌──────────────┐
           │   I/O Space  │
0x00100000 ├──────────────┤
           │  Kernel Code │
           │  Free Pages  │
0xE0000000 ├──────────────┤ PHYSTOP
0xFE000000 ├──────────────┤
           │  MMIO Devices│
0xFFFFFFFF └──────────────┘

进程虚拟地址空间:
0x00000000 ┌──────────────┐
           │  User Text   │
           │  User Heap   │ (sbrk)
           │  User Stack  │
0x80000000 ├──────────────┤ KERNBASE
           │ Kernel Text  │ (P2V 映射)
0xFE000000 ├──────────────┤
           │  Devices     │
0xFFFFFFFF └──────────────┘
```

## 致谢

- **xv6 原作者** — Frans Kaashoek, Robert Morris, Russ Cox (MIT PDOS)
- **原始项目** — [mit-pdos/xv6-public](https://github.com/mit-pdos/xv6-public)
- xv6 的设计灵感来源于 John Lions 的 *Commentary on UNIX 6th Edition*

## 许可证

原始 xv6 代码版权属于 Frans Kaashoek, Robert Morris 和 Russ Cox (2006-2018)。
本项目在原始代码基础上进行了重构和扩展，遵循原始许可证。
