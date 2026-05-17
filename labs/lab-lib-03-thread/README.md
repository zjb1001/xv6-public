# Lab: User-Level Thread Library

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6 creates processes through `fork`, which copies the entire address space each time and switches via the kernel scheduler — meaning every context switch must go through the overhead of entering the kernel, saving/restoring the trapframe, and returning to user mode.

This lab implements lightweight threads (coroutines) in **user space**, completely bypassing kernel involvement:

- Each thread has its own independent **stack** (allocated with `malloc`)
- Thread switching is done via `setjmp`/`longjmp` in user mode, triggering no system calls
- The scheduling policy is **cooperative Round-Robin**: threads must actively call `thread_yield` to give up the CPU

Core question: *"How do you switch to executing another function without going through the kernel?"*

## Prerequisites

- **Call stack structure**: Each function call allocates a frame on the stack (local variables, saved registers, return address); the frame is popped when the function returns
- **`setjmp`/`longjmp`**: `setjmp(jb)` saves current registers (including `%esp`/`%eip`) into `jmp_buf`; `longjmp(jb, 1)` restores these registers, "jumping back" to the setjmp call site
- **Cooperative vs. preemptive**: In cooperative multitasking, threads are not forcibly interrupted; switching only occurs when a thread voluntarily yields. Preemptive relies on timer interrupts

```
Stack layout of user-level threads:
Main stack (default stack allocated by sbrk)
  ┌──────────────┐
  │  thread lib  │  ← scheduler runs here
  └──────────────┘

Thread 1's stack (malloc'd)        Thread 2's stack (malloc'd)
  ┌──────────────┐                  ┌──────────────┐
  │  func_a frame │                 │  func_b frame │
  │  local vars   │                 │  local vars   │
  └──────────────┘                  └──────────────┘
  jmp_buf saves %esp pointing here   jmp_buf saves %esp pointing here
```

## Lab Tasks

### 1. Define the Thread Control Block (lib/xv6_thread.h)

```c
#define THREAD_STACK_SIZE  4096
#define MAX_THREADS        8

typedef enum { THREAD_FREE, THREAD_RUNNABLE, THREAD_RUNNING, THREAD_DONE } thread_state_t;

typedef struct thread {
    int            id;
    thread_state_t state;
    jmp_buf        ctx;          // Thread context (register snapshot)
    char          *stack;        // Thread-private stack (malloc'd)
    void         (*func)(void);  // Thread entry function
} thread_t;
```

**Key constraints**:
- Thread 0 is the "main thread", which uses the process default stack and does not need malloc
- `ctx` is only valid when a thread has been yielded; first-time thread startup requires special handling (see step 3)

### 2. Implement Thread Creation (lib/xv6_thread.c)

```c
int thread_create(void (*func)(void))
```

- Allocate a free slot from the thread table
- Allocate stack space with `malloc(THREAD_STACK_SIZE)`
- **Initialize the stack frame**: Push `thread_entry` (see step 3) and the `func` pointer at the top of the stack, so that the first `longjmp` to this thread jumps into the entry function
- Set `state = THREAD_RUNNABLE`

**Key technique — Manually constructing the initial stack**:

When an x86 function call enters, the top of the stack is the return address, and below that are the arguments. Therefore, forge a "call frame" at the top of the new stack:

```c
char *sp = stack + THREAD_STACK_SIZE;  // Stack grows from high to low addresses
sp -= sizeof(void*);
*(void**)sp = func;                    // "argument 1": thread function
sp -= sizeof(void*);
*(void**)sp = thread_exit;             // "return address": call exit when thread finishes
// Then set ctx.esp to sp (via setjmp magic or direct assignment)
```

### 3. Implement the Cooperative Scheduler (lib/xv6_thread.c)

```c
static void scheduler(void)
```

- Maintain the `current_thread` global variable
- Find the next `THREAD_RUNNABLE` thread in Round-Robin order from the thread table
- If found: `longjmp(next->ctx, 1)` to switch to the target thread
- If no RUNNABLE thread: all threads have finished, `exit(0)` to terminate the process

```c
void thread_yield(void)
```

- Set the current thread to RUNNABLE
- `if(setjmp(current->ctx) == 0)` -> first call, call `scheduler()`
- `if(setjmp(current->ctx) != 0)` -> returned from longjmp, resume execution

```c
static void thread_exit(void)
```

