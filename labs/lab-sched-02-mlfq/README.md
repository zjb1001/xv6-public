# Lab: Multi-Level Feedback Queue Scheduler
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

The problem with static priority scheduling (`lab-sched-01`) is that it requires users to manually set priorities -- but users often do not know whether a process should be CPU-intensive or I/O-intensive. MLFQ (Multi-Level Feedback Queue) **automatically adjusts priorities by observing process behavior**:

- New processes enter the **highest priority** queue by default (assuming they are interactive)
- A process that uses its entire time slice without blocking -> demoted to the next queue (behaving like a CPU-intensive process)
- A process that voluntarily blocks before its time slice ends (waiting for I/O) -> maintains or boosts its priority (behaving like an I/O-intensive process)
- **Periodic priority boost**: Prevents starvation by promoting all processes to the highest priority

MLFQ is a classic algorithm from Ousterhout's ten OS design rules and the foundational idea behind modern OS scheduling.

Core question: *"How can the scheduler make near-optimal decisions without knowing the process's future behavior?"*

## Prerequisites

- **MLFQ's 5 rules** (from OSTEP Chapter 8):
  1. Priority(A) > Priority(B) -> A runs
  2. Priority(A) = Priority(B) -> A and B alternate (Round-Robin)
  3. New processes enter the highest priority
  4. Process uses up its time slice -> demoted
  5. Periodic boost: all processes return to highest priority (prevents starvation)
- **Timer interrupts**: `src/trap.c` handles `T_IRQ0 + IRQ_TIMER`, incrementing `ticks` each interrupt; currently calls `yield()` for each tick (Round-Robin)

```
MLFQ three-level queue illustration:
Q0 (high priority, time slice=1 tick):  [new processes] [I/O processes]
Q1 (medium priority, time slice=4 ticks): [semi CPU-bound]
Q2 (low priority, time slice=infinity or 16 ticks): [pure CPU-bound processes]

Q0 has processes -> always schedule from Q0
Q0 empty -> schedule from Q1
Q1 empty -> schedule from Q2
```

## Lab Tasks

### 1. Define MLFQ parameters and data structures (include/proc.h, src/proc.c)

```c
#define NMLFQ      3             // Number of queues
#define BOOST_INTERVAL  100      // Priority boost every 100 ticks

// Time slices per queue (unit: ticks)
static int mlfq_quantum[NMLFQ] = {1, 4, 16};
```

Add to `struct proc`:

```c
int  mlfq_level;      // Current queue (0=highest, NMLFQ-1=lowest)
int  ticks_in_level;  // Time slices consumed in current queue
```

Maintain NMLFQ run queues (doubly-linked lists or scan process table by `mlfq_level`).

### 2. Modify process creation (new processes enter highest priority)

- In `allocproc()`: `p->mlfq_level = 0; p->ticks_in_level = 0`
- In `wakeup1()`: When transitioning from SLEEPING to RUNNABLE, optionally boost back to Q0 (I/O reward) or maintain current level

### 3. Modify the scheduler: select by priority queue (src/proc.c)

```c
void scheduler(void) {
    struct proc *p, *chosen;
    for(;;) {
        sti();
        acquire(&ptable.lock);
        chosen = 0;
        // From Q0 to Q(NMLFQ-1), find the first queue with a RUNNABLE process
        for(int level = 0; level < NMLFQ && !chosen; level++) {
            for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
                if(p->state == RUNNABLE && p->mlfq_level == level) {
                    chosen = p;
                    break;   // Same-level Round-Robin: can track last scheduled position
                }
            }
        }
        if(chosen) {
            // Run chosen
        }
        release(&ptable.lock);
    }
}
```

### 4. Implement demotion logic in timer interrupt handler (src/trap.c)

Timer interrupts are MLFQ's "timer". In `trap()`'s timer interrupt handling:

```c
// Check MLFQ demotion before yield()
struct proc *p = myproc();
if(p && p->state == RUNNING) {
    p->ticks_in_level++;
    if(p->ticks_in_level >= mlfq_quantum[p->mlfq_level]) {
        // Time slice exhausted, demote
        if(p->mlfq_level < NMLFQ - 1) {
            p->mlfq_level++;
        }
        p->ticks_in_level = 0;
    }
    yield();   // Yield CPU (triggers scheduler to re-select)
}

// Periodic boost
if(ticks % BOOST_INTERVAL == 0) {
    boost_all();   // Move all processes back to Q0
}
```

### 5. Implement priority boost (src/proc.c)

```c
static void boost_all(void) {
    // Must be called under ptable.lock protection
    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == RUNNABLE || p->state == SLEEPING) {
            p->mlfq_level = 0;
            p->ticks_in_level = 0;
        }
    }
}
```

