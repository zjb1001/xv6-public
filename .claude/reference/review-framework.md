# review-framework.md — 五维评审框架（单一事实源）

> **消费者**：xv6-reviewer（必读）、xv6-developer（按需，自检用）、xv6-debugger（按需）
> **维护规则**：唯一事实源。评审维度的检查项、状态机图只改本文件。
<!-- canonical: 进程状态机、锁序、五维检查清单、不变量锚点、评审报告模板 -->

## 进程状态机（合法转换）

```
UNUSED --allocproc()--> EMBRYO
EMBRYO --userinit()/fork()--> RUNNABLE
RUNNABLE --scheduler()--> RUNNING
RUNNING --yield()/sleep()/exit()--> RUNNABLE/SLEEPING/ZOMBIE
SLEEPING --wakeup()--> RUNNABLE
RUNNING --exit()--> ZOMBIE
ZOMBIE --wait() (parent)--> UNUSED
```

## 常见锁序（从先到后）

1. ptable.lock
2. sleep-locks (inode lock, file lock)
3. 缓冲区缓存锁 (bcache.lock)
4. kmem 锁 (kmem.lock)

## 五维检查清单

### 维度 1: 并发正确性 (权重最高)

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

### 维度 2: 资源管理

- [ ] 每个 kalloc() 在所有返回路径上是否有匹配的 kfree()？
- [ ] 每个 filedup() 是否有 fileclose()？
- [ ] 每个 idup() 是否有 iput()？
- [ ] 文件描述符 (ofile[]) 在 exit() 中是否正确清理？
- [ ] 引用计数是否在释放锁之前递增（防止使用中释放）？
- [ ] 新分配的资源在后续操作失败时是否回滚释放？

### 维度 3: 内存安全

- [ ] 用户空间的指针是否通过 argptr/fetchstr/fetchint 验证？
- [ ] 用户地址是否检查 < KERNBASE？
- [ ] 数组/缓冲区访问是否有边界检查？
- [ ] 大小参数是否防止整数溢出（如 newsz < KERNBASE）？
- [ ] 是否有使用后释放（dangling pointer）的风险？
- [ ] 内核栈溢出风险（只有 4KB）？

### 维度 4: 进程状态一致性

- [ ] 状态转换是否遵循上方状态机图？
- [ ] ptable.lock 是否在状态转换期间持有？
- [ ] scheduler() 中选中的进程是否已设为 RUNNING？
- [ ] exit() 是否正确清理资源并唤醒父进程？
- [ ] wait() 是否正确处理 ZOMBIE 子进程？

### 维度 5: 文件系统一致性

- [ ] FS 修改是否在 begin_op()/end_op() 之间？
- [ ] 是否使用 log_write() 而非直接 bwrite()？
- [ ] inode 锁 (ilock/iunlock) 是否包围 inode 字段访问？
- [ ] 目录操作是否正确处理 "." 和 ".."？
- [ ] 块分配 (balloc) 和释放 (bfree) 是否在日志内？

## 被违反的不变量（根因分析锚点）

| 不变量 | 表述 | 违反后果 |
|--------|------|---------|
| 锁不变量 | "共享状态修改前必须持锁" | 竞态、数据损坏 |
| 资源不变量 | "分配的资源必须释放" | 泄漏、系统逐渐耗尽 |
| 状态不变量 | "进程状态转换必须合法" | 状态机错乱、调度异常 |
| 中断不变量 | "上下文切换期间中断必须禁用" | 死锁、丢失唤醒 |
| 一致性不变量 | "FS 修改必须在事务中" | 崩溃后磁盘不一致 |

## 评审报告模板

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
```
