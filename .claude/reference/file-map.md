# file-map.md — xv6 目录/文件地图 + 修改清单（单一事实源）

> **消费者**：xv6-developer（必读）、xv6-reviewer（必读）、xv6-debugger（必读）、xv6-explainer（按需）、xv6-simulator（按需）
> **维护规则**：唯一事实源。修改清单、目录规范只改本文件。
<!-- canonical: 目录结构、架构→源文件表、文件职责、修改清单、Lab目录/编译规范 -->

## 目录结构

```
xv6-public/
├── boot/          # 引导加载程序
├── kernel/        # 内核源码 + 内核头文件
├── user/          # 用户程序 + 用户库
├── include/       # 内核/用户共享头文件 (types.h, stat.h, fcntl.h, syscall.h, traps.h, elf.h, fs.h)
├── tools/         # 构建工具和脚本 (mkfs, vectors.pl, sign.pl, runoff, ...)
├── docs/          # 文档 (README, Notes, TRICKS, BUGS)
├── labs/          # 实验设计文档
├── lab-Tests/     # 实验测试代码
├── Makefile       # 顶层构建文件
└── build/         # 编译产物目录 (由 make 自动创建)
```

## 架构 → 源文件

| 子系统 | 源文件 | 核心概念 |
|--------|--------|---------|
| 启动 | boot/bootasm.S, boot/bootmain.c, kernel/entry.S, kernel/main.c | Multiboot、保护模式、分页、GDT/IDT |
| 进程 | kernel/proc.c, kernel/proc.h, kernel/swtch.S | PCB (struct proc)、上下文切换、调度器、状态机 |
| 内存 | kernel/vm.c, kernel/kalloc.c, kernel/memlayout.h, kernel/mmu.h | 两级页表、4KB 页、内核/用户地址空间 |
| 文件系统 | kernel/fs.c, include/fs.h, kernel/bio.c, kernel/file.c, kernel/file.h, kernel/log.c | Unix FFS、inode、缓冲区缓存、预写日志 |
| I/O 设备 | kernel/console.c, kernel/uart.c, kernel/kbd.c, kernel/ide.c | 轮询 I/O、中断驱动磁盘 |
| 陷阱/中断 | kernel/trap.c, kernel/trapasm.S, build/vectors.S | IDT、trap frame、系统调用通过 INT 64 |
| 系统调用 | kernel/syscall.c, include/syscall.h, kernel/sysproc.c, kernel/sysfile.c | 21 个系统调用、argint/argptr/argstr |
| 同步 | kernel/spinlock.c, kernel/spinlock.h, kernel/sleeplock.c, kernel/sleeplock.h | 自旋锁 + pushcli/popcli、睡眠锁 via sleep/wakeup |
| 用户程序 | user/cat.c, user/echo.c, user/grep.c, user/sh.c, user/ls.c, ... | 简单 Unix 工具，链接 user/ulib.c |
| 中断控制 | kernel/picirq.c, kernel/ioapic.c, kernel/lapic.c, kernel/mp.c | APIC、IOAPIC、多处理器支持 |

## 关键常量 (kernel/param.h, kernel/memlayout.h)

- NPROC=64, NCPU=8, NOFILE=16, NFILE=100, NINODE=50
- KSTACKSIZE=4096 (4KB 内核栈), PGSIZE=4096
- KERNBASE=0x80000000, PHYSTOP=0xE0000000, EXTMEM=0x100000

## 修改清单（按修改类型）

修改任何功能时，检查是否涉及以下文件：

| 修改类型 | 必须检查的文件 |
|----------|---------------|
| 系统调用 | syscall.h, syscall.c, usys.S, user.h, sysproc.c/sysfile.c |
| 进程属性 | proc.h, proc.c, 可能 syscall.h |
| 调度 | proc.c:scheduler(), trap.c (timer interrupt) |
| 内存 | vm.c, kalloc.c, memlayout.h, 可能 proc.c |
| 文件系统 | fs.c, file.c, bio.c, log.c, file.h, fs.h |
| 设备驱动 | 对应的 .c 文件, 可能 trap.c (中断处理) |
| 同步原语 | spinlock.c, sleeplock.c, proc.c (sleep/wakeup) |