**Note**: `boost_all` must be called under `ptable.lock`, and timer interrupts trigger within `trap()`, so be mindful of lock nesting.

### 6. I/O reward (optional enhancement)

When a process wakes from SLEEPING (I/O complete), boost its priority (return to Q0 or promote one level), rewarding I/O-intensive processes:

```c
// In wakeup1():
p->mlfq_level = 0;   // I/O reward: return to highest priority
p->ticks_in_level = 0;
```

### 7. Write test program (user/mlfqtest.c)

```
Test 1: I/O-intensive process (frequent sleep) vs CPU-intensive process
        Expected: I/O process stays in Q0, CPU process gradually demoted to Q2
Test 2: Boost effect: CPU-bound process in Q2 runs long enough,
        then gets boosted to Q0 and briefly gets CPU resources
Test 3: New process priority: after fork, child immediately runs in Q0,
        with shorter response time than an older process in Q2
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| MLFQ rules | The 5 OSTEP rules mapped to code implementation |
| Dynamic priority | The scheduler automatically adjusts mlfq_level based on behavior |
| Quantum | Each queue has an independent time slice length |
| Priority boost | Periodically return all processes to Q0 to prevent starvation |
| I/O reward | Processes that block for I/O get boosted on wakeup |
| CPU vs I/O intensive | MLFQ automatically distinguishes and treats the two types differently |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| include/proc.h | Modify | Add `mlfq_level`, `ticks_in_level` fields |
| src/proc.c | Modify | Modify `scheduler`, implement `boost_all`, modify `allocproc`/`wakeup1` |
| src/trap.c | Modify | Timer interrupt handling for demotion logic and Boost triggering |
| user/mlfqtest.c | New | MLFQ behavior verification test |
| Makefile | Modify | Add `mlfqtest` |

## Verification

```bash
make clean && make qemu-nox CPUS=1
$ mlfqtest
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| New process in Q0 | Newly forked process immediately runs in Q0 | mlfqtest prints level |
| CPU-bound demotion | Continuously CPU-using process goes Q0 -> Q1 -> Q2 | Observe mlfq_level changes |
| I/O process stays in Q0 | Frequently sleeping process remains in Q0 | mlfqtest verification |
| Boost takes effect | Q2 processes boosted to Q0 every 100 ticks | Add timer output for verification |
| usertests passes | Scheduling changes do not break functionality | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Deadlock during Boost | `boost_all` called in timer interrupt, but ptable.lock already held | Call boost before or after `acquire`, check lock nesting |
| All processes stuck in Q2 | No Boost after demotion, or BOOST_INTERVAL too large | Reduce BOOST_INTERVAL, confirm Boost is called |
| High I/O process response latency | I/O reward not working on wakeup | Check level reset logic in wakeup1 |
| Starvation not eliminated | Boost omits SLEEPING processes | Confirm Boost also resets level for `SLEEPING` processes |

## Key Code Paths

- New process enters queue: `proc.c:allocproc()` -> `mlfq_level = 0, ticks_in_level = 0`
- Timer demotion: `trap.c:trap()` -> `ticks_in_level++` -> exceeds quantum -> `mlfq_level++`
- Boost: `trap.c:trap()` -> `ticks % BOOST_INTERVAL == 0` -> `boost_all()`
- Scheduling: `proc.c:scheduler()` -> scan from level 0 -> find first RUNNABLE

## Design Trade-offs

| Aspect | Static Priority (lab-sched-01) | MLFQ |
|--------|-------------------------------|------|
| Priority setting | Manual (setpriority) | Automatic (based on behavior) |
| I/O-intensive response | Depends on set priority | Automatically maintains high priority |
| Starvation | Exists | Mitigated by Boost mechanism |
| Implementation complexity | Low | Medium (multiple queues + time slices + Boost) |
| Gaming (Cheating) | Not applicable | Processes can intentionally block to stay in Q0 |

## Advanced Challenges

- [ ] Add **anti-gaming**: Track total CPU time consumed in Q0; force demotion when exceeding a threshold
- [ ] Implement **Per-Level Round-Robin**: Multiple processes in the same queue alternate via Round-Robin (rather than always picking the first one)
- [ ] Parameterized configuration: Make NMLFQ, quantum sizes, and BOOST_INTERVAL runtime-adjustable from the shell
- [ ] Compare with `lab-sched-03-stride`: Measure throughput and response time under mixed CPU-bound and I/O-bound workloads
- [ ] Read [OSTEP Chapter 8](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-mlfq.pdf) and verify each rule against this lab's code
