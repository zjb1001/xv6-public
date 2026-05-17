# Lab: User-Level Mutex
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

xv6's kernel implements spinlocks and sleep locks, but these are **kernel-mode** synchronization primitives that user programs cannot use directly.

Implementing a mutex in user mode requires two key capabilities:
1. **Atomic operations**: Test-and-set a flag bit, implemented on x86 via the `xchg` instruction, which cannot be interrupted
2. **Blocking**: When the lock is held, the process should not busy-wait (wasting CPU), but rather yield the processor and wait to be woken when the lock is released

This raises the classic **spin vs. sleep** tradeoff:
- **Spinlock**: Busy-waiting, suitable for scenarios with extremely short lock hold times (e.g., kernel interrupt handlers)
- **Sleeping Mutex**: Blocks and yields CPU, suitable for long waits in user mode

A more efficient approach is **futex (Fast Userspace muTEX)**: first attempt CAS (Compare-And-Swap) in user space; only enter the kernel to block on failure, reducing unnecessary system calls.

Core question: *"Why can't user mode implement mutual exclusion with plain read/write operations? Why can x86's xchg instruction do it?"*

## Prerequisites

- **Race condition**: Two threads simultaneously execute `if(!locked) locked=1; // critical section`; both may pass the if check and enter the critical section simultaneously
- **x86 atomic operations**: The `xchg` instruction holds the memory bus lock during execution, guaranteeing read-modify-write atomicity: `old = xchg(&lock, 1)` -- if old==0, acquisition succeeds
- **User-mode threads**: If using lab-lib-03's coroutine library, lock spinning prevents yield; true blocking requires kernel assistance
- **Kernel futex system call**: Linux's futex(FUTEX_WAIT) compares the value at a user address with an expected value and blocks if equal; futex(FUTEX_WAKE) wakes waiters

```
CAS (Compare-And-Swap) semantics:
Atomically execute: if(*addr == expected) { *addr = new; return true; }
                    else { return false; }

Spinlock implementation:
while(xchg(&lock, 1) == 1) { /* spin wait */ }
// Enter critical section
xchg(&lock, 0);  // Release lock
```

## Lab Tasks

### 1. Implement x86 atomic xchg (lib/xv6_mutex.c)

```c
// x86 xchg instruction: atomically swap *addr and newval, return old value
static inline uint xchg(volatile uint *addr, uint newval) {
    uint result;
    asm volatile("lock; xchgl %0, %1"
                 : "+m" (*addr), "=a" (result)
                 : "1" (newval)
                 : "cc");
    return result;
}
```

**Task**: Understand the meaning of each constraint symbol in the inline assembly above (`+m`, `=a`, `"1"`, `cc`) and explain them in comments.

### 2. Implement spin mutex (lib/xv6_mutex.c)

```c
typedef struct {
    volatile uint locked;  // 0=unlocked, 1=locked
} spinmutex_t;

void spinmutex_init(spinmutex_t *m) { m->locked = 0; }

void spinmutex_lock(spinmutex_t *m) {
    while(xchg(&m->locked, 1) != 0)
        ;  // Spin
}

void spinmutex_unlock(spinmutex_t *m) {
    xchg(&m->locked, 0);
}
```

### 3. Add kernel futex system call (src/sysproc.c)

futex allows user programs to **only enter the kernel when there is lock contention**:

```c
// futex_wait(addr, val): If *addr == val then block
// futex_wake(addr):      Wake one process waiting on addr
#define FUTEX_WAIT  0
#define FUTEX_WAKE  1

int sys_futex(void) {
    int *uaddr, op, val;
    if(argptr(0, (char**)&uaddr, sizeof(int)) < 0
       || argint(1, &op) < 0 || argint(2, &val) < 0) return -1;

    if(op == FUTEX_WAIT) {
        // Check if *uaddr still equals val (check while holding lock)
        acquire(&ptable.lock);  // Or a dedicated futex lock
        if(*uaddr != val) {
            release(&ptable.lock);
            return 0;  // Value changed, no need to wait
        }
        // Block current process
        sleep(uaddr, &ptable.lock);
        release(&ptable.lock);
        return 0;
    } else if(op == FUTEX_WAKE) {
        // Wake all processes waiting on uaddr
        wakeup(uaddr);
        return 0;
    }
    return -1;
}
```

### 4. Implement futex-based sleeping mutex (lib/xv6_mutex.c)

