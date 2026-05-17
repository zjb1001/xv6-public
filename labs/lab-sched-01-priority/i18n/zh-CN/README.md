# Lab: Priority Scheduler (优先级调度器)
[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

xv6 的 Round-Robin 调度器对所有进程一视同仁——shell、后台任务、音频播放器共用同一时间片粒度。而 `lab-fifo-sched` 实现了严格 FIFO，保证顺序但完全牺牲了响应性。

真实操作系统（Linux、macOS、Windows）都采用**优先级调度**：

- 每个进程有一个优先级值，调度器总是选择**优先级最高**的 RUNNABLE 进程
- 高优先级进程（如交互式 shell）响应延迟低
- 低优先级进程（如后台编译）不影响前台响应

本实验在 xv6 中实现静态优先级调度，并配套 `setpriority`/`getpriority` 系统调用。

核心问题：*"优先级调度带来了响应时间的改善，但也引入了什么新问题？"*

## 前置知识

- **xv6 调度器**: `src/proc.c:scheduler()` 在循环中扫描 `ptable.proc[]`，找 RUNNABLE 进程切换。目前找到第一个 RUNNABLE 就切换（Round-Robin 依赖时钟中断，非严格循环）
- **`ptable.lock`**: 保护进程表（`ptable`）的自旋锁，调度器在持锁状态下选择下一个进程
- **优先级反转（Priority Inversion）**: 低优先级进程持有高优先级进程需要的锁，导致高优先级进程被间接阻塞
- **饥饿（Starvation）**: 低优先级进程因为高优先级进程持续 RUNNABLE 而永远得不到 CPU

```
优先级调度示例：
进程 A (priority=0, 最高): 交互式 shell
进程 B (priority=5):        普通程序
进程 C (priority=10, 最低): 后台任务

A RUNNABLE 时: 无论 B/C 状态，始终调度 A
A SLEEPING 时: 在 B/C 中选 priority 最小（最高）的 B
```

## 实验内容

### 1. 在进程控制块中添加优先级字段 (include/proc.h)

```c
struct proc {
    // ... 现有字段 ...
    int priority;    // 调度优先级: 0=最高, 19=最低（类比 Unix nice 值）
};
```

**约定**: `priority` 值越小，优先级越高（类比 Linux `nice` 值，0 为默认）。

### 2. 初始化优先级 (src/proc.c)

- `allocproc()` 中将 `p->priority = 0`（默认最高，公平起步）
- `fork()` 中子进程继承父进程优先级：`np->priority = curproc->priority`

### 3. 实现 sys_setpriority / sys_getpriority (src/sysproc.c)

```c
int sys_setpriority(void) {
    int pid, priority;
    // 读取参数：pid（0 = 当前进程），priority（0..19）
    // 权限检查（可选）：仅允许降低自身优先级（nice 语义）
    // 找到目标进程，修改 priority
}

int sys_getpriority(void) {
    int pid;
    // 返回目标进程的 priority
}
```

注册到 `src/syscall.c` 和 `include/syscall.h`。

### 4. 改造调度器：选最高优先级 RUNNABLE 进程 (src/proc.c)

将 `scheduler()` 的进程选择逻辑从"找第一个 RUNNABLE"改为"找 priority 最小的 RUNNABLE"：

```c
void scheduler(void) {
    struct proc *p, *chosen;
    for(;;) {
        sti();
        acquire(&ptable.lock);
        chosen = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->state != RUNNABLE) continue;
            if(chosen == 0 || p->priority < chosen->priority)
                chosen = p;
        }
        if(chosen) {
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

**注意**: 这个改造后调度器时间复杂度为 O(NPROC)，每次调度扫描全表。

### 5. 演示优先级反转（思考题）

设计一个场景展示优先级反转：

```
进程 L (priority=10) 持有 spinlock S
进程 H (priority=0)  等待 spinlock S
进程 M (priority=5)  CPU-bound

实际运行顺序：M 比 L 先跑，因为 H 被阻塞，L 一直拿不到调度
问题：H 实际上被 M（低优先级）阻塞，而非直接被 L 阻塞
```

### 6. 编写测试程序 (user/priotest.c)

```
测试 1: fork 3 个进程，分别 setpriority 为 0/5/10，验证完成顺序符合优先级
测试 2: 高优先级进程 CPU-bound，低优先级进程能否得到 CPU？（饥饿演示）
测试 3: 继承验证——子进程优先级继承父进程
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 静态优先级 | priority 字段固定，除非显式调用 setpriority |
| 调度策略 | 调度器在所有 RUNNABLE 进程中选 priority 最小者 |
| 饥饿问题 | 低优先级进程在高优先级进程持续 RUNNABLE 时无法执行 |
| 优先级继承 | fork 时子进程继承父进程优先级 |
| 优先级反转 | 低优先级持锁，高优先级等锁，中优先级抢走 CPU |
| nice 值 | Unix/Linux 的用户可调优先级接口，本实验的设计原型 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/proc.h | 修改 | 添加 `priority` 字段 |
| src/proc.c | 修改 | `allocproc`/`fork` 初始化优先级；`scheduler` 改造 |
| src/sysproc.c | 修改 | 添加 `sys_setpriority`/`sys_getpriority` |
| include/syscall.h | 修改 | 添加系统调用编号 |
| src/syscall.c | 修改 | 注册系统调用 |
| include/user.h | 修改 | 添加用户态声明 |
| user/usys.S | 修改 | 添加系统调用汇编入口 |
| user/priotest.c | 新增 | 优先级调度验证测试 |

## 验证

```bash
make clean && make qemu-nox CPUS=1
$ priotest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 高优先级先完成 | priority=0 的进程比 priority=10 的早结束 | priotest 输出完成顺序 |
| 低优先级被饥饿 | 存在高 CPU-bound 时，低优先级进程不能运行 | priotest 计时统计 |
| setpriority 生效 | 运行时修改优先级立即影响调度 | priotest 动态修改验证 |
| 多核安全 | CPUS=2 下无崩溃，优先级依然生效 | 运行 usertests |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 所有进程优先级相同时无序 | 相同优先级下无 tiebreaker | 可加入 Round-Robin 作为次级策略 |
| setpriority 对其他进程无效 | 加锁前 pid 查找不正确 | 在 ptable.lock 保护下查找并修改 |
| 内核 panic 在 scheduler | 选中的进程已经不是 RUNNABLE | 加锁后需要重新检查 state |

## 关键代码路径

- 初始化: `proc.c:allocproc()` → `p->priority = 0`
- 继承: `proc.c:fork()` → `np->priority = curproc->priority`
- 调度: `proc.c:scheduler()` → 扫描全表 → 选 `priority` 最小的 RUNNABLE
- 修改优先级: `sysproc.c:sys_setpriority()` → 查找进程 → 修改 `p->priority`

## 设计权衡

| 方面 | Round-Robin（原始） | 优先级调度 |
|------|--------------------|---------|
| 公平性 | 所有进程等时间片 | 低优先级可能饥饿 |
| 响应时间 | 有上限（一个时间片） | 高优先级极低延迟 |
| 实现复杂度 | O(1) 轮转 | O(NPROC) 扫描 |
| 饥饿问题 | 无 | 有（低优先级进程） |
| 适用场景 | 公平批处理 | 交互式 + 后台任务混合 |

## 进阶挑战

- [ ] 实现**老化（Aging）**：低优先级进程每等待 N 个时钟周期，自动提升优先级，防止饥饿
- [ ] 实现**优先级天花板协议（Priority Ceiling Protocol）**：持锁时临时提升到锁的最高使用者优先级
- [ ] 将调度器改为**优先级队列**（数组按优先级分 20 个链表），避免 O(NPROC) 扫描
- [ ] 在 CPUS=2 下实现**多核负载均衡**：空闲 CPU 从其他 CPU 偷取高优先级进程
- [ ] 与 `lab-sched-02-mlfq` 对比：讨论静态优先级与动态优先级的各自适用场景
