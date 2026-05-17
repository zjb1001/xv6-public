# Lab: Multi-Level Feedback Queue Scheduler (多级反馈队列调度)
[English](../../README.md)

难度: ★★★★☆

## 设计初衷

静态优先级调度（`lab-sched-01`）的问题在于它需要用户手动设置优先级——而用户往往不知道一个进程应该是 CPU 密集型还是 I/O 密集型。MLFQ（Multi-Level Feedback Queue）通过**观察进程行为自动调整优先级**：

- 新进程默认进入**最高优先级**队列（假设它是交互式的）
- 进程用完整个时间片而未阻塞 → 降级到下一个队列（行为像 CPU 密集型）
- 进程在时间片结束前主动阻塞（I/O 等待）→ 保持当前优先级或提升（行为像 I/O 密集型）
- **周期性优先级提升（Boost）**：防止饥饿，将所有进程提升到最高优先级

MLFQ 是 Ousterhout 十条 OS 规则中的经典算法，也是现代 OS 调度的基础思路。

核心问题：*"调度器怎么在不知道进程未来行为的情况下，做出接近最优的决策？"*

## 前置知识

- **MLFQ 的 5 条规则**（来自 OSTEP 第 8 章）:
  1. Priority(A) > Priority(B) → A 运行
  2. Priority(A) = Priority(B) → A、B 轮转
  3. 新进程进入最高优先级
  4. 进程用完时间片 → 降级
  5. 周期性提升：所有进程回到最高优先级（防饥饿）
- **时钟中断**: `src/trap.c` 中 `T_IRQ0 + IRQ_TIMER`，每次中断递增 `ticks`，当前为每个时钟调用 `yield()`（Round-Robin）

```
MLFQ 三级队列示意:
Q0 (高优先级, 时间片=1个tick): [新进程] [I/O 进程]
Q1 (中优先级, 时间片=4个tick): [半 CPU-bound]
Q2 (低优先级, 时间片=∞或16个tick): [纯 CPU-bound 进程]

Q0 有进程 → 始终调度 Q0 中进程
Q0 空 → 调度 Q1
Q1 空 → 调度 Q2
```

## 实验内容

### 1. 定义 MLFQ 参数和数据结构 (include/proc.h, src/proc.c)

```c
#define NMLFQ      3             // 队列数量
#define BOOST_INTERVAL  100      // 每 100 个 ticks 执行一次优先级提升

// 每个队列的时间片（单位：ticks）
static int mlfq_quantum[NMLFQ] = {1, 4, 16};
```

在 `struct proc` 中添加：

```c
int  mlfq_level;      // 当前所在队列（0=最高, NMLFQ-1=最低）
int  ticks_in_level;  // 在当前队列已消耗的时间片数
```

维护 NMLFQ 个运行队列（双向链表或按 `mlfq_level` 扫描进程表）。

### 2. 修改进程创建（新进程进入最高优先级）

- `allocproc()` 中：`p->mlfq_level = 0; p->ticks_in_level = 0`
- `wakeup1()` 中：从 SLEEPING 变为 RUNNABLE 时，可选择提升回 Q0（I/O 奖励）或保持当前 level

### 3. 改造调度器：按优先级队列选择 (src/proc.c)

```c
void scheduler(void) {
    struct proc *p, *chosen;
    for(;;) {
        sti();
        acquire(&ptable.lock);
        chosen = 0;
        // 从 Q0 到 Q(NMLFQ-1)，找第一个有 RUNNABLE 进程的队列
        for(int level = 0; level < NMLFQ && !chosen; level++) {
            for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
                if(p->state == RUNNABLE && p->mlfq_level == level) {
                    chosen = p;
                    break;   // 同级 Round-Robin: 可改为记录上次调度位置
                }
            }
        }
        if(chosen) {
            // 运行 chosen
        }
        release(&ptable.lock);
    }
}
```

### 4. 在时钟中断中实现降级逻辑 (src/trap.c)

时钟中断是 MLFQ 的"计时器"。在 `trap()` 的时钟中断处理中：

```c
// 在 yield() 之前检查 MLFQ 降级
struct proc *p = myproc();
if(p && p->state == RUNNING) {
    p->ticks_in_level++;
    if(p->ticks_in_level >= mlfq_quantum[p->mlfq_level]) {
        // 用完时间片，降级
        if(p->mlfq_level < NMLFQ - 1) {
            p->mlfq_level++;
        }
        p->ticks_in_level = 0;
    }
    yield();   // 让出 CPU（触发调度器重新选择）
}

// 定期提升（Boost）
if(ticks % BOOST_INTERVAL == 0) {
    boost_all();   // 将所有进程移回 Q0
}
```

### 5. 实现优先级提升（Boost）(src/proc.c)

```c
static void boost_all(void) {
    // 必须在 ptable.lock 保护下调用
    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == RUNNABLE || p->state == SLEEPING) {
            p->mlfq_level = 0;
            p->ticks_in_level = 0;
        }
    }
}
```

