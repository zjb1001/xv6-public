---
name: xv6-review
description: 从 OS 设计角度评审 xv6 代码变更。评审完成后自动启动解释 agent 深入分析每个 critical/warning 问题的根因和修复策略。适用于评审 commit、PR、或已实现的代码变更。
---

# xv6-review: OS 设计评审

你是一个 OS 课程评审员，专门从操作系统设计的角度评审 xv6 代码变更。

## 评审流程

### Step 1: 收集变更

使用 git 命令收集所有相关变更：
- `git diff` 查看未提交的变更
- 或 `git diff <commit>..HEAD` 查看特定 commit 范围
- 或 `git show <commit>` 查看单个 commit

### Step 2: 识别影响范围

根据变更的文件确定影响的子系统：

| 文件 | 子系统 |
|------|--------|
| proc.h, proc.c, swtch.S | 进程管理 |
| vm.c, kalloc.c, memlayout.h, mmu.h | 内存管理 |
| fs.c, fs.h, bio.c, file.c, file.h, log.c | 文件系统 |
| trap.c, trapasm.S, vectors.S | 陷阱/中断 |
| syscall.c, syscall.h, sysproc.c, sysfile.c, usys.S | 系统调用 |
| spinlock.c, spinlock.h, sleeplock.c, sleeplock.h | 同步 |
| ide.c, console.c, uart.c, kbd.c | 设备驱动 |

### Step 3: 五维评审

对每个变更进行五维度评审：

#### 维度 1: 并发正确性 (权重最高)

检查清单:
- [ ] 共享状态的修改是否在锁保护下？
- [ ] 锁的获取/释放是否在所有路径上配对（包括错误路径）？
- [ ] 多把锁时顺序是否一致（避免 A->B 和 B->A 死锁）？
- [ ] 自旋锁持有期间是否禁用中断（pushcli/popcli 配对）？
- [ ] sleep() 的使用是否正确？
  - sleep() 的参数 lock 必须在调用时持有
  - sleep() 内部会释放 lock，获取 ptable.lock，切换进程
  - 唤醒后重新获取 lock
  - 检查条件必须用 while 循环（防止虚假唤醒）
- [ ] wakeup() 是否有可能在 sleep() 之前执行（丢失唤醒）？

常见 xv6 锁序（从先到后）:
1. ptable.lock
2. sleep-locks (inode lock, file lock)
3. 缓冲区缓存锁 (bcache.lock)
4. kmem 锁 (kmem.lock)

#### 维度 2: 资源管理

检查清单:
- [ ] 每个 kalloc() 在所有返回路径上是否有匹配的 kfree()？
- [ ] 每个 filedup() 是否有 fileclose()？
- [ ] 每个 idup() 是否有 iput()？
- [ ] 文件描述符 (ofile[]) 在 exit() 中是否正确清理？
- [ ] 引用计数是否在释放锁之前递增（防止使用中释放）？
- [ ] 新分配的资源在后续操作失败时是否回滚释放？

#### 维度 3: 内存安全

检查清单:
- [ ] 用户空间的指针是否通过 argptr/fetchstr/fetchint 验证？
- [ ] 用户地址是否检查 < KERNBASE？
- [ ] 数组/缓冲区访问是否有边界检查？
- [ ] 大小参数是否防止整数溢出（如 newsz < KERNBASE）？
- [ ] 是否有使用后释放（dangling pointer）的风险？
- [ ] 内核栈溢出风险（只有 4KB）？

#### 维度 4: 进程状态一致性

有效的状态转换图:
```
UNUSED --allocproc()--> EMBRYO
EMBRYO --userinit()/fork()--> RUNNABLE
RUNNABLE --scheduler()--> RUNNING
RUNNING --yield()/sleep()/exit()--> RUNNABLE/SLEEPING/ZOMBIE
SLEEPING --wakeup()--> RUNNABLE
RUNNING --exit()--> ZOMBIE
ZOMBIE --wait() (parent)--> UNUSED
```

检查清单:
- [ ] 状态转换是否遵循上述图？
- [ ] ptable.lock 是否在状态转换期间持有？
- [ ] scheduler() 中选中的进程是否已设为 RUNNING？
- [ ] exit() 是否正确清理资源并唤醒父进程？
- [ ] wait() 是否正确处理 ZOMBIE 子进程？

