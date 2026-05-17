# xv6-public — Unix V6 教学操作系统 (x86)

## 项目概述

xv6 是 MIT 6.828/6.S081 课程的教学操作系统，在 x86 多处理器上重新实现了 Unix Version 6。
这是 **x86 版本**（非 RISC-V），使用 ANSI C 编写。

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

## 架构概览

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

## 内存布局

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

## 执行流程

### 启动序列
BIOS -> boot/bootasm.S (实模式->保护模式, 开分页) -> boot/bootmain.c (从磁盘加载 ELF 内核) -> kernel/entry.S (设栈, 跳 main) -> kernel/main.c -> kinit1 -> kvmalloc -> mpinit -> lapicinit -> seginit -> ... -> userinit -> scheduler()

### 第一个用户进程
kernel/initcode.S (用户模式, exec("/init")) -> user/init.c (打开 console, fork+exec sh) -> user/sh.c (shell)

### 系统调用路径
用户代码 (user/usys.S: movl $SYS_xxx, %eax; int $T_SYSCALL) -> build/vectors.S -> kernel/trapasm.S (构建 trapframe) -> kernel/trap.c -> kernel/syscall.c (查 syscalls[] 表) -> sys_*

## 编码规范

- **C89/C90 风格**: 函数定义的类型名在单独一行
- **类型定义**: uint=unsigned int, uchar=unsigned char, pde_t=uint, pte_t=uint
- **锁规则**: 修改共享状态前必须获取锁; ptable.lock 保护进程状态
- **错误处理**: 函数返回 0/-1; 不可恢复错误用 panic()
- **PAGEBREAK 注释**: 为 xv6 教材排版保留

## 构建和运行

```bash
make              # 编译内核和用户程序
make qemu         # QEMU 运行（带窗口）
make qemu-nox     # QEMU 运行（纯终端）
make qemu-gdb     # QEMU + GDB 调试
make clean        # 清理
```

工具链: 32 位 x86 ELF GCC (可能需要 i386-jos-elf- 前缀)

构建产物: `build/xv6kernel` (内核二进制), `xv6.img` (引导镜像), `fs.img` (文件系统镜像), `_cat`, `_sh` 等 (用户程序)

## Lab 目录规范

进行 lab 实验编写时，必须遵循以下目录约束：

| 内容类型 | 目录 | 说明 |
|---------|------|------|
| 实验设计文档 | `labs/<lab-name>/` | README、设计说明、实验指导、框架代码 |
| 实验测试代码 | `lab-Tests/<lab-name>/` | 测试用例、验证脚本、期望输出 |

**规则：**
- **`labs/`**: 存放实验的设计内容，包括 `README.md`（实验说明）、实验框架代码、参考实现、知识点文档
- **`lab-Tests/`**: 存放具体的测试代码，包括自动化测试用例、测试脚本、验证程序
- 新建 lab 时两个目录下均需创建对应的 `<lab-name>/` 子目录
- 不得将测试代码混入 `labs/`，也不得将设计文档混入 `lab-Tests/`

## Lab 编译规范

**核心原则**：每个 `lab-Tests/<lab-name>/` 目录必须包含一个 `Makefile`，学生在该目录下直接 `make` 即可完成补丁应用、编译、运行，无需手动执行任何 `cp` / `cat >>` / `sed` 命令。

### 标准 Makefile 目标

| 目标 | 说明 |
|------|------|
| `make` / `make all` | 应用补丁 + 编译 xv6 |
| `make qemu-nox` | 应用补丁 + 编译 + 运行（无图形，终端直连） |
| `make qemu` | 应用补丁 + 编译 + 运行（带 QEMU 窗口） |
| `make qemu-gdb` | 应用补丁 + 编译 + 以 GDB 模式启动 |
| `make apply` | 仅应用补丁（幂等，可重复执行） |
| `make unapply` | 通过 `git restore` 撤销所有补丁并删除测试程序 |
| `make clean` | 清理编译产物（不撤销补丁） |

### Makefile 模板结构

