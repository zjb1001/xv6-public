# Lab: Semaphore
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

A mutex is a special case of a "binary semaphore": the value can only be 0 (locked) or 1 (unlocked). **Counting semaphores** generalize this concept: they allow N threads to simultaneously enter the critical section.

Semaphores were proposed by Dijkstra in 1965 and are one of the most classic primitives in operating system synchronization theory. Their core operations:

- **P (Proberen, wait/down)**: Decrement the semaphore value by 1; if the value becomes negative, block (suspend and wait)
- **V (Verhogen, signal/up)**: Increment the semaphore value by 1; if there are waiters, wake one

The power of semaphores lies in their ability to elegantly solve the **producer-consumer** problem:

```
Producer-Consumer (ring buffer, capacity N):
empty = Semaphore(N)   // Number of empty slots
full  = Semaphore(0)   // Number of filled slots
mutex = Semaphore(1)   // Mutual exclusion for buffer access

Producer:                          Consumer:
P(empty)  // Wait for empty slot    P(full)   // Wait for data
P(mutex)  // Enter critical section P(mutex)  // Enter critical section
  Write data to buffer               Read data from buffer
V(mutex)  // Leave critical section V(mutex)  // Leave critical section
V(full)   // Notify consumer         V(empty)  // Notify producer
```

Core question: *"What is the essential difference between a semaphore and a mutex? Why can't a mutex directly implement the producer-consumer pattern?"*

## Prerequisites

- **lab-sync-01-mutex**: Semaphores are built on Mutex + futex, relying on atomic operation concepts
- **Classic synchronization problems**:
  - Producer-consumer (bounded buffer)
  - Dining philosophers (resource allocation, deadlock prevention)
  - Readers-writers (see lab-sync-02)
- **Binary semaphore vs. mutex differences**:
  - A mutex must be locked and unlocked by the **same thread** (ownership)
  - A binary semaphore can have P and V performed by **different threads** (suitable for inter-thread synchronization/notification)

```
Semaphore sem(initial value=3) allows 3 resources simultaneously:

sem=3: T1 P -> sem=2: T2 P -> sem=1: T3 P -> sem=0
                                            T4 P -> blocks (sem=-1)
T1 V -> sem=0 -> wakes T4 -> T4 enters
```

## Lab Tasks

### 1. Define semaphore data structure (lib/xv6_semaphore.h)

```c
#ifndef XV6_SEMAPHORE_H
#define XV6_SEMAPHORE_H

#include "xv6_mutex.h"

typedef struct {
    volatile int value;     // Current semaphore value
    volatile int futex_v;   // futex wait variable
    mutex_t      mutex;     // Mutex protecting value
} sem_t;

void sem_init(sem_t *s, int value);
void sem_wait(sem_t *s);    // P operation / down
void sem_post(sem_t *s);    // V operation / up

#endif
```

### 2. Implement semaphore (lib/xv6_semaphore.c)

```c
void sem_init(sem_t *s, int value) {
    s->value = value;
    s->futex_v = 0;
    mutex_init(&s->mutex);
}

// P operation: wait for semaphore to be available
void sem_wait(sem_t *s) {
    mutex_lock(&s->mutex);
    s->value--;
    if(s->value < 0) {
        // Need to block: save current futex_v value, unlock, then wait
        int expected = s->futex_v;
        mutex_unlock(&s->mutex);
        futex_wait(&s->futex_v, expected);
        return;
    }
    mutex_unlock(&s->mutex);
}

// V operation: release semaphore and wake waiters
void sem_post(sem_t *s) {
    mutex_lock(&s->mutex);
    s->value++;
    if(s->value <= 0) {
        // Has waiters (value was negative before), wake one
        s->futex_v++;
        mutex_unlock(&s->mutex);
        futex_wake(&s->futex_v);
        return;
    }
    mutex_unlock(&s->mutex);
}
```

**Thought exercise**: After `sem_wait` returns from `futex_wait`, does it need to re-check `value < 0`? (Hint: spurious wakeup)

### 3. Implement producer-consumer (user/semtest.c part 1)

```c
#define BUFSIZE 8

int buffer[BUFSIZE];
int head = 0, tail = 0;
sem_t empty, full, mutex_sem;  // Using semaphore for mutual exclusion

void producer(int n) {
    for(int i = 0; i < n; i++) {
        sem_wait(&empty);            // Wait for empty slot
        sem_wait(&mutex_sem);        // Enter critical section
        buffer[tail] = i;
        tail = (tail + 1) % BUFSIZE;
        sem_post(&mutex_sem);        // Leave critical section
        sem_post(&full);             // Notify consumer
    }
}

void consumer(int n) {
    for(int i = 0; i < n; i++) {
        sem_wait(&full);             // Wait for data
        sem_wait(&mutex_sem);        // Enter critical section
        int item = buffer[head];
        head = (head + 1) % BUFSIZE;
        sem_post(&mutex_sem);        // Leave critical section
        sem_post(&empty);            // Notify producer
        printf(1, "consumed: %d\n", item);
    }
}

// Initialization: empty=BUFSIZE, full=0, mutex_sem=1
sem_init(&empty, BUFSIZE);
sem_init(&full, 0);
sem_init(&mutex_sem, 1);
```

