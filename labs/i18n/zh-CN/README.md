# xv6 Labs

[English](../../README.md)

本目录包含 xv6 教学操作系统的实验内容，覆盖 Boot 启动、用户库、内存管理、文件系统、调度、进程管理和同步七大系列，共 29 个实验。

## 可用实验

### Boot 启动系列

围绕 xv6 启动链（bootasm → bootmain → entry → main → initcode）的一系列改造实验。每个实验独立完成，也可组合实现更复杂的启动体验。编号越小越基础，建议按顺序完成。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-boot-01-vga](../../lab-boot-01-vga/) | ★☆☆☆☆ | VGA 文本模式启动信息打印 | MMIO, VGA 缓冲区, 512 字节限制 |
| 02 | [lab-boot-02-stage2](../../lab-boot-02-stage2/) | ★★★☆☆ | 多阶段引导加载 | Stage 1/2, 磁盘布局, ELF 加载 |
| 03 | [lab-boot-03-memdetect](../../lab-boot-03-memdetect/) | ★★★★☆ | 实模式 E820 内存检测 | BIOS 中断, 实/保护模式, bootloader-内核数据传递 |
| 04 | [lab-boot-04-multiboot](../../lab-boot-04-multiboot/) | ★★★★☆ | GRUB Multiboot 兼容启动 | Multiboot 规范, ELF, 双重启动路径 |
| 05 | [lab-boot-05-graphic](../../lab-boot-05-graphic/) | ★★★★☆ | VGA 图形模式启动画面 | VBE, 帧缓冲区, 位图字体, 分页映射 |
| 06 | [lab-boot-06-customfmt](../../lab-boot-06-customfmt/) | ★★★★★ | 自定义内核镜像格式替代 ELF | 可执行文件格式, 链接脚本, 构建工具链 |

### 其他独立实验

| 实验 | 难度 | 描述 | 关键文件 |
|------|------|------|----------|
| [lab-userspace](../../lab-userspace/) | ★★☆☆☆ | 用户身份与文件权限管理 | src/sysproc.c, src/sysfile.c, src/fs.c |
| [lab-userspace-01-shell-edit](../../lab-userspace-01-shell-edit/) | ★★★☆☆ | 交互式 shell 行编辑（Tab 补全 + 历史命令） | user/sh.c |
| [lab-fifo-sched](../../lab-fifo-sched/) | ★★☆☆☆ | FIFO 非抢占式队列调度器 | src/proc.c, src/trap.c, include/proc.h |

### lib 用户库系列

在 xv6 用户态构建完整的运行时库，从内存分配到格式化输出再到协程调度。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-lib-01-malloc](../../lab-lib-01-malloc/) | ★★★☆☆ | 用户态堆分配器（显式空闲链表） | sbrk, first-fit, 边界合并 |
| 02 | [lab-lib-02-printf](../../lab-lib-02-printf/) | ★★☆☆☆ | 完整 printf 格式引擎 | va_list, 格式说明符, 缓冲输出 |
| 03 | [lab-lib-03-thread](../../lab-lib-03-thread/) | ★★★★☆ | 用户态协程库（setjmp/longjmp） | TCB, 协作式调度, 上下文切换 |

### mm 内存管理系列

从内核对象分配器到写时复制，逐步掌握 xv6 的虚拟内存管理。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-mm-01-slab](../../lab-mm-01-slab/) | ★★★★☆ | Slab 分配器替代 kalloc | kmem_cache, slab, 内部碎片 |
| 02 | [lab-mm-02-cow](../../lab-mm-02-cow/) | ★★★★☆ | 写时复制 fork | PTE_COW, 引用计数, 页错误处理 |
| 03 | [lab-mm-03-lazy](../../lab-mm-03-lazy/) | ★★★☆☆ | 延迟分配（Lazy Allocation） | sbrk 惰性, 按需分页, T_PGFLT |
| 04 | [lab-mm-04-mmap](../../lab-mm-04-mmap/) | ★★★★★ | mmap/munmap 系统调用 | VMA, 文件映射, MAP_SHARED |

### fs 文件系统系列

深入 xv6 的 Unix FFS 文件系统，从容量扩展到崩溃一致性。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-fs-01-bigfile](../../lab-fs-01-bigfile/) | ★★★☆☆ | 双重间接块（大文件支持） | 二级间接块, bmap, itrunc |
| 02 | [lab-fs-02-symlink](../../lab-fs-02-symlink/) | ★★★☆☆ | 符号链接 | T_SYMLINK, namei, O_NOFOLLOW |
| 03 | [lab-fs-03-lrucache](../../lab-fs-03-lrucache/) | ★★★★☆ | LRU 缓冲区缓存 | LRU 链表, 冷热数据分离, bget/brelse |
| 04 | [lab-fs-04-crash](../../lab-fs-04-crash/) | ★★★★★ | 崩溃一致性实验 | WAL, commit 阶段, fscheck 工具 |

### sched 调度系列

实现三种经典调度策略，从静态优先级到按比例共享。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-sched-01-priority](../../lab-sched-01-priority/) | ★★★☆☆ | 静态优先级调度器 | priority 字段, 优先级反转 |
| 02 | [lab-sched-02-mlfq](../../lab-sched-02-mlfq/) | ★★★★☆ | 多级反馈队列（MLFQ） | 3 级队列, 时间量, 提升机制 |
| 03 | [lab-sched-03-stride](../../lab-sched-03-stride/) | ★★★★☆ | 步长调度（比例共享） | tickets, stride, pass, settickets |

### proc 进程管理系列

