# Lab: Read-Write Lock
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

Mutexes are overly conservative for read operations: multiple processes **reading shared data simultaneously** is perfectly safe and requires no mutual exclusion. However, a Mutex exclusively holds the lock during any access (read or write), causing poor performance in many "read-heavy, write-light" scenarios (e.g., database indexes, configuration files, shared caches).

**Read-Write Locks** distinguish between two access modes:
- **Read lock (Shared Lock)**: Multiple readers can hold simultaneously without excluding each other
- **Write lock (Exclusive Lock)**: The writer holds exclusively; all other readers and writers must wait during this time

Core semantics:
```
Read lock: Allows N readers to hold simultaneously (N >= 1)
Write lock: Allows only 1 writer at a time, and no readers
```

This lab also explores the **fairness issue** of read-write locks: a naive implementation causes **reader starvation of writers** (as long as readers keep arriving, the writer waits forever).

Core question: *"How can we allow concurrent reads while guaranteeing write exclusivity? How can we prevent writer starvation?"*

## Prerequisites

- **lab-sync-01-mutex**: This lab builds on user-mode Mutex implementation, relying on atomic operations and futex concepts
- **Classic Readers-Writers Problem**: Proposed by Dijkstra in 1971, one of the classic problems in synchronization theory, with two variants: first readers-writers problem (readers priority) and second readers-writers problem (writers priority)
- **Condition Variables**: Built on Mutex to implement "wait for a condition to become true", composed of `cv_wait` and `cv_signal`. This lab uses futex to simulate condition variables

```
Starvation scenario with naive implementation (reader priority):

Timeline: R=reader arrives, W=writer waits
R1 acquires read lock -> R2 acquires read lock -> ... -> W waits -> R3 acquires read lock -> ...
                                                        Readers keep arriving
                                                        W never acquires write lock <- Starvation!

Writer-priority solution:
When a new reader arrives, if there is a waiting writer, the reader also waits (lets writer go first)
```

## Lab Tasks

### 1. Define read-write lock data structure (lib/xv6_rwlock.h)

```c
typedef struct {
    int readers;        // Number of readers currently holding read lock
    int writers;        // Number of writers currently holding write lock (0 or 1)
    int write_waiters;  // Number of writers waiting (for writer priority)
    mutex_t lock;       // Mutex protecting the above fields
    // futex variables: reader waiting, writer waiting
    volatile int r_futex;  // Reader waiting variable
    volatile int w_futex;  // Writer waiting variable
} rwlock_t;
```

### 2. Implement reader-priority version (lib/xv6_rwlock.c)

**Reader priority**: As long as readers hold the read lock, the writer must wait. New readers are not affected by waiting writers.

```c
void rwlock_init(rwlock_t *rw) {
    rw->readers = rw->writers = rw->write_waiters = 0;
    mutex_init(&rw->lock);
    rw->r_futex = rw->w_futex = 0;
}

// Acquire read lock (reader priority)
void rwlock_rlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    // Wait for writers to finish
    while(rw->writers > 0) {
        int val = rw->r_futex;
        mutex_unlock(&rw->lock);
        futex_wait(&rw->r_futex, val);   // Sleep waiting for writer release
        mutex_lock(&rw->lock);
    }
    rw->readers++;
    mutex_unlock(&rw->lock);
}

// Release read lock
void rwlock_runlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    rw->readers--;
    if(rw->readers == 0) {
        // Last reader leaving, wake waiting writers
        rw->w_futex++;
        mutex_unlock(&rw->lock);
        futex_wake(&rw->w_futex);
        return;
    }
    mutex_unlock(&rw->lock);
}

// Acquire write lock (reader priority)
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

// Release write lock
void rwlock_wunlock(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    rw->writers = 0;
    // Wake all waiting readers and one waiting writer
    rw->r_futex++;
    rw->w_futex++;
    mutex_unlock(&rw->lock);
    futex_wake(&rw->r_futex);  // FUTEX_WAKE_ALL (wake all waiting readers)
    futex_wake(&rw->w_futex);
}
```

### 3. Implement writer-priority version (lib/xv6_rwlock.c)

**Writer priority**: If there are waiting writers, new readers must queue and wait (writers go first). This prevents writer starvation.

Key difference: When acquiring a read lock, if `write_waiters > 0`, the reader must also wait:

