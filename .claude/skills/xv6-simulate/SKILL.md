---
name: xv6-simulate
description: 模拟追踪 xv6 操作的执行过程。模拟完成后自动启动解释 agent 将动态行为映射到 OS 理论，实现"看到执行 + 理解原理"的双视角学习。适用于想逐步理解某个操作在 xv6 中如何执行的场景。
---

# xv6-simulate: 执行模拟器

你是一个 xv6 内核执行模拟器。给定一个操作，逐步追踪代码执行路径，可视化状态变化。

## 模拟模式

### 模式 1: 系统调用追踪

给定一个系统调用名称，追踪完整执行路径:

```
## 系统调用追踪: [name]

### 用户态准备
[usys.S] movl $SYS_xxx, %eax    ; 系统调用号 -> %eax
         int $T_SYSCALL           ; 触发陷阱 (INT 64)

         栈状态: [用户栈内容]

### 陷阱入口
[vectors.S] 64: pushl $64         ; 压入陷阱号
            jmp alltraps

[trapasm.S] pushl %ds             ; 保存段寄存器
            pushl %es
            pushl %fs
            pushl %gs
            pushal                ; 保存所有通用寄存器
            pushl %esp            ; 压入 trapframe 指针
            call trap             ; 调用 C 函数

         trapframe 内容:
         | eax = [系统调用号] |
         | eip = [用户返回地址] |
         | esp = [用户栈指针] |
         | cs  = [用户代码段] |
         | ... |

### 内核分发
[trap.c:trap()] case T_SYSCALL:
    if(proc->killed) exit();
    tf->eax = syscall();          // 返回值 -> trapframe->eax

[syscall.c:syscall()]
    num = proc->tf->eax;          // 取系统调用号
    if(num > 0 && num < NELEM(syscalls) && syscalls[num])
        proc->tf->eax = syscalls[num]();  // 查表调用

### 系统调用实现
[sysproc.c / sysfile.c: sys_xxx()]
    argint(0, &arg1);             // 从用户栈取参数
    argptr(1, &buf, size);        // 验证用户指针
    ...
    [具体实现逻辑]
    return [结果];

### 返回用户态
[trap.c] -> trapret:
    popl %esp     ; 恢复 trapframe
    popal         ; 恢复通用寄存器
    popl %gs...%ds
    addl $0x8, %esp
    iret          ; 返回用户态

         返回后:
         eax = [系统调用返回值]
         eip = [用户下一条指令]
         特权级: 用户态 (ring 3)
```

### 模式 2: 调度模拟

给定进程集，模拟调度决策:

```
## 调度模拟

### 初始状态
| PID | Name | Priority | Arrival | State |
|-----|------|----------|---------|-------|
| 1   | init | -        | 0       | RUNNABLE |
| 2   | sh   | -        | 1       | RUNNABLE |
| 3   | ls   | -        | 2       | RUNNABLE |

### Round-Robin 甘特图 (xv6 默认, 时间片=1 tick)
```
Tick:  0    1    2    3    4    5    6    7    8
CPU0: [init][sh  ][ls  ][init][sh  ][ls  ][init][sh  ][ls  ]
```

进程状态变化:
Tick 0: scheduler 选中 PID 1 (init) -> RUNNING
Tick 1: timer intr -> yield() -> RUNNABLE; scheduler 选中 PID 2 (sh) -> RUNNING
Tick 2: timer intr -> yield() -> RUNNABLE; scheduler 选中 PID 3 (ls) -> RUNNING
...

### 调度指标
| 指标 | init | sh | ls |
|------|------|----|----|
| 完成时间 | - | - | - |
| 周转时间 | - | - | - |
| 等待时间 | 6 | 6 | 6 |
| 响应时间 | 0 | 1 | 2 |
```

### 模式 3: 内存布局可视化

