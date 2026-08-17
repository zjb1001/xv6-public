# os-concepts.md — OS 概念 → xv6 映射表（单一事实源）

> **消费者**：xv6-explainer（必读）、xv6-simulator（必读）、xv6-developer（按需）、xv6-debugger（按需）、xv6-reviewer（按需）
> **维护规则**：这是唯一事实源。修改 OS 概念映射只改本文件，禁止在 skill/agent 内另建映射表。
<!-- canonical: OS术语对照表、架构概览表、内存布局图、关键常量、执行流程（启动序列/第一进程/系统调用路径）、子系统设计决策、学习路径、对比表 -->

本文件把 OS 教材概念映射到 xv6（**x86 版**，非 RISC-V）的具体文件、函数与设计权衡。
用途：解释设计决策、模拟执行路径、实现新功能、评审变更时的概念锚点。

## 核心术语对照

| English | 中文 | xv6 中的体现 |
|---------|------|-------------|
| Process | 进程 | struct proc, kernel/proc.c |
| Scheduling | 调度 | scheduler(), round-robin |
| Context Switch | 上下文切换 | kernel/swtch.S, sched() |
| Page Table | 页表 | walkpgdir(), kernel/vm.c |
| Virtual Memory | 虚拟内存 | allocuvm(), deallocuvm() |
| System Call | 系统调用 | kernel/syscall.c, user/usys.S, INT 64 |
| Inode | 索引节点 | kernel/fs.c: ialloc, iget, ilock |
| Buffer Cache | 缓冲区缓存 | kernel/bio.c: bread, brelse |
| Write-Ahead Log | 预写日志 | kernel/log.c: begin_op, end_op |
| Spinlock | 自旋锁 | kernel/spinlock.c: acquire, release |
| Sleep Lock | 睡眠锁 | kernel/sleeplock.c: acquiresleep |
| Trap | 陷阱/中断 | kernel/trap.c, kernel/trapasm.S |
| File Descriptor | 文件描述符 | struct file, kernel/file.c |
| Pipe | 管道 | kernel/pipe.c: piperead, pipewrite |
| Bootloader | 引导加载程序 | boot/bootasm.S, boot/bootmain.c |

## 子系统设计决策速查

### 进程管理 (proc.c, proc.h)

**为什么 struct proc 不用动态分配？**
- xv6 用静态数组 `struct proc proc[NPROC]` (NPROC=64)
- 替代方案: 链表、slab 分配器、Linux 的 task_struct + kmalloc
- 权衡: 静态数组简单但限制最大进程数；Linux 动态分配更灵活但复杂
- 简化: Linux 有线程/进程区分、namespace、cgroup，xv6 全部省略

**为什么上下文切换用汇编 (swtch.S)？**
- C 无法控制寄存器保存/恢复的精确顺序
- swtch 只保存 callee-saved 寄存器 (edi, esi, ebx, ebp, eip)
- caller-saved 寄存器由编译器在调用 swtch 前自动保存
- Linux 类似: context_switch -> switch_to 宏（但更复杂，处理 TLB 等）

**为什么用 Round-Robin 调度？**
- 最简单的公平调度算法，每个进程一个时间片
- 替代方案: 优先级调度、MLFQ、CFS (Linux)
- 权衡: RR 简单但不区分优先级；CFS 更公平但实现复杂
- 教学价值: RR 是理解调度的起点

### 内存管理 (vm.c, kalloc.c)

**为什么用两级页表而不是一级？**
- x86 硬件规定的两级页表结构（页目录 + 页表）
- 一级页表需要 4MB 连续内存（1024 * 1024 * 4B），浪费巨大
- 两级页表只需为实际使用的虚拟地址分配页表页
- Linux: 四级页表 (PGD -> PUD -> PMD -> PTE)，支持更大地址空间

**为什么内核空间直接映射（P2V/V2P）？**
- 内核虚拟地址 = 物理地址 + KERNBASE
- 简化内核对物理内存的访问：不需要为每个物理页建映射
- 替代方案: Linux 使用独立的内核映射 + vmalloc
- 权衡: 直接映射简单但不灵活；Linux 方式支持内核虚拟地址不连续

**为什么 kalloc 用链表而非 buddy 系统？**
- kalloc 维护一个空闲页链表 (struct run)
- 替代方案: buddy 分配器、slab 分配器 (Linux)
- 权衡: 链表简单但无法合并、只能分配整页；buddy 可以分裂/合并
- 教学价值: 理解物理内存分配的最基本方法

### 文件系统 (fs.c, bio.c, log.c)

**为什么用 inode 而不是 FAT？**
- Unix 传统：inode 将文件元数据与目录项分离
- 优势: 硬链接简单（多个目录项指向同一 inode）
- 替代方案: FAT（简单但无硬链接）、ext4（更复杂的树结构）
- xv6 的 inode: 12 个直接块 + 1 个间接块，最大文件 ~140KB * 4KB

**为什么需要缓冲区缓存 (bio.c)？**
- 磁盘 I/O 比 CPU 慢几个数量级
- bread() 先查缓存，命中则不读磁盘
- 替代方案: Linux 的 page cache + buffer head（更复杂）
- xv6 简化: 固定大小缓存 (NBUF)，LRU 替换策略

**为什么需要预写日志 (log.c)？**
- 崩溃一致性: 如果文件系统操作中途断电，磁盘状态可能不一致
- WAL (Write-Ahead Logging): 先写日志，再写实际数据
- 替代方案: copy-on-write (btrfs)、软更新 (soft updates)
- OSTEP Ch.24 的核心主题

