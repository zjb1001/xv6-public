# Lab: Stride Scheduler
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

Both priority scheduling and MLFQ attempt to give "important processes" more CPU time, but neither can **precisely quantify** the CPU allocation ratio. Stride scheduling (proportional-share scheduling) provides a strict guarantee:

> If process A holds 2 tickets and process B holds 1 ticket, then over a long run, A receives exactly twice the CPU time of B.

The core idea of Stride scheduling:

- Each process has a **stride** = `STRIDE_BASE / tickets`; more tickets means a smaller stride
- Each scheduling round selects the process with the **smallest accumulated pass value** (pass represents "amount of service received so far")
- After running, `pass += stride`, making that process relatively less likely to be selected next time

This is a **deterministic** fair scheduling algorithm. Compared to random-based Lottery Scheduling, it has smaller error and faster convergence to the target ratio.

Core question: *"Why can Lottery Scheduling only guarantee fairness in a statistical sense, while Stride achieves deterministic fairness?"*

## Prerequisites

- **Lottery Scheduling**: Randomly selects processes proportional to ticket count; short-term variance, long-term convergence to target ratio
- **Mathematical intuition of Stride scheduling**: pass is "virtual time consumed"; always scheduling the process with the least virtual time keeps all processes' virtual times approximately equal
- **Integer overflow**: `pass` values accumulate continuously and need overflow handling (modular arithmetic or periodic reset)
- **New process joining**: A new process should set its initial pass to the **maximum** pass of all current processes, to avoid monopolizing the CPU

```
Stride scheduling illustration (STRIDE_BASE=10000):
Process A: tickets=2, stride=5000, pass=0
Process B: tickets=1, stride=10000, pass=0

Round 1: Smallest pass A(0)=B(0), pick A -> A.pass=5000
Round 2: B(0) < A(5000), pick B -> B.pass=10000
Round 3: A(5000) < B(10000), pick A -> A.pass=10000
Round 4: A(10000)=B(10000), pick A -> A.pass=15000
Round 5: B(10000) < A(15000), pick B -> B.pass=20000
...
Result: A ran 3 times, B ran 2 times (approaching 2:1)
```

## Lab Tasks

### 1. Add Stride scheduling fields (include/proc.h)

```c
#define STRIDE_BASE  1000000    // Stride base (stride when tickets=1)
#define DEFAULT_TICKETS  10     // Default ticket count

struct proc {
    // ... existing fields ...
    int  tickets;    // CPU share tickets held (1..STRIDE_BASE)
    int  stride;     // Stride = STRIDE_BASE / tickets
    int  pass;       // Accumulated service amount (pass counter for scheduling)
};
```

### 2. Initialize Stride fields (src/proc.c)

- In `allocproc()`: `p->tickets = DEFAULT_TICKETS; p->stride = STRIDE_BASE / DEFAULT_TICKETS; p->pass = 0`
- **New process pass initialization**: Set the new process's pass to the maximum pass among all RUNNABLE processes (to prevent the new process from monopolizing CPU upon joining):
  ```c
  // In fork: child pass = max(pass of all RUNNABLE processes)
  int max_pass = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->state == RUNNABLE && p->pass > max_pass)
          max_pass = p->pass;
  np->pass = max_pass;
  ```

### 3. Implement sys_settickets (src/sysproc.c)

```c
int sys_settickets(void) {
    int n;
    if(argint(0, &n) < 0) return -1;
    if(n <= 0 || n > STRIDE_BASE) return -1;
    struct proc *p = myproc();
    acquire(&ptable.lock);
    p->tickets = n;
    p->stride  = STRIDE_BASE / n;
    // Changing stride does not reset pass (preserves existing fairness)
    release(&ptable.lock);
    return 0;
}
```

### 4. Modify the scheduler: select RUNNABLE process with smallest pass (src/proc.c)

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
            chosen->pass += chosen->stride;   // Update pass before running
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

**Key detail**: `pass += stride` happens when the process is **selected** (before running), not after it finishes running. This ensures pass is consistent from the scheduler's perspective.

### 5. Handle integer overflow

`pass` values increase continuously. A 32-bit integer overflows after approximately 2147 scheduling rounds when `STRIDE_BASE=1000000` and `tickets=1`.