- Set current thread `state = THREAD_DONE`, free the stack
- Call `scheduler()` to switch to another thread (never returns)

### 4. Implement `thread_run` Entry (lib/xv6_thread.c)

```c
void thread_run(void)
```

- Initialize the main thread (id=0, using the current stack)
- Loop calling `scheduler()` until all threads finish

### 5. Write Test Program (user/threadtest.c)

```c
void task_a(void) {
    for(int i = 0; i < 3; i++) {
        printf("A %d\n", i);
        thread_yield();
    }
}
void task_b(void) {
    for(int i = 0; i < 3; i++) {
        printf("B %d\n", i);
        thread_yield();
    }
}
int main(void) {
    thread_create(task_a);
    thread_create(task_b);
    thread_run();
}
// Expected output: A 0 / B 0 / A 1 / B 1 / A 2 / B 2
```

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| Context Switch | `setjmp`/`longjmp` save/restore registers, equivalent to kernel `swtch.S` |
| Thread Control Block (TCB) | `thread_t` structure: state, stack, context |
| Cooperative scheduling | Threads must voluntarily yield; analogous to early Windows 3.x message loop |
| Stack allocation | User-mode malloc allocates thread stacks; contrast with kernel allocating kernel stacks per process |
| Calling convention | Manually construct x86 initial stack frames; understand `%esp`/`%eip`/return address layout |
| M:1 model | All user threads share a single kernel process/scheduling entity |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| lib/xv6_thread.h | New | TCB definition, thread API declarations |
| lib/xv6_thread.c | New | Complete thread library implementation |
| user/threadtest.c | New | Verify cooperative scheduling order |
| Makefile | Modify | Add `threadtest` and `xv6_thread.c` to build |

## Verification

### Build and Run

```bash
make clean && make qemu-nox
$ threadtest
```

### Verification Goals

| Goal | Expected Behavior | How to Observe |
|------|------------------|----------------|
| Alternating execution | A 0 / B 0 / A 1 / B 1 ... strict alternation | Output line order |
| Clean exit | Process exits after all threads are DONE, no hanging | Shell returns `$` |
| Independent stacks | Local variables of each thread do not interfere | Declare large arrays in task_a/b to verify |
| Yield semantics | A thread that does not call yield monopolizes the CPU | Remove yield from test, observe serial output |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Crash immediately after jump | Initial stack frame constructed incorrectly, `%esp` not aligned | x86 requires 16-byte stack alignment; check sp alignment |
| Crash after thread function returns | "Return address" is not `thread_exit` | Check the return address written at the initial stack top |
| Scheduler loops infinitely | Thread not correctly marked as DONE | Confirm `thread_exit` sets `state = THREAD_DONE` |
| setjmp return value incorrect | longjmp second argument of 0 is changed to 1 (C standard behavior) | Normal behavior; use `!= 0` to detect longjmp return |

## Key Code Paths

- Thread creation: `thread_create()` -> malloc stack -> construct initial stack frame
- First switch: `thread_run()` -> `scheduler()` -> `longjmp(t->ctx)` -> jump into `thread_entry`
- Yield CPU: `thread_yield()` -> `setjmp(save)` -> `scheduler()` -> `longjmp(next->ctx)`
- Thread exit: `thread_exit()` -> `state=DONE` -> `scheduler()` (never returns)

## Design Trade-offs

| Aspect | User-level threads (this lab) | Kernel threads (xv6 processes) |
|--------|-------------------------------|-------------------------------|
| Switching overhead | Very low (pure user-mode setjmp/longjmp) | Higher (enter kernel, save trapframe) |
| Parallelism | None (M:1, shared single core) | Yes (1:1, can run in parallel on multiple cores) |
| Blocking impact | One thread blocks (read), entire process stops | Only affects the current kernel thread |
| Preemption | None (cooperative) | Yes (timer interrupt) |
| Implementation complexity | Low (user mode) | High (requires kernel support) |

## Advanced Challenges

- [ ] Implement **preemptive** user threads: use `SIGALRM` (once signals are implemented) to periodically trigger yield
- [ ] Implement thread-local storage (TLS): each thread's `errno` variable is independent
- [ ] Implement simple user-mode mutexes (in conjunction with lab-sync-01)
- [ ] Change the scheduling policy from Round-Robin to **priority scheduling**
- [ ] Track yield count and execution time per thread; output scheduling statistics