### 4. Implement dining philosophers (user/semtest.c part 2)

5 philosophers, 5 chopsticks (each needs left and right):

```c
sem_t chopstick[5];  // Each chopstick initialized to 1

void philosopher(int id) {
    int left  = id;
    int right = (id + 1) % 5;
    // Deadlock prevention: last philosopher picks right first, then left
    if(id == 4) { sem_wait(&chopstick[right]); sem_wait(&chopstick[left]); }
    else        { sem_wait(&chopstick[left]);  sem_wait(&chopstick[right]); }
    printf(1, "philosopher %d eating\n", id);
    // Simulate eating
    sem_post(&chopstick[left]);
    sem_post(&chopstick[right]);
}
```

**Thought exercise**: Why does simply "pick left first, then right" cause deadlock? Does your solution completely eliminate deadlock?

### 5. Implement sem_trywait with timeout (optional)

```c
// Non-blocking attempt: if semaphore > 0, acquire immediately; otherwise return -1
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

### 6. Test summary (user/semtest.c)

```
Test 1: Binary semaphore behavior (equivalent to mutex, but supports cross-process V)
Test 2: Producer-consumer correctness (no data loss, no duplicates)
Test 3: Dining philosophers (no deadlock, all philosophers get to eat)
Test 4: sem_trywait non-blocking verification
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Counting semaphore | value represents available resources; can be negative (negative = number of waiters) |
| P/V operations | sem_wait / sem_post; atomicity guaranteed by mutex |
| Producer-consumer | Three semaphores: empty, full, mutex coordinate read/write |
| Dining philosophers | Resource allocation deadlock prevention (breaking circular wait condition) |
| Deadlock prevention | Breaking "circular wait": last philosopher reverses chopstick pickup order |
| Binary semaphore vs. mutex | Semaphore has no ownership; P and V can be performed across threads/processes |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| lib/xv6_semaphore.c | New | Semaphore implementation |
| lib/xv6_semaphore.h | New | sem_t definition and function declarations |
| lib/xv6_mutex.c/h | Dependency | Semaphore is built on mutex + futex |
| user/semtest.c | New | Producer-consumer + dining philosophers test |

## Verification

```bash
make clean && make qemu-nox
$ semtest
$ usertests
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| Producer-consumer correctness | Consumer receives all data produced (correct order) | semtest verifies sum |
| No deadlock | Philosophers all eventually finish eating | semtest does not hang |
| sem_trywait non-blocking | Returns -1 immediately when value is 0 | semtest verification |
| usertests passes | Existing functionality not affected | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Producer data lost | mutex_sem not properly protecting buffer | Confirm sem_wait(&mutex_sem) is called before all buffer accesses |
| Philosophers deadlock | All philosophers pick up left chopstick simultaneously | Implement resource ordering or reverse last philosopher's pickup order |
| Enters critical section after futex_wait spurious wakeup | sem_wait does not re-check value | Re-check condition after futex_wait returns (loop wait) |

## Key Code Paths

- P operation: `sem_wait()` -> `value--` -> if `<0` then `futex_wait` to block
- V operation: `sem_post()` -> `value++` -> if `<=0` then `futex_wake` to wake one waiter
- Producer: `P(empty)` -> `P(mutex)` -> write data -> `V(mutex)` -> `V(full)`
- Consumer: `P(full)` -> `P(mutex)` -> read data -> `V(mutex)` -> `V(empty)`

## Design Trade-offs

| Aspect | Mutex | Binary Semaphore | Counting Semaphore |
|--------|-------|-----------------|-------------------|
| Resource count | 1 (exclusive) | 1 (exclusive) | N (counted) |
| Ownership | Yes (unlocker must be locker) | No (any thread can V) | No |
| Inter-thread notification | Not suitable | Suitable | Suitable |
| Typical use | Critical section protection | Thread synchronization | Resource pool management |

## Advanced Challenges

- [ ] Solve the **dining philosophers** with another approach: Waiter algorithm -- allow at most N-1 philosophers to pick up chopsticks simultaneously
- [ ] Combine with **lab-proc-03-shm**: Implement **inter-process semaphores** using shared memory so two processes can synchronize via semaphores (analogous to `sem_open` + POSIX semaphores)
- [ ] Implement **named semaphores**: `sem_open(name, ...)` shares semaphores by name (analogous to file paths)
- [ ] Analyze the relationship between xv6's `sleep/wakeup` mechanism and semaphores: What is xv6's `sleep` `chan` parameter essentially?
- [ ] Solve the **Cigarette Smokers Problem** using semaphores, analyze its similarities and differences with the producer-consumer problem