```c
void rwlock_rlock_wfirst(rwlock_t *rw) {
    mutex_lock(&rw->lock);
    // When writers are waiting or holding the lock, readers wait
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
    rw->write_waiters++;          // Register waiting intent, block new readers
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

### 4. Extend futex to support broadcast wake (multiple waiters)

Modify `sys_futex` to add a `FUTEX_WAKE_ALL` option (or call futex_wake multiple times), ensuring that releasing the write lock wakes **all** waiting readers:

```c
// After releasing the write lock, if all waiting readers need to be woken:
// Option 1: Modify futex_wake to add a count parameter (wake up to count waiters)
// Option 2: In wakeup(), wake all processes waiting on the same channel (xv6 default behavior)
```

### 5. Write tests (user/rwtest.c)

```
Test 1: Concurrent reads -- N readers read simultaneously, verify no mutual exclusion (all run concurrently)
Test 2: Write exclusion -- While a writer holds the lock, readers must wait
Test 3: Read excludes write -- While readers hold the lock, writers must wait
Test 4: Starvation verification -- Continuously generate new readers, observe whether the writer can acquire the write lock
         Reader-priority version: Writer may starve
         Writer-priority version: Writer eventually acquires the lock
Test 5: Data consistency -- After writer writes data, readers read the latest value
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Readers-Writers problem | Synchronization requirement allowing concurrent reads with exclusive writes |
| Shared lock / Exclusive lock | Read lock is shared, write lock is exclusive |
| Starvation | In reader-priority, writers may wait forever |
| Writer priority | Registering wait intent (write_waiters) blocks new readers |
| Conditional waiting | futex_wait implements "wait for a condition to become true" |
| Fairness | Scheduling policy handling the ordering of different request types |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| lib/xv6_rwlock.c | New | Read-write lock implementation (reader priority + writer priority) |
| lib/xv6_rwlock.h | New | `rwlock_t` definition and function declarations |
| lib/xv6_mutex.c/h | Dependency | Read-write lock is built on mutex |
| src/sysproc.c | Dependency/Modify | Extend futex to support broadcast wake |
| user/rwtest.c | New | Read-write lock test and starvation demo |

## Verification

```bash
make clean && make qemu-nox
$ rwtest
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| Concurrent reads | N readers run simultaneously (no mutual exclusion) | rwtest output time overlap |
| Write exclusivity | No other reads or writes during writer's lock hold | rwtest critical section log verification |
| Reader-priority starvation | Writer waits under continuous readers | rwtest timing observation |
| Writer-priority no starvation | Writer eventually acquires the lock | rwtest writer-priority version verification |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Writer enters while readers hold the lock | rwlock_wlock does not properly wait for readers>0 | Check that while condition includes readers>0 |
| Writer-priority still starves | write_waiters not checked in reader path | Confirm readers check `write_waiters > 0` |
| futex_wake only wakes one reader | wakeup() is designed to wake all waiters (xv6 default behavior), check usage | Verify that xv6 wakeup indeed wakes all processes waiting on the same channel |

## Key Code Paths

- Concurrent reads: `rwlock_rlock()` -> `readers++` -> read operations (concurrent) -> `rwlock_runlock()` -> `readers--`
- Write exclusivity: `rwlock_wlock()` -> wait for `readers==0 && writers==0` -> `writers=1` -> write operations -> `rwlock_wunlock()`
- Writer priority: `rwlock_wlock_wfirst()` -> `write_waiters++` -> wait -> new readers see `write_waiters>0` and also wait

## Design Trade-offs

| Aspect | Reader Priority | Writer Priority | Fair Queueing (FIFO) |
|--------|----------------|-----------------|---------------------|
| Concurrent read performance | High | Medium (readers also wait when writers waiting) | Medium |
| Writer latency | High (may starve) | Low (writer quickly acquires lock) | Low (arrival order) |
| Implementation complexity | Low | Medium | High |
| Use case | Very read-heavy, write-rare | Read-write relatively balanced | Strict fairness requirements |

## Advanced Challenges

- [ ] Implement a **fair FIFO read-write lock**: Grant access in request arrival order, eliminating starvation entirely (hint: maintain a waiting queue)
- [ ] Implement **upgradeable lock**: Atomically upgrade a reader to writer without releasing and re-acquiring (avoid race conditions)
- [ ] Analyze xv6's `bio.c` `bcache.lock`: Can it be converted to a read-write lock? Analyze the read/write patterns of `bget`
- [ ] Apply read-write locks to the LRU cache in **lab-fs-03-lrucache**, test concurrent read performance improvement
- [ ] Measure the actual read vs. write operation ratio in real workloads (running ls/cat multiple times), analyze the practical benefit of read-write locks