扩展 xv6 的进程模型，实现信号、精确 wait 和共享内存。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-proc-01-signal](../../lab-proc-01-signal/) | ★★★★☆ | Unix 信号机制 | signal frame, sigreturn, sig_pending |
| 02 | [lab-proc-02-waitpid](../../lab-proc-02-waitpid/) | ★★★☆☆ | waitpid 与进程组 | WNOHANG, pgid, 孤儿/僵尸进程 |
| 03 | [lab-proc-03-shm](../../lab-proc-03-shm/) | ★★★★☆ | 共享内存 IPC | 页表共享, 引用计数, shmget/shmat |

### sync 同步系列

从原子操作构建用户态同步原语，理解经典并发问题。

| # | 实验 | 难度 | 描述 | 关键概念 |
|---|------|------|------|----------|
| 01 | [lab-sync-01-mutex](../../lab-sync-01-mutex/) | ★★★☆☆ | 用户态互斥锁 | xchg 原子操作, 自旋 vs 睡眠, futex |
| 02 | [lab-sync-02-rwlock](../../lab-sync-02-rwlock/) | ★★★★☆ | 读写锁 | 共享锁/独占锁, 写者优先, 饥饿 |
| 03 | [lab-sync-03-semaphore](../../lab-sync-03-semaphore/) | ★★★☆☆ | 信号量 | P/V 操作, 生产者-消费者, 哲学家就餐 |

## 推荐学习路径

```
入门 (★☆ ~ ★★☆)
  lab-boot-01-vga → lab-lib-02-printf → lab-mm-03-lazy
  lab-proc-02-waitpid → lab-sync-01-mutex → lab-sync-03-semaphore

进阶 (★★★☆)
  lab-boot-02-stage2 → lab-boot-03-memdetect → lab-lib-01-malloc
  lab-sched-01-priority → lab-fs-01-bigfile → lab-fs-02-symlink

挑战 (★★★★ ~ ★★★★★)
  lab-mm-02-cow → lab-mm-04-mmap → lab-fs-03-lrucache → lab-fs-04-crash
  lab-sched-02-mlfq → lab-sched-03-stride → lab-proc-01-signal
  lab-lib-03-thread → lab-sync-02-rwlock → lab-proc-03-shm

组合挑战
  lab-proc-01-signal + lab-proc-02-waitpid → 完整 POSIX 进程模型
  lab-proc-03-shm + lab-sync-03-semaphore → 跨进程生产者-消费者
  lab-mm-02-cow + lab-mm-04-mmap → 现代 VM 子系统
```

## Boot 实验依赖关系

```
lab-boot-01-vga (入门, 无前置依赖)
    │
    ├── lab-boot-02-stage2 (突破 512 字节限制)
    │       │
    │       ├── lab-boot-05-graphic (Stage 2 有足够空间放字体和绘图代码)
    │       └── lab-boot-06-customfmt (Stage 2 可支持压缩等高级特性)
    │
    ├── lab-boot-03-memdetect (在 bootasm.S 实模式段插入 E820 检测)
    │
    └── lab-boot-04-multiboot (替代整个自举 bootloader, 可与 memdetect 结合)

可组合: memdetect + multiboot, stage2 + graphic, stage2 + customfmt
```

## 目录结构

```
labs/
├── README.md              # 本文件
├── lab-template/          # 新实验模板
│
├── lab-boot-01-vga/       # 启动信息打印
├── lab-boot-02-stage2/    # 多阶段引导
├── lab-boot-03-memdetect/ # 内存检测
├── lab-boot-04-multiboot/ # GRUB Multiboot
├── lab-boot-05-graphic/   # 图形模式启动画面
├── lab-boot-06-customfmt/ # 自定义内核镜像格式
│
├── lab-userspace/         # 用户身份与文件权限
├── lab-userspace-01-shell-edit/ # shell 行编辑（Tab + 历史）
├── lab-fifo-sched/        # FIFO 非抢占式调度器
│
├── lab-lib-01-malloc/     # 用户态堆分配器
├── lab-lib-02-printf/     # printf 格式引擎
├── lab-lib-03-thread/     # 用户态协程库
│
├── lab-mm-01-slab/        # Slab 分配器
├── lab-mm-02-cow/         # 写时复制 fork
├── lab-mm-03-lazy/        # 延迟分配
├── lab-mm-04-mmap/        # mmap/munmap
│
├── lab-fs-01-bigfile/     # 双重间接块
├── lab-fs-02-symlink/     # 符号链接
├── lab-fs-03-lrucache/    # LRU 缓冲区缓存
├── lab-fs-04-crash/       # 崩溃一致性
│
├── lab-sched-01-priority/ # 静态优先级调度
├── lab-sched-02-mlfq/     # 多级反馈队列
├── lab-sched-03-stride/   # 步长调度
│
├── lab-proc-01-signal/    # Unix 信号机制
├── lab-proc-02-waitpid/   # waitpid 与进程组
├── lab-proc-03-shm/       # 共享内存 IPC
│
├── lab-sync-01-mutex/     # 用户态互斥锁
├── lab-sync-02-rwlock/    # 读写锁
└── lab-sync-03-semaphore/ # 信号量
```

## 如何开始新实验

1. 复制模板: `cp -r labs/lab-template labs/lab-<name>`
2. 编辑 `labs/lab-<name>/README.md` 描述实验目标和步骤
3. 在新 git 分支上开发: `git checkout -b lab-<name>`

## 构建和运行

```bash
make                    # 编译
make qemu-nox           # 运行 (Ctrl+A X 退出)
make lab-list           # 列出可用实验
```
