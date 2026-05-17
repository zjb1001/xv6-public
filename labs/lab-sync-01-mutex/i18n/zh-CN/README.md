# Lab: User-Level Mutex (用户态互斥锁)
[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

xv6 内核实现了自旋锁（spinlock）和睡眠锁（sleeplock），但这些是**内核态**的同步原语，用户程序无法直接使用。

在用户态实现互斥锁需要两个关键能力：
1. **原子操作**：测试并设置（test-and-set）一个标志位，这在 x86 上通过 `xchg` 指令实现，无法被中断打断
2. **阻塞**：当锁被占用时，不应忙等（浪费 CPU），而应让出处理器，等待锁释放时被唤醒

这引出了经典的**自旋 vs 睡眠**权衡：
- **自旋锁（Spinlock）**：忙等，适合锁持有时间极短的场景（如内核中断处理）
- **睡眠互斥锁（Mutex）**：阻塞，让出 CPU，适合用户态长时间等待

更高效的方案是 **futex（Fast Userspace muTEX）**：先在用户态尝试 CAS（Compare-And-Swap），失败才进入内核阻塞，减少不必要的系统调用。

核心问题：*"为什么用户态无法用普通读写操作实现互斥？x86 的 xchg 指令为何可以？"*

## 前置知识

- **竞态条件**: 两个线程同时执行 `if(!locked) locked=1; // 临界区`，两者都可能通过 if 检查并同时进入临界区
- **x86 原子操作**: `xchg` 指令在执行期间持有内存总线锁，保证读-改-写的原子性：`old = xchg(&lock, 1)` 若 old==0 则获取成功
- **用户态线程**: 若使用 lab-lib-03 的协程库，锁的自旋会阻止 yield；真正的阻塞需要内核帮助
- **内核 futex 系统调用**: Linux 的 futex(FUTEX_WAIT) 将用户地址上的值与期望值比较，若相等则阻塞；futex(FUTEX_WAKE) 唤醒等待者

```
CAS（Compare-And-Swap）语义:
原子执行: if(*addr == expected) { *addr = new; return true; }
         else { return false; }

自旋锁实现:
while(xchg(&lock, 1) == 1) { /* 自旋等待 */ }
// 进入临界区
xchg(&lock, 0);  // 释放锁
```

## 实验内容

### 1. 实现 x86 原子 xchg（lib/xv6_mutex.c）

```c
// x86 xchg 指令：原子交换 *addr 和 newval，返回旧值
static inline uint xchg(volatile uint *addr, uint newval) {
    uint result;
    asm volatile("lock; xchgl %0, %1"
                 : "+m" (*addr), "=a" (result)
                 : "1" (newval)
                 : "cc");
    return result;
}
```

**实验任务**: 理解上述内联汇编的每个约束符号含义（`+m`、`=a`、`"1"`、`cc`），并在注释中解释。

### 2. 实现自旋互斥锁（lib/xv6_mutex.c）

```c
typedef struct {
    volatile uint locked;  // 0=未锁，1=已锁
} spinmutex_t;

void spinmutex_init(spinmutex_t *m) { m->locked = 0; }

void spinmutex_lock(spinmutex_t *m) {
    while(xchg(&m->locked, 1) != 0)
        ;  // 自旋
}

void spinmutex_unlock(spinmutex_t *m) {
    xchg(&m->locked, 0);
}
```

### 3. 添加内核 futex 系统调用（src/sysproc.c）

futex 允许用户程序在**锁有竞争时才进入内核**：

```c
// futex_wait(addr, val): 若 *addr == val 则阻塞
// futex_wake(addr):      唤醒等待 addr 的一个进程
#define FUTEX_WAIT  0
#define FUTEX_WAKE  1

int sys_futex(void) {
    int *uaddr, op, val;
    if(argptr(0, (char**)&uaddr, sizeof(int)) < 0
       || argint(1, &op) < 0 || argint(2, &val) < 0) return -1;

    if(op == FUTEX_WAIT) {
        // 检查 *uaddr 是否仍等于 val（在持锁期间检查）
        acquire(&ptable.lock);  // 或专用 futex 锁
        if(*uaddr != val) {
            release(&ptable.lock);
            return 0;  // 值已变化，无需等待
        }
        // 阻塞当前进程
        sleep(uaddr, &ptable.lock);
        release(&ptable.lock);
        return 0;
    } else if(op == FUTEX_WAKE) {
        // 唤醒所有等待在 uaddr 上的进程
        wakeup(uaddr);
        return 0;
    }
    return -1;
}
```

### 4. 实现基于 futex 的睡眠互斥锁（lib/xv6_mutex.c）

```c
typedef struct {
    volatile int state;  // 0=未锁，1=锁定无等待者，2=锁定有等待者
} mutex_t;

void mutex_init(mutex_t *m) { m->state = 0; }

void mutex_lock(mutex_t *m) {
    // 快速路径：CAS(0 → 1)
    int c;
    if((c = __sync_val_compare_and_swap(&m->state, 0, 1)) != 0) {
        // 慢路径：有竞争，进入内核等待
        do {
            if(c == 2 || __sync_val_compare_and_swap(&m->state, 1, 2) != 0)
                futex_wait(&m->state, 2);
        } while((c = __sync_val_compare_and_swap(&m->state, 0, 2)) != 0);
    }
}

void mutex_unlock(mutex_t *m) {
    if(__sync_fetch_and_sub(&m->state, 1) != 1) {
        // 有等待者（state 曾为 2），需要唤醒
        m->state = 0;
        futex_wake(&m->state);
    }
}
```

### 5. 实现头文件（lib/xv6_mutex.h）

```c
#ifndef XV6_MUTEX_H
#define XV6_MUTEX_H

typedef struct { volatile uint locked; }  spinmutex_t;
typedef struct { volatile int  state;  }  mutex_t;

void spinmutex_init(spinmutex_t *m);
void spinmutex_lock(spinmutex_t *m);
void spinmutex_unlock(spinmutex_t *m);

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
```

### 6. 编写测试（user/mutextest.c）

```
测试 1: 自旋锁基本正确性 —— 两个进程累加共享计数器，验证无竞态
测试 2: 睡眠互斥锁基本正确性 —— 同上，验证结果正确
测试 3: 性能对比 —— 在高竞争下，自旋锁 vs 睡眠锁的 CPU 使用率
测试 4: 死锁检测 —— 同一进程两次 mutex_lock 会死锁（演示）
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 竞态条件 | 多进程同时修改共享变量导致结果不确定 |
| 原子操作 | x86 `xchg`/`lock cmpxchg` 保证读-改-写原子性 |
| 自旋锁 | 忙等待，适合短临界区，浪费 CPU |
| 睡眠互斥锁 | 竞争时进入内核阻塞，节省 CPU |
| futex | 无竞争时纯用户态，有竞争时才进内核 |
| 临界区 | 必须在锁保护下才能访问的代码段 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| lib/xv6_mutex.c | 新增 | spinmutex 和 mutex 实现 |
| lib/xv6_mutex.h | 新增 | 类型定义和函数声明 |
| src/sysproc.c | 修改 | 实现 `sys_futex` |
| include/syscall.h | 修改 | 添加 `SYS_futex` |
| include/user.h | 修改 | 添加 `futex_wait`/`futex_wake` 用户声明 |
| user/mutextest.c | 新增 | 互斥锁测试 |

## 验证

```bash
make clean && make qemu-nox
$ mutextest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 无竞态 | 100 次并发加法结果正确（期望 N*100） | mutextest 验证 |
| futex 减少系统调用 | 无竞争时 futex_wait 不被调用 | 添加内核计数验证 |
| usertests 通过 | 已有功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 计数结果错误 | 锁实现有 bug，临界区未被保护 | 用 GDB 断点调试 xchg 返回值 |
| 进程卡死 | futex_wait 后未被 wake | 检查 wakeup 的第一个参数是否与 sleep 相同 |
| xchg 编译报错 | 内联汇编约束不对 | 参考 xv6 spinlock.c 中的 xchg 实现 |

## 关键代码路径

- 原子获取锁: `xchg(&m->locked, 1)` → 若返回 0 则成功，否则自旋或进入 futex_wait
- futex 阻塞: `sys_futex(FUTEX_WAIT)` → 验证 *uaddr==val → `sleep(uaddr, lock)`
- futex 唤醒: `mutex_unlock()` → `futex_wake()` → `sys_futex(FUTEX_WAKE)` → `wakeup(uaddr)`

## 设计权衡

| 方面 | 自旋锁 | 睡眠互斥锁 | futex |
|------|--------|----------|-------|
| 无竞争开销 | 极低（1个原子指令） | 低（1个原子指令） | 极低（纯用户态） |
| 有竞争开销 | 高（CPU 忙等） | 低（进程睡眠） | 低（仅竞争时进内核） |
| 适用场景 | 内核短临界区 | 用户态一般场景 | 高性能用户态同步 |
| 死锁风险 | 有（同线程重入） | 有 | 有 |

## 进阶挑战

- [ ] 实现**可重入互斥锁（Recursive Mutex）**：记录持有者 tid，同一线程可多次 lock
- [ ] 实现**超时等待**：`mutex_timedlock(m, timeout_ms)` —— futex_wait 加超时参数
- [ ] 实现**死锁检测**：维护锁的持有关系图，检测环（等待图算法）
- [ ] 结合 **lab-sync-03-semaphore** 和 **lab-proc-03-shm**：用共享内存的信号量实现跨进程互斥
- [ ] 对比分析：在单 CPU 和多 CPU（`make CPUS=4 qemu-nox`）下，自旋锁和睡眠锁的性能差异有何规律？