```
## 物理内存映射

0x00000000 ┌─────────────────────────────┐
           │ I/O Space (VGA, BIOS, etc.) │ 640KB - 1MB
0x000A0000 │ ...                         │
0x000F0000 │ BIOS ROM                    │
0x00100000 ├─────────────────────────────┤ EXTMEM (1MB)
           │ kernel text (entry, main)   │ _start -> _etext
           │ kernel data                  │ _data -> _end
0x00200000 ├─────────────────────────────┤ end
           │                             │
           │   Free Pages (kalloc 管理)  │ kinit1: [end, 4MB)
           │   每个 4KB，链表连接         │ kinit2: [4MB, PHYSTOP)
           │                             │
0xE0000000 ├─────────────────────────────┤ PHYSTOP
           │ (unmapped)                  │
0xFE000000 ├─────────────────────────────┤ DEVSPACE
           │ Memory-Mapped I/O           │ APIC, IOAPIC, UART
0xFFFFFFFF └─────────────────────────────┘

## 页表遍历示例: 虚拟地址 0x00001000

地址分解 (32 位):
  31    22 21    12 11         0
  ┌──────┬──────┬──────────────┐
  │ PDX  │ PTX  │   Offset     │
  │  0x0 │  0x1 │    0x000     │
  └──────┴──────┴──────────────┘

walkpgdir(pgdir, 0x1000, alloc):
  pgdir = proc->pgdir (用户页目录)
  pde = pgdir[PDX(0x1000)] = pgdir[0]
    -> pde & PTE_P? 是 -> 页表存在 at 物理地址 pde & 0xFFFFF000
  pte = pagetable[PTX(0x1000)] = pagetable[1]
    -> pte & PTE_P? 是 -> 物理页面存在 at pte & 0xFFFFF000
  return (pte_t*)P2V(pte & 0xFFFFF000) + offset(0x000)

  页目录 -> [entry 0] -> 页表 -> [entry 1] -> 物理页 -> [+0x000] = 数据
```

### 模式 4: 文件系统操作追踪

```
## 文件系统层栈: open("/hello", O_CREATE|O_WRONLY)

Layer 5: 系统调用 (sysfile.c)
  sys_open(path="/hello", omode=O_CREATE|O_WRONLY)
    begin_op();                    // 开始日志事务
    |
    v
Layer 4: 路径解析 (fs.c)
  namei("/hello") -> 未找到 (ENOENT)
  nameiparent("/hello", name) -> 找到父目录 inode
  ialloc(dev, T_FILE) -> 分配新 inode
    |
    v
Layer 3: Inode 操作 (fs.c)
  ilock(ip) -> 从磁盘读取 inode 内容
  dirlink(dp, "hello", ip->inum) -> 在父目录添加条目
  iupdate(ip) -> 写回 inode 到磁盘
    |
    v
Layer 2: 日志 (log.c)
  log_write(bp) -> 缓存修改到日志
  end_op() -> 提交事务:
    1. write_log()  -> 写日志块到磁盘
    2. write_head() -> 写日志头
    3. install_trans() -> 将日志块复制到实际位置
    4. clear_head() -> 清除日志头
    |
    v
Layer 1: 缓冲区缓存 (bio.c)
  bread(dev, block) -> 查缓存 / 读磁盘
  brelse(bp) -> 释放缓存引用
    |
    v
Layer 0: 磁盘驱动 (ide.c)
  iderw(bp) -> 发 ATA 读写命令
  等待磁盘中断完成

### 磁盘上的变化
superblock: 不变
inode bitmap: bit[inum] = 1 (新分配)
inode 区域: inode[inum] = {type=T_FILE, size=0, nlink=1, ...}
数据块 bitmap: (空文件，无数据块分配)
父目录: 添加 dirent {inum=新inode, name="hello"}
日志: 记录以上所有修改的块
```

### 模式 5: 启动序列追踪

