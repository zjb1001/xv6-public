# Lab: Priority Scheduler
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

xv6's Round-Robin scheduler treats all processes equally -- shell, background tasks, and audio players share the same time-slice granularity. Meanwhile, `lab-fifo-sched` implements strict FIFO, guaranteeing order but completely sacrificing responsiveness.

Real operating systems (Linux, macOS, Windows) all use **priority scheduling**:

- Each process has a priority value; the scheduler always selects the RUNNABLE process with the **highest priority**
- High-priority processes (e.g., interactive shell) have low response latency
- Low-priority processes (e.g., background compilation) do not affect foreground responsiveness

This lab implements static priority scheduling in xv6, along with `setpriority`/`getpriority` system calls.

Core question: *"Priority scheduling improves response times, but what new problems does it introduce?"*

## Prerequisites

- **xv6 scheduler**: `src/proc.c:scheduler()` scans `ptable.proc[]` in a loop, finding RUNNABLE processes to switch to. Currently it switches to the first RUNNABLE found (Round-Robin relies on timer interrupts, not strict cycling)
- **`ptable.lock`**: The spinlock protecting the process table (`ptable`); the scheduler selects the next process while holding this lock
- **Priority Inversion**: A low-priority process holds a lock needed by a high-priority process, causing the high-priority process to be indirectly blocked
- **Starvation**: Low-priority processes never get CPU because high-priority processes remain RUNNABLE

```
Priority scheduling example:
Process A (priority=0, highest): Interactive shell
Process B (priority=5):          Normal program
Process C (priority=10, lowest): Background task

When A is RUNNABLE: Always schedule A, regardless of B/C state
When A is SLEEPING: Choose B (lowest priority value = highest priority) among B/C
```

## Lab Tasks

### 1. Add a priority field to the process control block (include/proc.h)

```c
struct proc {
    // ... existing fields ...
    int priority;    // Scheduling priority: 0=highest, 19=lowest (analogous to Unix nice value)
};
```

**Convention**: A smaller `priority` value means higher priority (analogous to Linux `nice` values, with 0 as default).

### 2. Initialize priority (src/proc.c)

- In `allocproc()`: set `p->priority = 0` (highest by default, fair start)
- In `fork()`: child inherits parent's priority: `np->priority = curproc->priority`

### 3. Implement sys_setpriority / sys_getpriority (src/sysproc.c)

```c
int sys_setpriority(void) {
    int pid, priority;
    // Read arguments: pid (0 = current process), priority (0..19)
    // Permission check (optional): only allow lowering own priority (nice semantics)
    // Find target process, modify priority
}

int sys_getpriority(void) {
    int pid;
    // Return the target process's priority
}
```

Register in `src/syscall.c` and `include/syscall.h`.

### 4. Modify the scheduler: select the highest-priority RUNNABLE process (src/proc.c)

Change the `scheduler()` process selection logic from "find the first RUNNABLE" to "find the RUNNABLE with the smallest priority value":

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

**Note**: After this modification, the scheduler time complexity is O(NPROC), scanning the entire table each scheduling round.

### 5. Demonstrate priority inversion (thought exercise)

Design a scenario that demonstrates priority inversion:

```
Process L (priority=10) holds spinlock S
Process H (priority=0)  waits for spinlock S
Process M (priority=5)  CPU-bound

Actual execution order: M runs before L, because H is blocked and L never gets scheduled
Problem: H is effectively blocked by M (lower priority), not directly by L
```

### 6. Write a test program (user/priotest.c)

```
Test 1: Fork 3 processes, setpriority to 0/5/10 respectively, verify completion order matches priority
Test 2: High-priority process is CPU-bound -- can low-priority processes get CPU? (starvation demo)
Test 3: Inheritance verification -- child process inherits parent's priority
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Static priority | The priority field is fixed unless setpriority is explicitly called |
| Scheduling policy | The scheduler selects the RUNNABLE process with the smallest priority value |
| Starvation | Low-priority processes cannot execute when high-priority processes remain RUNNABLE |
| Priority inheritance | Child inherits parent's priority on fork |
| Priority inversion | Low-priority holds lock, high-priority waits for lock, mid-priority steals CPU |
| nice value | The user-adjustable priority interface in Unix/Linux, the design prototype for this lab |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| include/proc.h | Modify | Add `priority` field |
| src/proc.c | Modify | `allocproc`/`fork` initialize priority; `scheduler` modification |
| src/sysproc.c | Modify | Add `sys_setpriority`/`sys_getpriority` |
| include/syscall.h | Modify | Add system call numbers |
| src/syscall.c | Modify | Register system calls |
| include/user.h | Modify | Add user-space declarations |
| user/usys.S | Modify | Add system call assembly stubs |
| user/priotest.c | New | Priority scheduling verification test |

## Verification

```bash
make clean && make qemu-nox CPUS=1
$ priotest
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| High priority finishes first | The priority=0 process ends before priority=10 | priotest output completion order |
| Low priority starves | With a high CPU-bound process, low-priority processes cannot run | priotest timing statistics |
| setpriority takes effect | Runtime priority change immediately affects scheduling | priotest dynamic modification test |
| Multi-core safety | No crashes with CPUS=2, priority still works | Run usertests |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| All processes with same priority are unordered | No tiebreaker for equal priorities | Add Round-Robin as a secondary strategy |
| setpriority has no effect on other processes | PID lookup is incorrect before locking | Lookup and modify under ptable.lock protection |
| Kernel panic in scheduler | Selected process is no longer RUNNABLE | Re-check state after acquiring lock |

## Key Code Paths

- Initialization: `proc.c:allocproc()` -> `p->priority = 0`
- Inheritance: `proc.c:fork()` -> `np->priority = curproc->priority`
- Scheduling: `proc.c:scheduler()` -> scan full table -> select RUNNABLE with smallest `priority`
- Modify priority: `sysproc.c:sys_setpriority()` -> find process -> modify `p->priority`

## Design Trade-offs

| Aspect | Round-Robin (original) | Priority Scheduling |
|--------|----------------------|---------------------|
| Fairness | Equal time slices for all processes | Low priority may starve |
| Response time | Bounded (one time slice) | Very low latency for high priority |
| Implementation complexity | O(1) rotation | O(NPROC) scan |
| Starvation | None | Yes (low-priority processes) |
| Use case | Fair batch processing | Mixed interactive + background tasks |

## Advanced Challenges

- [ ] Implement **Aging**: Low-priority processes automatically increase priority every N timer ticks to prevent starvation
- [ ] Implement **Priority Ceiling Protocol**: Temporarily boost to the highest-priority user of a lock while holding it
- [ ] Convert the scheduler to a **priority queue** (20 linked lists indexed by priority) to avoid O(NPROC) scanning
- [ ] Implement **multi-core load balancing** with CPUS=2: idle CPUs steal high-priority processes from other CPUs
- [ ] Compare with `lab-sched-02-mlfq`: discuss the respective use cases for static vs. dynamic priority
