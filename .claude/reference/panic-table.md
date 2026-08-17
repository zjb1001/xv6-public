# panic-table.md — panic 定位表 + 调试命令手册（单一事实源）

> **消费者**：xv6-debugger（必读）、xv6-reviewer（按需）、xv6-developer（按需）
> **维护规则**：唯一事实源。新增 panic 场景、GDB/QEMU 技巧只改本文件。
<!-- canonical: panic定位表、启动断点、症状速查、GDB/QEMU命令、竞态检测协议 -->

## Panic 消息定位表

xv6 中所有 panic 的位置和含义：

| Panic 消息 | 源文件位置 | OS 原因 |
|-----------|-----------|---------|
| "sched ptable.lock" | proc.c | sched() 要求持有 ptable.lock |
| "sched locks" | proc.c | sched() 期间持有超过一把锁 |
| "sched interruptible" | proc.c | sched() 要求中断禁用 (FL_IF clear) |
| "acquire" | spinlock.c | 重复获取已持有的锁（同 CPU）|
| "remap" | vm.c | 映射已存在的虚拟地址 |
| "kfree" | kalloc.c | 释放无效物理页 |
| "use before init" | vm.c | 使用未初始化的内存 |
| "iget" | fs.c | inode 表溢出 |
| "balloc" | fs.c | 磁盘块耗尽 |
| "filealloc" | file.c | 文件表溢出 |

通用诊断:
1. 用 GDB: `b panic` 捕获所有 panic
2. panic 时检查栈回溯: `bt` 或 `x/20x $esp`
3. 检查寄存器状态: `info registers`
4. 定位触发 panic 的具体条件

## 启动链关键断点

```
0x7c00         — bootasm.S 入口（BIOS 加载 bootblock）
0x10000c       — bootmain.c: bootmain()
0x100000+      — entry.S 入口
main.c:main()  — 各初始化函数
```

## 症状 → 诊断速查

### 启动失败

| 症状 | 可能原因 | 诊断步骤 |
|------|---------|---------|
| 完全无输出 | QEMU 配置错误或串口问题 | 检查 make qemu-nox，检查 QEMU 版本 |
| "Booting from hard disk..." 后挂起 | bootmain.c 未找到 ELF magic | 检查 kernel 是否编译成功，检查 Makefile |
| Triple fault (重启循环) | bootasm.S 段设置错误或 entrypgdir 问题 | GDB: `b *0x7c00` 单步跟踪 bootasm.S |
| main.c 中 panic | 某个初始化函数失败 | 根据 panic 字符串定位具体函数 |

### 内存损坏

| 症状 | 检查 |
|------|------|
| 数据变成 0x01010101 | kfree 填充模式，说明 use-after-free |
| T_PGFLT (trap 14) | 用 `rcr2()` 获取故障地址，检查页表 |
| 随机崩溃 | 栈溢出（只有 4KB 内核栈）、越界写 |
| kalloc 返回 NULL | 物理内存耗尽 |

## GDB 连接

```bash
# 终端 1: 启动 QEMU with GDB stub
make qemu-gdb

# 终端 2: 启动 GDB
gdb kernel
# 或使用自定义 .gdbinit:
# target remote localhost:25000
# add-symbol-file kernel 0x100000
```

## 关键 GDB 命令

```
# 断点
b *0x7c00          # boot 入口
b main             # main 函数
b trap             # 每次陷阱/中断
b panic            # 捕获 panic
b scheduler        # 观察调度

# 检查
info registers     # 寄存器
x/10x $esp         # 栈内容
bt                 # 栈回溯
p/x cr2            # 页错误地址 (需要 QEMU monitor)

# 进程状态
p ptable.proc[i].state     # 查看进程状态
p ptable.proc[i].name      # 进程名
p ptable.proc[i].pid       # PID
p ptable.proc[i].parent    # 父进程

# 锁状态
p ptable.lock.locked       # ptable 锁
p ptable.lock.cpu          # 锁持有者
```

## QEMU Monitor 命令

```
Ctrl-A c          # 进入 QEMU monitor
info mem          # 页表映射
info tlb          # TLB 内容
info registers    # CPU 寄存器
xp/Nx ADDR        # 物理内存查看
x/Nx ADDR         # 虚拟内存查看
```

## 死锁/挂起诊断决策树

```
系统挂起
├── 完全无输出？
│   ├── 是: 可能启动失败 -> 检查 boot 链
│   └── 否: 可能死锁
│       ├── 中断是否禁用？ -> 检查 pushcli/popcli 配对
│       ├── 是否有循环等待？ -> 检查锁获取顺序
│       └── 是否丢失唤醒？ -> 检查 sleep/wakeup 时序
└── 部分进程卡住？
    ├── 其他进程是否正常？ -> 可能是 sleep 条件不满足
    ├── 所有进程卡住？ -> 全局锁或中断问题
    └── 只在 CPUS>1 时出现？ -> SMP 竞态
```

调试技巧:
- `make CPUS=1 qemu-nox` — 单 CPU 模式排除 SMP 问题
- `b scheduler` — 观察调度是否继续
- `b sleep` — 观察什么进程在睡眠
- 检查 ptable: `p ptable.proc[0].state` ... `p ptable.proc[63].state`

## 竞态检测协议

1. **识别共享状态**: 哪些数据被多个 CPU/中断/进程访问？
2. **找到保护锁**: 代码中是否显式标注了保护该状态的锁？
3. **检查所有访问路径**: 是否每个路径都获取了锁？
4. **检查中断安全**: 持锁期间是否禁用了中断？
5. **检查 TOCTOU**: 检查条件和使用条件之间是否有窗口？

特征:
- 只在 CPUS>1 时出现 -> SMP 竞态
- 出现频率随 CPU 数增加 -> 锁粒度问题
- 时序敏感（加入 print 就消失）-> 几乎确定是竞态
