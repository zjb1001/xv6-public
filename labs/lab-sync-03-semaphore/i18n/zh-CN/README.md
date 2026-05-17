# Lab: Semaphore (信号量)
[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

互斥锁是"二值信号量"的特例：值只有 0（未锁）和 1（已锁）。**计数信号量（Counting Semaphore）** 将这个概念推广：允许 N 个线程同时进入临界区。

信号量由 Dijkstra 在 1965 年提出，是操作系统同步理论中最经典的原语之一。它的核心操作：

- **P（Proberen，等待/down）**：信号量值减 1，若值为负则阻塞（挂起等待）
- **V（Verhogen，发信号/up）**：信号量值加 1，若有等待者则唤醒一个

信号量的强大之处在于它能优雅地解决**生产者-消费者**问题：

```
生产者-消费者（环形缓冲区，容量 N）:
empty = Semaphore(N)   // 空槽数量
full  = Semaphore(0)   // 满槽数量
mutex = Semaphore(1)   // 缓冲区互斥访问

生产者:                          消费者:
P(empty)  // 等待有空槽           P(full)   // 等待有数据
P(mutex)  // 进入临界区           P(mutex)  // 进入临界区
  写数据到 buffer                   从 buffer 读数据
V(mutex)  // 离开临界区           V(mutex)  // 离开临界区
V(full)   // 通知消费者有数据       V(empty)  // 通知生产者有空槽
```

核心问题：*"信号量和互斥锁有什么本质区别？为什么不能用互斥锁直接实现生产者-消费者？"*

## 前置知识

- **lab-sync-01-mutex**: 信号量基于 Mutex + futex 实现，依赖原子操作概念
- **经典同步问题**:
  - 生产者-消费者（有界缓冲区）
  - 哲学家就餐（资源分配，死锁预防）
  - 读者-写者（参见 lab-sync-02）
- **二值信号量 vs 互斥锁的区别**:
  - 互斥锁必须由**同一线程**加锁和解锁（所有权）
  - 二值信号量可由**不同线程**进行 P 和 V（适合用于线程间同步/通知）

```
信号量 sem(初值=3) 允许 3 个资源同时使用:

sem=3: T1 P → sem=2: T2 P → sem=1: T3 P → sem=0
                                             T4 P → 阻塞 (sem=-1)
T1 V → sem=0 → 唤醒 T4 → T4 进入
```

## 实验内容

### 1. 定义信号量数据结构（lib/xv6_semaphore.h）

```c
#ifndef XV6_SEMAPHORE_H
#define XV6_SEMAPHORE_H

#include "xv6_mutex.h"

typedef struct {
    volatile int value;     // 信号量当前值
    volatile int futex_v;   // futex 等待变量
    mutex_t      mutex;     // 保护 value 的互斥锁
} sem_t;

void sem_init(sem_t *s, int value);
void sem_wait(sem_t *s);    // P 操作 / down
void sem_post(sem_t *s);    // V 操作 / up

#endif
```

### 2. 实现信号量（lib/xv6_semaphore.c）

```c
void sem_init(sem_t *s, int value) {
    s->value = value;
    s->futex_v = 0;
    mutex_init(&s->mutex);
}

// P 操作：等待信号量可用
void sem_wait(sem_t *s) {
    mutex_lock(&s->mutex);
    s->value--;
    if(s->value < 0) {
        // 需要阻塞：保存 futex_v 的当前值，解锁后等待
        int expected = s->futex_v;
        mutex_unlock(&s->mutex);
        futex_wait(&s->futex_v, expected);
        return;
    }
    mutex_unlock(&s->mutex);
}

// V 操作：释放信号量并唤醒等待者
void sem_post(sem_t *s) {
    mutex_lock(&s->mutex);
    s->value++;
    if(s->value <= 0) {
        // 有等待者（value 之前为负），唤醒一个
        s->futex_v++;
        mutex_unlock(&s->mutex);
        futex_wake(&s->futex_v);
        return;
    }
    mutex_unlock(&s->mutex);
}
```

**思考题**: `sem_wait` 在 `futex_wait` 返回后，是否需要重新检查 `value < 0`？（提示：虚假唤醒 / spurious wakeup）

### 3. 实现生产者-消费者（user/semtest.c 第一部分）

```c
#define BUFSIZE 8

int buffer[BUFSIZE];
int head = 0, tail = 0;
sem_t empty, full, mutex_sem;  // 用信号量实现互斥

void producer(int n) {
    for(int i = 0; i < n; i++) {
        sem_wait(&empty);            // 等待有空槽
        sem_wait(&mutex_sem);        // 进入临界区
        buffer[tail] = i;
        tail = (tail + 1) % BUFSIZE;
        sem_post(&mutex_sem);        // 离开临界区
        sem_post(&full);             // 通知消费者
    }
}

void consumer(int n) {
    for(int i = 0; i < n; i++) {
        sem_wait(&full);             // 等待有数据
        sem_wait(&mutex_sem);        // 进入临界区
        int item = buffer[head];
        head = (head + 1) % BUFSIZE;
        sem_post(&mutex_sem);        // 离开临界区
        sem_post(&empty);            // 通知生产者
        printf(1, "consumed: %d\n", item);
    }
}

// 初始化：empty=BUFSIZE, full=0, mutex_sem=1
sem_init(&empty, BUFSIZE);
sem_init(&full, 0);
sem_init(&mutex_sem, 1);
```

### 4. 实现哲学家就餐（user/semtest.c 第二部分）

5 位哲学家，5 根筷子（每位需要左右各一根）：

```c
sem_t chopstick[5];  // 每根筷子初始化为 1

void philosopher(int id) {
    int left  = id;
    int right = (id + 1) % 5;
    // 死锁预防：最后一个哲学家先取右筷再取左筷
    if(id == 4) { sem_wait(&chopstick[right]); sem_wait(&chopstick[left]); }
    else        { sem_wait(&chopstick[left]);  sem_wait(&chopstick[right]); }
    printf(1, "philosopher %d eating\n", id);
    // 模拟用餐
    sem_post(&chopstick[left]);
    sem_post(&chopstick[right]);
}
```

**思考题**: 为什么简单地"先取左后取右"会导致死锁？你的解决方案是否完全避免了死锁？

### 5. 实现带超时的 sem_trywait（可选）

```c
// 非阻塞尝试：若信号量 >0 则立即获取，否则返回 -1
int sem_trywait(sem_t *s) {
    mutex_lock(&s->mutex);
    if(s->value > 0) {
        s->value--;
        mutex_unlock(&s->mutex);
        return 0;
    }
    mutex_unlock(&s->mutex);
    return -1;
}
```

### 6. 测试总结（user/semtest.c）

```
测试 1: 二值信号量行为（等同 mutex，但可跨进程 V）
测试 2: 生产者-消费者正确性（数据不丢失、不重复）
测试 3: 哲学家就餐（无死锁，所有哲学家都能吃到饭）
测试 4: sem_trywait 非阻塞验证
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 计数信号量 | value 表示可用资源数，可为负（负值 = 等待者数） |
| P/V 操作 | sem_wait / sem_post，原子性通过 mutex 保证 |
| 生产者-消费者 | 三个信号量：empty, full, mutex 协调读写 |
| 哲学家就餐 | 资源分配的死锁预防（破坏循环等待条件） |
| 死锁预防 | 打破"循环等待"：最后一个哲学家反转取筷顺序 |
| 二值信号量 vs 互斥锁 | 信号量无所有权，可跨线程/进程进行 P/V |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| lib/xv6_semaphore.c | 新增 | 信号量实现 |
| lib/xv6_semaphore.h | 新增 | sem_t 定义和函数声明 |
| lib/xv6_mutex.c/h | 依赖 | 信号量基于 mutex + futex |
| user/semtest.c | 新增 | 生产者-消费者 + 哲学家就餐测试 |

## 验证

```bash
make clean && make qemu-nox
$ semtest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 生产者-消费者正确性 | 消费者接收到所有生产的数据（顺序正确） | semtest 验证总和 |
| 无死锁 | 哲学家最终全部完成用餐 | semtest 不卡死 |
| sem_trywait 非阻塞 | 值为 0 时立即返回 -1 | semtest 验证 |
| usertests 通过 | 已有功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 生产者数据丢失 | mutex_sem 未正确保护 buffer | 确认 sem_wait(&mutex_sem) 在所有 buffer 访问前调用 |
| 哲学家死锁 | 全部哲学家同时取左筷 | 实现资源排序或最后一个反转取筷顺序 |
| futex_wait 虚假唤醒后进入临界区 | sem_wait 未重新检查 value | 在 futex_wait 返回后重新检查条件（循环等待） |

## 关键代码路径

- P 操作: `sem_wait()` → `value--` → 若 `<0` 则 `futex_wait` 阻塞
- V 操作: `sem_post()` → `value++` → 若 `<=0` 则 `futex_wake` 唤醒一个等待者
- 生产者: `P(empty)` → `P(mutex)` → 写数据 → `V(mutex)` → `V(full)`
- 消费者: `P(full)` → `P(mutex)` → 读数据 → `V(mutex)` → `V(empty)`

## 设计权衡

| 方面 | 互斥锁 (Mutex) | 二值信号量 | 计数信号量 |
|------|--------------|----------|----------|
| 资源数量 | 1（独占） | 1（独占） | N（计数） |
| 所有权 | 有（unlock 者必须是 lock 者） | 无（任意线程可 V） | 无 |
| 线程间通知 | 不适合 | 适合 | 适合 |
| 典型用途 | 临界区保护 | 线程同步 | 资源池管理 |

## 进阶挑战

- [ ] 解决**哲学家就餐**的另一种方法：服务员（Waiter）算法 —— 同时最多允许 N-1 个哲学家拿筷子
- [ ] 结合 **lab-proc-03-shm**：用共享内存实现**进程间信号量**，使两个进程可通过信号量同步（类比 `sem_open` + `POSIX semaphore`）
- [ ] 实现**命名信号量**：`sem_open(name, ...)` 通过名字共享信号量（类比文件路径）
- [ ] 分析 xv6 的 `sleep/wakeup` 机制与信号量的关系：xv6 的 `sleep` 中的 `chan` 参数本质上是什么？
- [ ] 用信号量解决**五个吸烟者问题（Cigarette Smokers Problem）**，分析其与生产者-消费者的异同
