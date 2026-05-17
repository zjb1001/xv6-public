# Lab: Read-Write Lock (读写锁)
[English](../../README.md)

难度: ★★★★☆

## 设计初衷

互斥锁（Mutex）对读操作过于保守：多个进程**同时读**共享数据是安全的，根本不需要互斥。但 Mutex 在任何访问（读或写）期间都独占锁，这使得大量"读多写少"的场景（如数据库索引、配置文件、共享缓存）性能低下。

**读写锁（Read-Write Lock）** 区分两种访问模式：
- **读锁（共享锁 / Shared Lock）**：多个读者可同时持有，互不排斥
- **写锁（独占锁 / Exclusive Lock）**：写者独占，期间任何读者和写者都必须等待

核心语义：
```
读锁: 允许 N 个读者同时持有（N ≥ 1）
写锁: 同时只允许 1 个写者，且无读者
```

本实验同时探讨**读写锁的公平性问题**：朴素实现会导致**读者饥饿写者**（只要不断有读者到来，写者永远等不到）。

核心问题：*"如何在允许并发读的同时保证写的独占性？如何防止写者饥饿？"*

## 前置知识

- **lab-sync-01-mutex**: 本实验基于用户态 Mutex 实现，依赖原子操作和 futex 概念
- **经典读者-写者问题（Readers-Writers Problem）**: Dijkstra 于 1971 年提出，是同步理论的经典问题之一，有两种变体：第一读者-写者问题（读者优先）和第二读者-写者问题（写者优先）
- **条件变量（Condition Variable）**: 在 Mutex 基础上实现"等待某条件成立"，由 `cv_wait` 和 `cv_signal` 组成。本实验用 futex 模拟条件变量

```
朴素实现（读者优先）的饥饿场景:

时间轴: R=读者到来, W=写者等待
R1 加读锁 → R2 加读锁 → ... → W 等待 → R3 加读锁 → ...
                                          读者持续到来
                                          W 永远无法获取写锁 ← 饥饿！

写者优先解决方案:
新读者到来时，若有等待写者，则读者也等待（让写者先行）
```

## 实验内容

### 1. 定义读写锁数据结构（lib/xv6_rwlock.h）

```c
typedef struct {
    int readers;        // 当前持有读锁的读者数量
    int writers;        // 当前持有写锁的写者数量（0 或 1）
    int write_waiters;  // 等待写锁的数量（用于写者优先）
    mutex_t lock;       // 保护上述字段的互斥锁
    // futex 变量：读者等待、写者等待
    volatile int r_futex;  // 读者等待变量
    volatile int w_futex;  // 写者等待变量
} rwlock_t;
```

### 2. 实现读者优先版本（lib/xv6_rwlock.c）

**读者优先**：只要有读者持有读锁，写者就必须等待。新读者不受等待写者的影响。

```c
void rwlock_init(rwlock_t *rw) {
    rw->readers = rw->writers = rw->write_waiters = 0;
    mutex_init(&rw->lock);
    rw->r_futex = rw->w_futex = 0;
}

// 获取读锁（读者优先）
void rwlock_rlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    // 等待写者完成
    while(rw->writers > 0) {
        int val = rw->r_futex;
        mutex_unlock(&rw->lock);
        futex_wait(&rw->r_futex, val);   // 睡眠等待写者释放
        mutex_lock(&rw->lock);
    }
    rw->readers++;
    mutex_unlock(&rw->lock);
}

// 释放读锁
void rwlock_runlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    rw->readers--;
    if(rw->readers == 0) {
        // 最后一个读者离开，唤醒等待的写者
        rw->w_futex++;
        mutex_unlock(&rw->lock);
        futex_wake(&rw->w_futex);
        return;
    }
    mutex_unlock(&rw->lock);
}

// 获取写锁（读者优先）
void rwlock_wlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    while(rw->readers > 0 || rw->writers > 0) {
        int val = rw->w_futex;
        mutex_unlock(&rw->lock);
        futex_wait(&rw->w_futex, val);
        mutex_lock(&rw->lock);
    }
    rw->writers = 1;
    mutex_unlock(&rw->lock);
}

// 释放写锁
void rwlock_wunlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    rw->writers = 0;
    // 唤醒所有等待读者和一个等待写者
    rw->r_futex++;
    rw->w_futex++;
    mutex_unlock(&rw->lock);
    futex_wake(&rw->r_futex);  // FUTEX_WAKE_ALL（唤醒所有等待读者）
    futex_wake(&rw->w_futex);
}
```

### 3. 实现写者优先版本（lib/xv6_rwlock.c）

**写者优先**：若有等待的写者，新读者需排队等待（写者先行）。这防止了写者饥饿。

关键差异：获取读锁时，若 `write_waiters > 0`，读者也需等待：