Solution: Periodic re-normalization (subtract the minimum pass from all passes):

```c
// At the start of the scheduler loop, if chosen->pass exceeds threshold, normalize all
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

### 6. Write tests and verify CPU ratio (user/stridetest.c)

```
Test: Fork 3 CPU-bound processes, settickets(1), settickets(2), settickets(4) respectively
      Run for a long enough time, count the actual timer ticks each process runs
      Verify the ratio is approximately 1:2:4
```

Collect statistics by reading `ticks` (via the `uptime()` system call) or by printing run time when processes exit.

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Proportional-share scheduling | Tickets determine CPU share ratio; Stride achieves precise allocation |
| Virtual time | pass is the "virtual CPU time" consumed by each process |
| Fairness | Within any time window, the ratio of pass increments across processes is proportional to their tickets |
| Lottery scheduling comparison | Random vs. deterministic; differences in convergence speed and error |
| New process starvation | New process with pass=0 monopolizes CPU; must initialize to max(pass) |
| Integer overflow handling | Practical systems must handle counter overflow edge cases |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| include/proc.h | Modify | Add `tickets`, `stride`, `pass` fields |
| src/proc.c | Modify | `allocproc`/`fork` initialization, `scheduler` changed to pass-min selection |
| src/sysproc.c | Modify | Implement `sys_settickets` |
| include/syscall.h | Modify | Add `SYS_settickets` |
| include/user.h | Modify | Add `settickets` declaration |
| user/usys.S | Modify | Add system call stub |
| user/stridetest.c | New | Proportional share verification test |
| Makefile | Modify | Add `stridetest` |

## Verification

```bash
make clean && make qemu-nox CPUS=1
$ stridetest
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| CPU ratio correct | Processes with tickets 1:2:4 have CPU time ratio approximately 1:2:4 | stridetest statistics and ratio output |
| Small error | Smaller error than lottery scheduling (within 10%) | Run multiple times and compute variance |
| New process does not monopolize | After fork, the new process does not briefly monopolize CPU | stridetest verifies other processes are not interrupted |
| usertests passes | Functionality not affected | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| One process monopolizes CPU | New process has pass=0 while other processes have large pass values | Initialize pass to max(pass) in fork |
| Large ratio deviation | pass overflow causes process order inversion | Add overflow reset logic |
| pass+=stride in wrong position | Updated after swtch, causing race conditions on multi-core | Update immediately after selection in the scheduler, not after running |

## Key Code Paths

- Initialization: `proc.c:allocproc()` -> `tickets=DEFAULT_TICKETS, stride=STRIDE_BASE/tickets, pass=0`
- Fork inheritance: `proc.c:fork()` -> `np->pass = max(pass of all RUNNABLE processes)`
- Scheduling: `proc.c:scheduler()` -> scan full table -> select RUNNABLE with smallest pass -> `pass += stride`
- Modify tickets: `sysproc.c:sys_settickets()` -> modify tickets and stride

## Design Trade-offs

| Aspect | Lottery Scheduling | Stride Scheduling |
|--------|--------------------|-------------------|
| Fairness | Statistical fairness | Deterministic precise fairness |
| Short-term error | Large (randomness) | Very small (deterministic) |
| Implementation complexity | Simple (random numbers) | Medium (pass management) |
| New process handling | Natural (randomly fair) | Needs special pass initialization |
| Integer overflow | Random numbers don't overflow | Must handle pass overflow |

## Advanced Challenges

- [ ] Implement **Lottery Scheduling**, compare CPU ratio error under the same tickets with Stride
- [ ] Implement **`sys_gettickets`**: Query the ticket count of a specified process
- [ ] Implement **Ticket Transfer**: Process A can transfer tickets to B while waiting for B to complete, boosting B's priority (solves priority inversion)
- [ ] Test Stride's multi-core behavior with CPUS=2, analyze why Stride precision decreases in multi-core environments
- [ ] Read the [Waldspurger & Weihl, OSDI 1994](https://www.usenix.org/conference/osdi/stride-scheduling-deterministic-proportional-share-resource-management) original paper, compare this implementation with the paper's design