### 同步 (spinlock.c, sleeplock.c)

**为什么自旋锁要禁用中断 (pushcli/popcli)？**
- 防止死锁: 如果 CPU 持有锁时被中断，中断处理程序可能再次请求同一把锁
- pushcli/popcli 嵌套计数: 允许在已禁用中断的情况下获取多把锁
- Linux: 每个锁跟踪所属中断处理程序，更精细但更复杂

**为什么有 sleeplock 和 spinlock 两种？**
- spinlock: 短时间持有，忙等，中断禁用 — 适合保护简短操作
- sleeplock: 长时间持有，睡眠等待，允许中断 — 适合磁盘 I/O 等慢操作
- xv6 设计: 磁盘 I/O 期间不能持有自旋锁（会导致死锁），用 sleeplock

### 系统调用 (syscall.c, usys.S)

**为什么系统调用通过 INT 64（陷阱）实现？**
- 用户态无法直接调用内核代码（特权级隔离）
- INT 指令触发陷阱 -> 硬件切换到内核态 -> 跳转到 IDT 中的处理程序
- 替代方案: sysenter/sysexit (x86 快速系统调用)、Linux 用 syscall 指令
- xv6 简化: INT 64 慢但简单；Linux 用 MSR 寄存器设置快速入口

**为什么用 argint/argptr/argstr 提取参数？**
- 系统调用参数在用户栈上，内核不能直接信任用户指针
- argint: 从用户栈读取整数
- argptr: 读取指针并验证地址范围
- argstr: 读取字符串并验证地址
- 安全性: 防止用户程序传入内核地址来读写内核内存

## 内存布局（canonical）

```
物理内存:
0x00000000 ┌─────────────────┐
           │   I/O Space     │ [0, EXTMEM=0x100000)
0x00100000 ├─────────────────┤
           │   Kernel Code   │
           │   Kernel Data   │
           │   Free Pages    │ [kalloc 管理的空闲内存]
0xE0000000 ├─────────────────┤ PHYSTOP
           │   (unmapped)    │
0xFE000000 ├─────────────────┤ DEVSPACE
           │   MMIO Devices  │
0xFFFFFFFF └─────────────────┘

进程虚拟地址空间:
0x00000000 ┌─────────────────┐
           │   User Text     │ (ELF 加载, 只读/执行)
           │   User Data/BSS │
           │   User Heap     │ (sbrk 向上增长)
           │   ...           │
           │   User Stack    │ (1 页, 向下增长)
0x80000000 ├─────────────────┤ KERNBASE
           │   Kernel Text   │ (通过 P2V 映射到物理地址)
           │   Kernel Data   │
           │   Free Memory   │
0xFE000000 ├─────────────────┤
           │   Devices       │ (恒等映射)
0xFFFFFFFF └─────────────────┘
```

## 执行流程（canonical）

### 启动序列
BIOS -> boot/bootasm.S (实模式->保护模式, 开分页) -> boot/bootmain.c (从磁盘加载 ELF 内核) -> kernel/entry.S (设栈, 跳 main) -> kernel/main.c -> kinit1 -> kvmalloc -> mpinit -> lapicinit -> seginit -> ... -> userinit -> scheduler()

### 第一个用户进程
kernel/initcode.S (用户模式, exec("/init")) -> user/init.c (打开 console, fork+exec sh) -> user/sh.c (shell)

## 学习路径推荐

对于不同的 OS 概念，推荐阅读顺序：

1. **进程**: proc.h (struct proc) -> proc.c:allocproc -> proc.c:fork -> proc.c:scheduler
2. **调度**: proc.c:scheduler -> trap.c (timer interrupt) -> proc.c:yield
3. **内存**: memlayout.h -> kalloc.c -> vm.c:setupkvm -> vm.c:allocuvm
4. **系统调用**: usys.S -> trapasm.S -> trap.c -> syscall.c -> sysproc.c
5. **文件系统**: fs.h (数据结构) -> bio.c -> fs.c -> log.c -> file.c
6. **同步**: spinlock.c -> sleeplock.c -> proc.c:sleep/wakeup
7. **启动**: bootasm.S -> bootmain.c -> entry.S -> main.c -> userinit -> initcode.S

## 系统调用执行路径（全链路）

```
用户代码 (user/usys.S: movl $SYS_xxx, %eax; int $T_SYSCALL)
  -> build/vectors.S -> kernel/trapasm.S (构建 trapframe)
  -> kernel/trap.c -> kernel/syscall.c (查 syscalls[] 表)
  -> sys_xxx() -> 返回值写入 tf->eax -> trapret -> iret -> 用户态
```

## xv6 vs Linux 对比总表

| 方面 | xv6 | Linux |
|------|-----|-------|
| 进程锁 | 全局 ptable.lock | 每进程 RCU + 细粒度锁 |
| 调度 | RR，全局队列 | CFS，每 CPU 运行队列 |
| 内存分配 | 链表 kalloc | buddy + slab |
| 页表 | 两级，无 swap | 四级，支持 swap/THP |
| FS 缓冲 | 固定大小 buf cache | page cache + writeback |
| FS 日志 | 固定大小 WAL | jbd2，动态事务 |
| 系统调用 | INT 64 (慢) | syscall 指令 (快) |
