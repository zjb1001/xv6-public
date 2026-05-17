# Lab: Stride Scheduler (Stride 比例份额调度)
[English](../../README.md)

难度: ★★★★☆

## 设计初衷

优先级调度和 MLFQ 都试图给"重要进程"更多 CPU 时间，但都无法**精确量化**CPU 分配比例。Stride 调度（比例份额调度）则给出了严格的保证：

> 如果进程 A 持有 2 份票（tickets），进程 B 持有 1 份，那么长期运行后 A 获得的 CPU 时间恰好是 B 的 2 倍。

Stride 调度的核心思想：

- 每个进程有一个**步幅（stride）** = `STRIDE_BASE / tickets`，tickets 越多步幅越小
- 每次调度选择**累计 pass 值最小**的进程运行（pass 代表"已获得的服务量"）
- 运行后 `pass += stride`，使该进程下次被选中的优先级相对降低

这是一个**确定性**的公平调度算法，与基于随机数的彩票调度（Lottery Scheduling）相比，误差更小，更快收敛到目标比例。

核心问题：*"为什么随机彩票调度的公平性只能在统计意义上保证，而 Stride 能做到确定性的公平？"*

## 前置知识

- **彩票调度（Lottery Scheduling）**: 按票数比例随机选择进程，短期有波动，长期趋近目标比例
- **Stride 调度的数学直觉**: pass 是"已消耗的虚拟时间"，总是调度虚拟时间最少的进程，使所有进程虚拟时间保持近似相等
- **整数溢出**: `pass` 值持续累加，需要处理溢出（可用模运算或定期重置）
- **新进程加入**: 新进程应将初始 pass 设为当前所有进程 pass 的**最大值**，避免新进程独占 CPU

```
Stride 调度示意（STRIDE_BASE=10000）:
进程 A: tickets=2, stride=5000, pass=0
进程 B: tickets=1, stride=10000, pass=0

轮次1: pass 最小 A(0)=B(0), 选 A → A.pass=5000
轮次2: B(0) < A(5000), 选 B → B.pass=10000
轮次3: A(5000) < B(10000), 选 A → A.pass=10000
轮次4: A(10000)=B(10000), 选 A → A.pass=15000
轮次5: B(10000) < A(15000), 选 B → B.pass=20000
...
结果：A 运行了 3 次，B 运行了 2 次（趋近 2:1）
```

## 实验内容

### 1. 添加 Stride 调度字段 (include/proc.h)

```c
#define STRIDE_BASE  1000000    // 步幅基数（tickets=1 时的步幅）
#define DEFAULT_TICKETS  10     // 默认票数

struct proc {
    // ... 现有字段 ...
    int  tickets;    // 持有的 CPU 份额票数（1..STRIDE_BASE）
    int  stride;     // 步幅 = STRIDE_BASE / tickets
    int  pass;       // 累计服务量（调度 pass 计数器）
};
```

### 2. 初始化 Stride 字段 (src/proc.c)

- `allocproc()` 中：`p->tickets = DEFAULT_TICKETS; p->stride = STRIDE_BASE / DEFAULT_TICKETS; p->pass = 0`
- **新进程 pass 初始化**：将新进程的 pass 设为当前所有 RUNNABLE 进程 pass 的最大值（避免新进程刚加入时独占 CPU）：
  ```c
  // fork 时，子进程 pass = max(所有 RUNNABLE 进程的 pass)
  int max_pass = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->state == RUNNABLE && p->pass > max_pass)
          max_pass = p->pass;
  np->pass = max_pass;
  ```

### 3. 实现 sys_settickets (src/sysproc.c)

```c
int sys_settickets(void) {
    int n;
    if(argint(0, &n) < 0) return -1;
    if(n <= 0 || n > STRIDE_BASE) return -1;
    struct proc *p = myproc();
    acquire(&ptable.lock);
    p->tickets = n;
    p->stride  = STRIDE_BASE / n;
    // 修改 stride 不重置 pass（保持已有公平性）
    release(&ptable.lock);
    return 0;
}
```

### 4. 改造调度器：选 pass 最小的 RUNNABLE 进程 (src/proc.c)

```c
void scheduler(void) {
    struct proc *p, *chosen;
    for(;;) {
        sti();
        acquire(&ptable.lock);
        chosen = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->state != RUNNABLE) continue;
            if(chosen == 0 || p->pass < chosen->pass)
                chosen = p;
        }
        if(chosen) {
            chosen->pass += chosen->stride;   // 运行前更新 pass
            chosen->state = RUNNING;
            c->proc = chosen;
            switchuvm(chosen);
            swtch(&c->scheduler, chosen->context);
            switchkvm();
            c->proc = 0;
        }
        release(&ptable.lock);
    }
}
```

**关键细节**: `pass += stride` 发生在进程**被选中时**（运行前），而不是运行结束后。这保证了 pass 在调度器视角是一致的。