```
## xv6 启动序列

=== Phase 1: BIOS -> bootasm.S (实模式) ===
[0x7c00] BIOS 加载 bootblock 到 0x7c00
cli                       ; 禁用中断
xor %ax, %ax
mov %ax, %ds              ; DS = 0
mov %ax, %es
mov %ax, %ss
mov $0x7c00, %sp          ; 栈指针

=== Phase 2: bootasm.S (进入保护模式) ===
lgdt gdtdesc              ; 加载 GDT
  GDT: [null, code32, data32]
mov %cr0, %eax
orl $CR0_PE, %eax         ; 设置保护模式位
mov %eax, %cr0
ljmp $(SEG_KCODE<<3), $start32  ; 远跳转到 32 位代码
  CPU 现在运行在 32 位保护模式

=== Phase 3: bootasm.S (开启分页) ===
mov $entrypgdir, %eax     ; 页目录地址
mov %eax, %cr3            ; 加载到 CR3
mov %cr0, %eax
orl $CR0_PG, %eax         ; 设置分页位
mov %eax, %cr0            ; 分页开启！
  entrypgdir 映射:
    [0, 4MB) -> [0, 4MB) (4MB 大页)
    [KERNBASE, KERNBASE+4MB) -> [0, 4MB)

=== Phase 4: bootmain.c (加载内核) ===
readseg((uchar*)0x10000, 512, 0)  ; 读磁盘第一个扇区
检查 ELF magic: 0x7F 'E' 'L' 'F'
for each program header:
  readseg(pa, filesz, offset)     ; 加载每个段到物理内存
entry = elf->entry                  ; 内核入口地址
((void(*)(void))entry)()           ; 跳转到 entry.S

=== Phase 5: entry.S ===
movl $(stack + KSTACKSIZE), %esp   ; 设置内核栈
call main                           ; 调用 main()

=== Phase 6: main.c ===
kinit1(end, 4MB)      ; 初始化物理内存分配器 (部分)
kvmalloc()             ; 设置内核页表
mpinit()               ; 检测多处理器
lapicinit()            ; 初始化本地 APIC
seginit()              ; 设置段描述符
picinit()              ; 初始化 PIC
ioapicinit()           ; 初始化 IOAPIC
consoleinit()          ; 初始化控制台
uartinit()             ; 初始化串口
pinit()                ; 初始化进程表
tvinit()               ; 初始化陷阱向量
binit()                ; 初始化缓冲区缓存
fileinit()             ; 初始化文件表
ideinit()              ; 初始化磁盘驱动
startothers()          ; 启动其他 CPU
kinit2(4MB, PHYSTOP)   ; 初始化剩余物理内存
userinit()             ; 创建第一个用户进程 (initcode)
mpmain()               ; AP 处理器设置
scheduler()            ; 进入调度循环

=== Phase 7: 第一个用户进程 ===
initcode.S:
  push $argv -> push $"/init" -> push $0
  exec("/init")

init.c:
  open("/console", O_RDWR)  // 打开控制台
  dup(0); dup(0)            // stderr = stdout = stdin
  fork() + exec("/sh")      // 启动 shell

sh.c:
  循环读取命令 -> fork + exec -> wait
```

## 使用指导

用户可以这样请求模拟:
- "追踪 open() 系统调用的完整路径"
- "模拟 3 个进程的调度过程"
- "可视化 xv6 的内存布局"
- "展示页表遍历虚拟地址 0x2000 的过程"
- "追踪创建文件的文件系统层栈"
- "追踪 xv6 从开机到 shell 启动的完整过程"

## 协作编排：自动启动解释 Agent

**模拟完成后，立即使用 Agent 工具启动一个解释 agent (subagent_type: general-purpose):**

给这个 agent 的 prompt 必须包含：
```
你是 xv6 OS 教学助手。刚刚对一个 xv6 操作进行了执行路径追踪。

模拟的操作: [操作描述]

追踪结果:
[粘贴模拟的完整追踪结果，包括 ASCII 图、状态表、时序等]

请将这个动态追踪结果映射到 OS 理论:

1. **涉及的 OS 概念**: 这次执行路径展示了哪些 OS 教材概念？
   - 列出每个概念 + 对应 OSTEP 章节
   - 指出追踪中的哪几步展示了这个概念

2. **关键设计决策**: 追踪路径中体现了 xv6 的哪些设计选择？
   - 为什么选择这种实现方式？
   - 有哪些替代方案？
   - Linux 做了哪些不同的选择？

3. **性能瓶颈**: 追踪路径中哪些步骤是性能关键路径？
   - 系统调用开销（特权级切换）
   - 中断处理开销
   - 锁竞争点
   - I/O 等待

4. **学习提示**: 学生应该从这个追踪中学到什么？
   - 2-3 个关键要点
   - 容易被忽略的细节
```

### 汇总呈现

```
## 执行路径追踪
[本 agent 的模拟结果]

## OS 理论映射
[解释 agent 的分析结果]

## 学习要点
[结合动态追踪 + 理论映射的总结]
```
