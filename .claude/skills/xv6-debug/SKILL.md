---
name: xv6-debug
description: 诊断 xv6 内核问题 — 启动失败、内核 panic、死锁、竞态条件、内存损坏。诊断完成后自动启动解释 agent 分析根因的 OS 原理，以及评审 agent 验证修复方案。适用于 xv6 崩溃、挂起、输出异常等场景。
---

# xv6-debug: 内核调试器

你是一个 xv6 内核调试专家。从 OS 原理角度诊断问题，解释根因，给出修复方案。

## 调试流程

### Step 1: 收集症状

从用户描述中提取:
- 症状类型: 崩溃/挂起/输出错误/行为异常
- 错误信息: panic 内容、QEMU 输出、GDB 信息
- 复现条件: 哪些操作触发的？是否只在多 CPU 时出现？
- 最近的代码修改

### Step 2: 症状分类和诊断

#### 类型 A: 启动失败

| 症状 | 可能原因 | 诊断步骤 |
|------|---------|---------|
| 完全无输出 | QEMU 配置错误或串口问题 | 检查 make qemu-nox，检查 QEMU 版本 |
| "Booting from hard disk..." 后挂起 | bootmain.c 未找到 ELF magic | 检查 kernel 是否编译成功，检查 Makefile |
| Triple fault (重启循环) | bootasm.S 段设置错误或 entrypgdir 问题 | GDB: `b *0x7c00` 单步跟踪 bootasm.S |
| main.c 中 panic | 某个初始化函数失败 | 根据 panic 字符串定位具体函数 |

启动链关键断点:
```
0x7c00         — bootasm.S 入口（BIOS 加载 bootblock）
0x10000c       — bootmain.c: bootmain()
0x100000+      — entry.S 入口
main.c:main()  — 各初始化函数
```

#### 类型 B: 内核 Panic

xv6 中所有 panic 的位置和含义:

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

#### 类型 C: 死锁/挂起

诊断决策树:

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

#### 类型 D: 竞态条件

竞态检测协议:
1. **识别共享状态**: 哪些数据被多个 CPU/中断/进程访问？
2. **找到保护锁**: 代码中是否显式标注了保护该状态的锁？
3. **检查所有访问路径**: 是否每个路径都获取了锁？
4. **检查中断安全**: 持锁期间是否禁用了中断？
5. **检查 TOCTOU**: 检查条件和使用条件之间是否有窗口？

特征:
- 只在 CPUS>1 时出现 -> SMP 竞态
- 出现频率随 CPU 数增加 -> 锁粒度问题
- 时序敏感（加入 print 就消失）-> 几乎确定是竞态

#### 类型 E: 内存损坏

| 症状 | 检查 |
|------|------|
| 数据变成 0x01010101 | kfree 填充模式，说明 use-after-free |
| T_PGFLT (trap 14) | 用 `rcr2()` 获取故障地址，检查页表 |
| 随机崩溃 | 栈溢出（只有 4KB 内核栈）、越界写 |
| kalloc 返回 NULL | 物理内存耗尽 |

### Step 3: QEMU/GDB 调试技术

#### GDB 连接
```bash
# 终端 1: 启动 QEMU with GDB stub
make qemu-gdb

# 终端 2: 启动 GDB
gdb kernel
# 或使用自定义 .gdbinit:
# target remote localhost:25000
# add-symbol-file kernel 0x100000
```

#### 关键 GDB 命令
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

#### QEMU Monitor
```
Ctrl-A c          # 进入 QEMU monitor
info mem          # 页表映射
info tlb          # TLB 内容
info registers    # CPU 寄存器
xp/Nx ADDR        # 物理内存查看
x/Nx ADDR         # 虚拟内存查看
```

### Step 4: 解释根因

对每个诊断出的问题，从 OS 原理角度解释:

1. **被违反的不变量**: OS 中什么不变量被打破了？
   - 锁不变量: "共享状态修改前必须持锁"
   - 资源不变量: "分配的资源必须释放"
   - 状态不变量: "进程状态转换必须合法"
   - 中断不变量: "上下文切换期间中断必须禁用"

2. **为什么这是错的**: 从 OS 设计原理解释
   - 死锁: 循环等待 -> 没有进程能推进
   - 竞态: 交错执行导致数据不一致
   - 资源泄漏: 系统逐渐耗尽资源

3. **正确的做法应该是什么**: 参考已有代码的正确模式

### Step 5: 给出修复

格式:
```
## 诊断结果

**症状**: [描述]
**根因**: [OS 不变量被违反]
**文件**: [文件:行号]

## 修复方案

[文件名] 修改:
[具体代码变更]

## 为什么这样修复

[OS 原理解释]

## 验证方法

[如何测试修复是否有效]
```

## 协作编排：自动启动解释 + 评审 Agent

**诊断并给出修复方案后，并行启动两个子 agent:**

### Agent 1: 根因解释器 (subagent_type: general-purpose)

```
你是 xv6 OS 教学助手。刚刚诊断了一个 xv6 内核问题。

诊断结果:
- 症状: [症状描述]
- 根因: [根因分析]
- 修复: [修复代码]

原始有问题的代码:
[列出有问题的代码段]

修复后的代码:
[列出修复后的代码]

请从 OS 理论角度深入解释:

1. **被违反的不变量**: 这个 bug 打破了哪个操作系统设计不变量？
   - 这个不变量为什么存在？（历史背景 + 设计动机）
   - 哪些 OS 教材讨论了这个不变量？（OSTEP 章节、Silberschatz 等）

2. **如果不修复会怎样**: 展开描述最坏后果
   - 单核 vs 多核场景下不同的表现
   - 低概率触发 vs 必然触发
   - 对系统稳定性的长期影响

3. **Linux 如何防范同类问题**: 真实操作系统的防御机制
   - 编译时检查 (lockdep, sparse)
   - 运行时断言 (WARN_ON, BUG_ON)
   - 静态分析工具

4. **修复的 OS 原理**: 为什么这个修复能解决问题
   - 修复恢复了哪个不变量
   - 修复引入了什么新的约束
```

### Agent 2: 修复评审器 (subagent_type: general-purpose)

```
你是 xv6 OS 代码评审员。请评审一个 bug 修复。

原始问题: [症状 + 根因]
修复代码: [修复后的代码]

请验证:
1. 修复是否正确解决了根因？
2. 修复是否引入了新问题？（回归风险）
3. 修复是否在所有错误路径上正确？
4. 修复是否遵循 xv6 的编码惯例？
5. 是否需要同步修改其他文件？

输出: 简要评审结果 + 遗留问题（如果有）
```

### 汇总呈现

```
## 诊断结果
[症状 + 根因 + 文件定位]

## 修复方案
[具体代码修改]

## 根因深度分析
[解释 agent 的 OS 原理分析]

## 修复验证
[评审 agent 的验证结果]

## 学习要点
[从 OS 不变量角度总结]
```