### 新增系统调用七步走

```
Step 1: syscall.h — 添加 SYS_xxx 编号
Step 2: syscall.c — extern 声明 + syscalls[] 表条目
Step 3: sysproc.c 或 sysfile.c — 实现 sys_xxx()
Step 4: user.h — 用户空间函数声明
Step 5: usys.S — SYSCALL(xxx) 宏
Step 6: Makefile UPROGS — 添加测试程序（如果有）
Step 7: 测试程序 — 编写用户态测试
```

执行路径参考:
```
用户调用 -> usys.S (movl $SYS_xxx, %eax; int $T_SYSCALL)
         -> vectors.S -> trapasm.S (构建 trapframe)
         -> trap.c:trap() -> syscall.c:syscall()
         -> syscalls[tf->eax] -> sys_xxx()
         -> 返回值写入 tf->eax -> trapret -> iret -> 用户态
```

### 新增内核数据结构的三种参考模式

1. **ptable** (proc.c): `struct { spinlock lock; struct proc proc[NPROC]; }`
2. **kmem** (kalloc.c): `struct { spinlock lock; struct run *freelist; }`
3. **ftable** (file.c): `struct { spinlock lock; struct file file[NFILE]; }`

步骤：定义结构体（嵌入 spinlock）→ 初始化函数（在 main.c 中调用）→ 操作函数（alloc/free/lookup）→ 锁保护的所有访问路径。

### 调度修改约束

- scheduler() 中必须持有 ptable.lock
- 必须正确调用 swtch()
- 上下文切换前必须: `holding(&ptable.lock) && interrupts disabled`

### 内存管理关键函数

- `walkpgdir(pde_t *pgdir, const void *va, int alloc)` — 页表遍历
- `allocuvm(pde_t *pgdir, uint oldsz, uint newsz)` — 分配用户页面
- `deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)` — 释放用户页面
- `mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)` — 建立映射

约束: 用户地址不能超过 KERNBASE；内核地址空间在所有进程间共享（setupkvm）；PTE_U 标志区分用户/内核页面。

## Lab 目录规范（唯一事实源）

| 内容类型 | 目录 | 说明 |
|---------|------|------|
| 实验设计文档 | `labs/<lab-name>/` | README、设计说明、实验指导、框架代码 |
| 实验测试代码 | `lab-Tests/<lab-name>/` | 测试用例、验证脚本、期望输出 |

**规则：**
- **`labs/`**: 实验设计内容，包括 `README.md`（实验说明）、框架代码、参考实现、知识点文档
- **`lab-Tests/`**: 具体测试代码，包括自动化测试用例、测试脚本、验证程序
- 新建 lab 时两个目录下均需创建对应的 `<lab-name>/` 子目录
- 不得将测试代码混入 `labs/`，也不得将设计文档混入 `lab-Tests/`

## Lab Makefile 规范

每个 `lab-Tests/<lab-name>/` 目录必须包含一个 `Makefile`，学生在该目录下直接 `make` 即可完成补丁应用、编译、运行。

标准目标: `all`(apply+编译) / `qemu-nox` / `qemu` / `qemu-gdb` / `apply`(幂等) / `unapply`(git restore) / `clean` / `start`(一键启动) / `exit`(一键退出)。

模板要点: `XVROOT := ../..`；apply 步骤全部幂等（grep 检查标记）；unapply 用 `git restore -- kernel/... include/... user/...` 并删除测试程序；根 Makefile 加用户程序需同时改 `UC` / `UPROG_NAMES` / `UPROG_GENERIC` 三个变量。

> 参考实现：[lab-Tests/lab-sched-01-priority/Makefile](../../lab-Tests/lab-sched-01-priority/Makefile)