### 5. 处理整数溢出

`pass` 值持续增加，32 位整数在 `STRIDE_BASE=1000000`、`tickets=1` 时约 2147 次调度后溢出。

处理方案：定期重新标准化（将所有 pass 减去最小 pass）：

```c
// 在 scheduler 循环开始时，若 chosen->pass 超过阈值，全体归一化
if(chosen && chosen->pass > 0x70000000) {
    int min_pass = chosen->pass;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if(p->state == RUNNABLE && p->pass < min_pass)
            min_pass = p->pass;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if(p->state != UNUSED)
            p->pass -= min_pass;
}
```

### 6. 编写测试并验证 CPU 比例 (user/stridetest.c)

```
测试: fork 3 个 CPU-bound 进程，分别 settickets(1), settickets(2), settickets(4)
     运行足够长时间，统计每个进程实际运行的时钟周期数
     验证比例约为 1:2:4
```

通过读取 `ticks`（`uptime()` 系统调用）或在进程退出时打印运行时间来统计。

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 比例份额调度 | tickets 决定 CPU 占用比，Stride 实现精确分配 |
| 虚拟时间 | pass 是每个进程消耗的"虚拟 CPU 时间" |
| 公平性（Fairness） | 任意时间窗口内，所有进程的 pass 增量比例与 tickets 成正比 |
| 彩票调度对比 | 随机 vs 确定性，收敛速度和误差的差异 |
| 新进程饥饿 | 新进程 pass=0 时会独占 CPU，需初始化为 max(pass) |
| 整数溢出处理 | 实际系统中需要处理计数器溢出的边界情况 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/proc.h | 修改 | 添加 `tickets`、`stride`、`pass` 字段 |
| src/proc.c | 修改 | `allocproc`/`fork` 初始化，`scheduler` 改为 pass-min 选择 |
| src/sysproc.c | 修改 | 实现 `sys_settickets` |
| include/syscall.h | 修改 | 添加 `SYS_settickets` |
| include/user.h | 修改 | 添加 `settickets` 声明 |
| user/usys.S | 修改 | 添加系统调用入口 |
| user/stridetest.c | 新增 | 比例份额验证测试 |
| Makefile | 修改 | 添加 `stridetest` |

## 验证

```bash
make clean && make qemu-nox CPUS=1
$ stridetest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| CPU 比例正确 | tickets 1:2:4 的进程，CPU 时间比约为 1:2:4 | stridetest 统计并输出比例 |
| 误差小 | 比彩票调度误差更小（10% 以内） | 运行多次统计方差 |
| 新进程不独占 | fork 后新进程不会短暂独占 CPU | stridetest 验证其他进程不中断 |
| usertests 通过 | 功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 一个进程独占 CPU | 新进程 pass=0，而其他进程 pass 很大 | fork 时初始化 pass 为 max(pass) |
| 比例偏差很大 | pass 溢出导致进程顺序颠倒 | 添加溢出重置逻辑 |
| pass+=stride 位置错误 | 在 swtch 之后更新，导致多核条件下竞态 | 在调度器中选中后立即更新，而非运行完后 |

## 关键代码路径

- 初始化: `proc.c:allocproc()` → `tickets=DEFAULT_TICKETS, stride=STRIDE_BASE/tickets, pass=0`
- fork 继承: `proc.c:fork()` → `np->pass = max(所有 RUNNABLE 进程的 pass)`
- 调度: `proc.c:scheduler()` → 扫描全表 → 选 pass 最小的 RUNNABLE → `pass += stride`
- 修改票数: `sysproc.c:sys_settickets()` → 修改 tickets 和 stride

## 设计权衡

| 方面 | 彩票调度（Lottery） | Stride 调度 |
|------|--------------------|------------|
| 公平性 | 统计意义上的公平 | 确定性精确公平 |
| 短期误差 | 较大（随机性） | 极小（确定性） |
| 实现复杂度 | 简单（随机数） | 中等（pass 管理） |
| 新进程处理 | 自然（随机公平） | 需要特殊处理 pass 初始值 |
| 整数溢出 | 随机数不溢出 | 需要处理 pass 溢出 |

## 进阶挑战

- [ ] 实现**彩票调度**（Lottery Scheduling），对比两者在相同 tickets 下的 CPU 比例误差
- [ ] 实现 **`sys_gettickets`**：查询指定进程的 tickets 数
- [ ] 实现**票数转让（Ticket Transfer）**：进程 A 在等待 B 完成时，可将 tickets 转让给 B，提升 B 的优先级（解决优先级反转）
- [ ] 在 CPUS=2 下测试 Stride 的多核行为，分析为什么多核环境下 Stride 的精度会下降
- [ ] 阅读 [Waldspurger & Weihl, OSDI 1994](https://www.usenix.org/conference/osdi/stride-scheduling-deterministic-proportional-share-resource-management) 原始论文，对比本实现与论文设计的差异