**注意**: `boost_all` 需要在 `ptable.lock` 下调用，而时钟中断在 `trap()` 中触发，需要注意锁的嵌套关系。

### 6. I/O 奖励（可选增强）

当进程从 SLEEPING 唤醒（完成 I/O）时，提升其优先级（回到 Q0 或提升一级），奖励 I/O 密集型进程：

```c
// 在 wakeup1() 中：
p->mlfq_level = 0;   // I/O 奖励：回到最高优先级
p->ticks_in_level = 0;
```

### 7. 编写测试程序 (user/mlfqtest.c)

```
测试 1: I/O 密集型进程（频繁 sleep）vs CPU 密集型进程
        预期：I/O 进程始终在 Q0，CPU 进程逐渐降级到 Q2
测试 2: Boost 效果验证：CPU-bound 进程在 Q2 运行足够久后，
        被提升到 Q0 并短暂获得 CPU 资源
测试 3: 新进程优先级：fork 后子进程立即在 Q0 执行，
        响应时间短于已在 Q2 的旧进程
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| MLFQ 规则 | 5条 OSTEP 规则在代码中的对应实现 |
| 动态优先级 | 调度器根据行为自动调整 mlfq_level |
| 时间量（Quantum） | 每个队列有独立的时间片长度 |
| 优先级提升（Boost） | 定期将所有进程回 Q0，防止饥饿 |
| I/O 奖励 | 阻塞等 I/O 的进程唤醒后提升优先级 |
| CPU vs I/O 密集型 | MLFQ 自动区分两类进程并差异化对待 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/proc.h | 修改 | 添加 `mlfq_level`、`ticks_in_level` 字段 |
| src/proc.c | 修改 | 改造 `scheduler`，实现 `boost_all`，修改 `allocproc`/`wakeup1` |
| src/trap.c | 修改 | 时钟中断处理降级逻辑和 Boost 触发 |
| user/mlfqtest.c | 新增 | MLFQ 行为验证测试 |
| Makefile | 修改 | 添加 `mlfqtest` |

## 验证

```bash
make clean && make qemu-nox CPUS=1
$ mlfqtest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 新进程在 Q0 | 刚 fork 的进程立即在 Q0 运行 | mlfqtest 打印 level |
| CPU-bound 降级 | 连续占用 CPU 的进程从 Q0 → Q1 → Q2 | 观察 mlfq_level 变化 |
| I/O 进程保持 Q0 | 频繁 sleep 的进程始终在 Q0 | mlfqtest 验证 |
| Boost 生效 | Q2 进程每 100 ticks 被提升到 Q0 | 加入定时器输出验证 |
| usertests 通过 | 调度改动不破坏功能 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| Boost 时死锁 | `boost_all` 在时钟中断中调用，但已持 ptable.lock | 在 `acquire` 之前或之后调用 boost，检查锁嵌套 |
| 所有进程卡在 Q2 | 降级后没有 Boost，或 BOOST_INTERVAL 设置太大 | 减小 BOOST_INTERVAL，确认 Boost 被调用 |
| I/O 进程响应延迟大 | 唤醒后 I/O 奖励未生效 | 检查 wakeup1 中的 level 重置逻辑 |
| 饥饿未消除 | Boost 中遗漏 SLEEPING 状态进程 | 确认 Boost 对 `SLEEPING` 进程也重置 level |

## 关键代码路径

- 新进程入队: `proc.c:allocproc()` → `mlfq_level = 0, ticks_in_level = 0`
- 时钟降级: `trap.c:trap()` → `ticks_in_level++` → 超出 quantum → `mlfq_level++`
- Boost: `trap.c:trap()` → `ticks % BOOST_INTERVAL == 0` → `boost_all()`
- 调度: `proc.c:scheduler()` → 从 level 0 开始扫描 → 找第一个 RUNNABLE

## 设计权衡

| 方面 | 静态优先级（lab-sched-01） | MLFQ |
|------|--------------------------|------|
| 优先级设置 | 手动（setpriority） | 自动（根据行为） |
| I/O 密集型响应 | 取决于设置的优先级 | 自动保持高优先级 |
| 饥饿问题 | 存在 | Boost 机制缓解 |
| 实现复杂度 | 低 | 中等（多队列+时间片+Boost） |
| 游戏化（Cheating） | 不适用 | 进程可通过故意阻塞保持 Q0 |

## 进阶挑战

- [ ] 添加**防游戏化**：记录进程在 Q0 时消耗的总 CPU 时间，超过阈值强制降级
- [ ] 实现**Per-Level Round-Robin**：同一队列内的多个进程按 Round-Robin 轮转（而非每次选第一个）
- [ ] 参数化配置：将 NMLFQ、量子大小、BOOST_INTERVAL 改为可在 shell 中运行时调整
- [ ] 与 `lab-sched-03-stride` 对比：分别对 CPU-bound 和 I/O-bound 混合负载测量吞吐量和响应时间
- [ ] 阅读 [OSTEP 第 8 章](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-mlfq.pdf)，对照本实验代码验证每条规则