```c
typedef struct {
    volatile int state;  // 0=unlocked, 1=locked no waiters, 2=locked with waiters
} mutex_t;

void mutex_init(mutex_t *m) { m->state = 0; }

void mutex_lock(mutex_t *m) {
    // Fast path: CAS(0 -> 1)
    int c;
    if((c = __sync_val_compare_and_swap(&m->state, 0, 1)) != 0) {
        // Slow path: contention, enter kernel to wait
        do {
            if(c == 2 || __sync_val_compare_and_swap(&m->state, 1, 2) != 0)
                futex_wait(&m->state, 2);
        } while((c = __sync_val_compare_and_swap(&m->state, 0, 2)) != 0);
    }
}

void mutex_unlock(mutex_t *m) {
    if(__sync_fetch_and_sub(&m->state, 1) != 1) {
        // Has waiters (state was 2), need to wake
        m->state = 0;
        futex_wake(&m->state);
    }
}
```

### 5. Implement header file (lib/xv6_mutex.h)

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

### 6. Write tests (user/mutextest.c)

```
Test 1: Spinlock basic correctness -- two processes increment shared counter, verify no race
Test 2: Sleeping mutex basic correctness -- same as above, verify result correctness
Test 3: Performance comparison -- CPU usage of spinlock vs sleeping mutex under high contention
Test 4: Deadlock detection -- calling mutex_lock twice from the same process causes deadlock (demo)
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Race condition | Multiple processes simultaneously modifying shared variables causing indeterminate results |
| Atomic operations | x86 `xchg`/`lock cmpxchg` guarantee read-modify-write atomicity |
| Spinlock | Busy-waiting, suitable for short critical sections, wastes CPU |
| Sleeping mutex | Enters kernel to block on contention, saves CPU |
| futex | Pure user mode when uncontended, enters kernel only on contention |
| Critical section | Code segments that must be accessed under lock protection |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| lib/xv6_mutex.c | New | spinmutex and mutex implementation |
| lib/xv6_mutex.h | New | Type definitions and function declarations |
| src/sysproc.c | Modify | Implement `sys_futex` |
| include/syscall.h | Modify | Add `SYS_futex` |
| include/user.h | Modify | Add `futex_wait`/`futex_wake` user declarations |
| user/mutextest.c | New | Mutex test |

## Verification

```bash
make clean && make qemu-nox
$ mutextest
$ usertests
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| No race | 100 concurrent additions produce correct result (expected N*100) | mutextest verification |
| futex reduces system calls | futex_wait not called when uncontended | Add kernel counter verification |
| usertests passes | Existing functionality not affected | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Incorrect count result | Lock implementation has bug, critical section unprotected | Debug xchg return value with GDB breakpoints |
| Process hangs | Not woken after futex_wait | Check if wakeup's first argument is the same as sleep's |
| xchg compilation error | Inline assembly constraints are wrong | Refer to the xchg implementation in xv6 spinlock.c |

## Key Code Paths

- Atomic lock acquisition: `xchg(&m->locked, 1)` -> if returns 0 then success, otherwise spin or enter futex_wait
- futex block: `sys_futex(FUTEX_WAIT)` -> verify *uaddr==val -> `sleep(uaddr, lock)`
- futex wake: `mutex_unlock()` -> `futex_wake()` -> `sys_futex(FUTEX_WAKE)` -> `wakeup(uaddr)`

## Design Trade-offs

| Aspect | Spinlock | Sleeping Mutex | futex |
|--------|----------|---------------|-------|
| Uncontended overhead | Very low (1 atomic instruction) | Low (1 atomic instruction) | Very low (pure user mode) |
| Contended overhead | High (CPU busy-waiting) | Low (process sleeps) | Low (enters kernel only on contention) |
| Use case | Kernel short critical sections | General user-mode scenarios | High-performance user-mode synchronization |
| Deadlock risk | Yes (same-thread re-entry) | Yes | Yes |

## Advanced Challenges

- [ ] Implement a **Recursive Mutex**: Record owner tid, same thread can lock multiple times
- [ ] Implement **timed wait**: `mutex_timedlock(m, timeout_ms)` -- add timeout parameter to futex_wait
- [ ] Implement **deadlock detection**: Maintain a lock ownership graph, detect cycles (wait-for graph algorithm)
- [ ] Combine **lab-sync-03-semaphore** and **lab-proc-03-shm**: Implement cross-process mutex using shared memory semaphores
- [ ] Comparative analysis: What patterns emerge in the performance differences between spinlocks and sleeping mutexes on single CPU vs. multi CPU (`make CPUS=4 qemu-nox`)?