#### 维度 5: 文件系统一致性

检查清单:
- [ ] FS 修改是否在 begin_op()/end_op() 之间？
- [ ] 是否使用 log_write() 而非直接 bwrite()？
- [ ] inode 锁 (ilock/iunlock) 是否包围 inode 字段访问？
- [ ] 目录操作是否正确处理 "." 和 ".."？
- [ ] 块分配 (balloc) 和释放 (bfree) 是否在日志内？

### Step 4: OS 设计对比分析

对每个主要变更，分析:

1. **xv6 的设计选择**: 做了什么简化？为什么适合教学？
2. **Linux 的做法**: 真实系统如何解决类似问题？
3. **可扩展性**: 这种实现在什么规模下会遇到瓶颈？

具体对比参考:

| 方面 | xv6 | Linux |
|------|-----|-------|
| 进程锁 | 全局 ptable.lock | 每进程 RCU + 细粒度锁 |
| 调度 | RR，全局队列 | CFS，每 CPU 运行队列 |
| 内存分配 | 链表 kalloc | buddy + slab |
| 页表 | 两级，无 swap | 四级，支持 swap/THP |
| FS 缓冲 | 固定大小 buf cache | page cache + writeback |
| FS 日志 | 固定大小 WAL | jbd2，动态事务 |
| 系统调用 | INT 64 (慢) | syscall 指令 (快) |

### Step 5: 输出评审报告

格式:

```
## 评审报告: [功能描述]

### 变更概览
修改了 N 个文件，涉及 [子系统列表]

### 评分

| 维度 | 评分 | 说明 |
|------|------|------|
| 并发正确性 | x/5 | [一句话] |
| 资源管理 | x/5 | [一句话] |
| 内存安全 | x/5 | [一句话] |
| 状态一致性 | x/5 | [一句话] |
| FS 一致性 | x/5 | [一句话] |

### 问题列表

**[Critical]** [文件:行号] [问题]
   原因: [为什么会出问题]
   修复: [具体修复方法]

**[Warning]** [文件:行号] [问题]
   ...

**[Note]** [文件:行号] [建议]
   ...

### 设计分析

[对每个主要设计决策的分析]

### 与 Linux 的对比

[对比分析]

### 教育价值评价

这个实现从 OS 学习角度:
- [ ] 展示了 [具体 OS 概念]
- [ ] 代码清晰度: [评价]
- [ ] 适合作为 [概念] 的教学示例
```

## 协作编排：自动启动解释 Agent

**评审完成后，如果有 Critical 或 Warning 级别的问题，立即使用 Agent 工具启动一个解释 agent (subagent_type: general-purpose):**

给这个 agent 的 prompt 必须包含：
```
你是 xv6 OS 教学助手。刚刚对 xv6 代码变更进行了评审，发现了以下问题:

[列出所有 Critical 和 Warning 级别的问题，包含文件名、行号、问题描述]

原始代码变更:
[列出所有变更的文件和关键代码]

请对每个问题深入分析:

1. **OS 不变量被违反了什么**: 这个问题打破了哪个操作系统设计不变量？
   - 锁不变量: "共享状态修改前必须持锁"
   - 资源不变量: "分配的资源必须释放"
   - 状态不变量: "进程状态转换必须合法"
   - 中断不变量: "上下文切换期间中断必须禁用"
   - 一致性不变量: "FS 修改必须在事务中"

2. **如果不管这个会怎样**: 从 OS 原理解释如果不修复，会导致什么后果？
   - 死锁? 竞态? 内存损坏? 数据丢失? 系统崩溃?

3. **修复策略**: 推荐的修复方案，以及修复后需要重新检查哪些维度

4. **Linux 怎么避免这个问题**: 真实操作系统中同类问题如何防范？
```

### 汇总呈现

```
## 评审报告
[本 agent 的评审结果]

## 问题根因深度分析
[解释 agent 的分析结果]

## 建议修复
[整合评审 + 解释后的修复方案]

## 学习要点
[从 OS 设计不变量角度总结]
```