```c
void rwlock_rlock_wfirst(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    // 有写者正在等待或持有锁时，读者等待
    while(rw->writers > 0 || rw->write_waiters > 0) {
        int val = rw->r_futex;
        mutex_unlock(&rw->lock);
        futex_wait(&rw->r_futex, val);
        mutex_lock(&rw->lock);
    }
    rw->readers++;
    mutex_unlock(&rw->lock);
}

void rwlock_wlock_wfirst(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    rw->write_waiters++;          // 注册等待意图，阻止新读者进入
    while(rw->readers > 0 || rw->writers > 0) {
        int val = rw->w_futex;
        mutex_unlock(&rw->lock);
        futex_wait(&rw->w_futex, val);
        mutex_lock(&rw->lock);
    }
    rw->write_waiters--;
    rw->writers = 1;
    mutex_unlock(&rw->lock);
}
```

### 4. 扩展 futex 支持广播唤醒（多个等待者）

修改 `sys_futex` 的 `FUTEX_WAKE_ALL` 选项（或多次 futex_wake），确保写锁释放时能唤醒**所有**等待的读者：

```c
// 在释放写锁后，若需要唤醒所有读者：
// 方案 1: 修改 futex_wake 增加 count 参数（wake 最多 count 个等待者）
// 方案 2: 在 wakeup() 中唤醒所有等待同一 channel 的进程（xv6 默认行为）
```

### 5. 编写测试（user/rwtest.c）

```
测试 1: 并发读 —— N 个读者同时读，验证不互斥（全部同时运行）
测试 2: 写排斥 —— 写者持锁期间，读者必须等待
测试 3: 读排斥写 —— 读者持锁期间，写者必须等待
测试 4: 饥饿验证 —— 持续产生新读者，观察写者是否能获得写锁
         读者优先版本：写者可能饥饿
         写者优先版本：写者最终获得锁
测试 5: 数据一致性 —— 写者写数据后，读者读到最新值
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 读者-写者问题 | 允许并发读，写时独占的同步需求 |
| 共享锁 / 独占锁 | 读锁共享，写锁独占 |
| 饥饿（Starvation） | 读者优先时写者可能永远等待 |
| 写者优先 | 注册等待意图（write_waiters）阻止新读者 |
| 条件等待 | futex_wait 实现"等待某条件成立" |
| 公平性 | 调度策略中对不同类型请求的先后顺序处理 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| lib/xv6_rwlock.c | 新增 | 读写锁实现（读者优先 + 写者优先） |
| lib/xv6_rwlock.h | 新增 | `rwlock_t` 定义和函数声明 |
| lib/xv6_mutex.c/h | 依赖 | 读写锁基于 mutex 实现 |
| src/sysproc.c | 依赖/修改 | 扩展 futex 支持广播唤醒 |
| user/rwtest.c | 新增 | 读写锁测试和饥饿演示 |

## 验证

```bash
make clean && make qemu-nox
$ rwtest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 并发读 | N 个读者同时运行（无互斥） | rwtest 输出时间重叠 |
| 写独占 | 写者持锁期间无其他读写 | rwtest 验证临界区日志 |
| 读者优先饥饿 | 持续读者下写者等待 | rwtest 计时观察 |
| 写者优先无饥饿 | 写者最终获得锁 | rwtest 写者优先版本验证 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 读者同时持锁后写者进入 | rwlock_wlock 未正确等待 readers>0 | 检查 while 条件包含 readers>0 |
| 写者优先仍饥饿 | write_waiters 未在读者检查中生效 | 确认读者检查 `write_waiters > 0` |
| futex_wake 只唤醒一个读者 | wakeup() 设计唤醒所有等待者（xv6 默认行为），检查使用方式 | 验证 xv6 wakeup 确实唤醒所有等待同一 channel 的进程 |

## 关键代码路径

- 并发读: `rwlock_rlock()` → `readers++` → 读操作（并发） → `rwlock_runlock()` → `readers--`
- 写独占: `rwlock_wlock()` → 等待 `readers==0 && writers==0` → `writers=1` → 写操作 → `rwlock_wunlock()`
- 写者优先: `rwlock_wlock_wfirst()` → `write_waiters++` → 等待 → 新读者看到 `write_waiters>0` 也等待

## 设计权衡

| 方面 | 读者优先 | 写者优先 | 公平排队（FIFO） |
|------|---------|---------|----------------|
| 并发读性能 | 高 | 中（有写者等待时读者也等） | 中 |
| 写者延迟 | 高（可能饥饿） | 低（写者快速获锁） | 低（按到达顺序） |
| 实现复杂度 | 低 | 中 | 高 |
| 适用场景 | 读极多写极少 | 读写相对均衡 | 严格公平需求 |

## 进阶挑战

- [ ] 实现**公平 FIFO 读写锁**：按请求到达顺序授权，彻底消除饥饿（提示：维护等待队列）
- [ ] 实现**升级锁（Upgrade Lock）**：读者升级为写者时不释放再重新获取（原子升级，避免竞态）
- [ ] 分析 xv6 `bio.c` 的 `bcache.lock`：它是否可以改成读写锁？分析 `bget` 的读写模式
- [ ] 在 **lab-fs-03-lrucache** 的 LRU 缓存上应用读写锁，测试并发读的性能提升
- [ ] 统计实际工作负载（ls/cat 多次运行）中读操作与写操作的比例，分析读写锁的实际收益