```makefile
XVROOT := ../..

.PHONY: all qemu qemu-nox qemu-gdb apply unapply clean

all: apply
	$(MAKE) -C $(XVROOT)

qemu: apply
	$(MAKE) -C $(XVROOT) qemu

qemu-nox: apply
	$(MAKE) -C $(XVROOT) qemu-nox

qemu-gdb: apply
	$(MAKE) -C $(XVROOT) qemu-nox-gdb

apply:
	@echo "=== [<lab-name>] Applying patches ==="
	# 每步用 grep 做幂等检查，已打过则跳过
	# 替换内核文件: cp <file> $(XVROOT)/kernel/<file>
	# 替换共享头文件: cp <file> $(XVROOT)/include/<file>
	# 替换用户文件: cp <file> $(XVROOT)/user/<file>
	# 替换引导文件: cp <file> $(XVROOT)/boot/<file>
	# 追加内容:     if ! grep -q "marker" $(XVROOT)/kernel/file; then cat additions >> $(XVROOT)/kernel/file; fi
	# sed 插入行:   if ! grep -q "marker" $(XVROOT)/kernel/file; then sed -i '/anchor/a new_line' $(XVROOT)/kernel/file; fi
	# 修改 Makefile: sed -i 's/old_token$$/old_token newprog/' $(XVROOT)/Makefile
	@echo "=== Apply done ==="

unapply:
	cd $(XVROOT) && git restore -- kernel/<被修改的文件> include/<共享头文件> user/<用户文件>
	rm -f $(XVROOT)/user/<testprog>.c

clean:
	$(MAKE) -C $(XVROOT) clean
```

### 文件路径映射

lab 补丁操作的文件路径对应关系：

| 文件类型 | 路径模式 | 示例 |
|---------|---------|------|
| 内核源码/头文件 | `$(XVROOT)/kernel/<file>` | `kernel/proc.c`, `kernel/defs.h` |
| 共享头文件 | `$(XVROOT)/include/<file>` | `include/syscall.h`, `include/traps.h` |
| 用户程序/库 | `$(XVROOT)/user/<file>` | `user/user.h`, `user/usys.S` |
| 引导代码 | `$(XVROOT)/boot/<file>` | `boot/bootmain.c`, `boot/bootasm.S` |
| 根 Makefile | `$(XVROOT)/Makefile` | 根目录下的 Makefile |

### 幂等打补丁规则

所有 `apply` 步骤必须幂等，遵循以下模式：

```bash
# 追加内容前检查标记
if ! grep -q "unique_marker" $(XVROOT)/kernel/target_file; then
    cat additions.c >> $(XVROOT)/kernel/target_file
fi

# sed 插入前检查
if ! grep -q "unique_marker" $(XVROOT)/kernel/target_file; then
    sed -i '/anchor_line/a inserted_line' $(XVROOT)/kernel/target_file
fi
```

### 根 Makefile 中添加用户程序

需要同时修改三个变量（`$` 在 Makefile 中需写为 `$$`）：

```makefile
# UC: 控制 .c → .o 编译
sed -i 's/libtest uidtest fifotest$$/libtest uidtest fifotest <newprog>/' $(XVROOT)/Makefile

# UPROG_NAMES: 控制哪些程序进入 fs.img
sed -i 's/usertests wc zombie uidtest fifotest$$/usertests wc zombie uidtest fifotest <newprog>/' $(XVROOT)/Makefile

# UPROG_GENERIC: 控制链接规则（使用标准 ULIB）
sed -i 's/stressfs wc zombie uidtest fifotest$$/stressfs wc zombie uidtest fifotest <newprog>/' $(XVROOT)/Makefile
```

> 参考实现：[lab-Tests/lab-sched-01-priority/Makefile](lab-Tests/lab-sched-01-priority/Makefile)

## 可用 Skills

| Skill | 命令 | 用途 |
|-------|------|------|
| xv6-dev | `/xv6-dev` | 开发新功能，每步带 OS 概念解释 |
| xv6-review | `/xv6-review` | 从 OS 设计角度评审代码变更 |
| xv6-explain | `/xv6-explain` | 解释 xv6 代码，映射到 OS 教材概念 |
| xv6-debug | `/xv6-debug` | 诊断 xv6 崩溃、死锁、异常行为 |
| xv6-simulate | `/xv6-simulate` | 模拟追踪系统调用、调度、内存操作 |

## OS 核心术语对照

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
