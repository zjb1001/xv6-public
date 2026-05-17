# Lab: Signal Mechanism
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6's `kill(pid)` system call can only send one type of "signal" to a process -- forced termination. It sets `p->killed = 1`, and the process checks and exits on its next return from the kernel. This mechanism is extremely simplistic: no signal numbers, no user-defined handler functions, no signal masking.

The Unix signal mechanism (POSIX signals) is the standard method for asynchronous notification between processes:

- Processes can register **signal handlers** that execute in user mode when a signal is received
- When the kernel "delivers" a signal to a process, it modifies the process's return path so it first runs the signal handler, then resumes original execution
- Typical uses: `SIGINT` (Ctrl+C termination), `SIGCHLD` (child process exit notification), `SIGUSR1/2` (user-defined)

Core question: *"The signal handler is clearly in user code -- how does the kernel make the process 'suddenly jump into executing it'?"*

## Prerequisites

- **Trapframe**: The user-mode register snapshot saved in `src/trapasm.S`, defined as `struct trapframe` in `include/x86.h`. The kernel modifies `%eip` in the trapframe to change where the user process "returns" to
- **Challenges of user-mode signal handling**: The signal handler runs in user mode; after completion, the full state (including all registers) must be restored. This requires the kernel to save the complete trapframe on the user stack and restore it via a `sigreturn` system call when the handler returns
- **Signal masking**: Processes can temporarily block certain signals (deferred delivery); during signal handling, the same signal type is typically auto-blocked

```
Signal delivery execution path:
Normal: User code -> System call/interrupt -> Kernel -> Return to user code

Signal delivery: User code -> System call/interrupt -> Kernel
  | Before returning, check pending signals
  | Modify user stack and trapframe
  User mode executes signal_handler()
  | signal_handler returns -> calls sigreturn()
  | Kernel restores original trapframe
  Resume original user code execution
```

## Lab Tasks

### 1. Define signal numbers and data structures (include/proc.h)

```c
#define NSIG     16         // Maximum 16 signal types

// Standard signal numbers (subset)
#define SIGHUP   1          // Hangup
#define SIGINT   2          // Interrupt (Ctrl+C)
#define SIGKILL  9          // Force kill (cannot be caught)
#define SIGSEGV  11         // Segmentation fault
#define SIGALRM  14         // Timer signal
#define SIGTERM  15         // Termination request

typedef void (*sighandler_t)(int);
#define SIG_DFL  ((sighandler_t)0)   // Default handling (terminate process)
#define SIG_IGN  ((sighandler_t)1)   // Ignore signal

struct proc {
    // ... existing fields ...
    sighandler_t  sig_handlers[NSIG];  // Handler function per signal
    uint          sig_pending;         // Pending signals bitmap
    uint          sig_mask;            // Blocked signals bitmap
};
```

### 2. Implement sys_signal (register signal handler) (src/sysproc.c)

```c
// sys_signal(signo, handler) -> returns old handler
sighandler_t sys_signal(void) {
    int signo;
    sighandler_t handler;
    if(argint(0, &signo) < 0 || argptr(1, (char**)&handler, sizeof(handler)) < 0)
        return SIG_ERR;
    if(signo <= 0 || signo >= NSIG) return SIG_ERR;
    if(signo == SIGKILL) return SIG_ERR;  // SIGKILL cannot be caught
    struct proc *p = myproc();
    sighandler_t old = p->sig_handlers[signo];
    p->sig_handlers[signo] = handler;
    return old;
}
```

### 3. Extend sys_kill: support signal numbers (src/sysproc.c)

Expand `sys_kill(pid, signo)` from "terminate only" to "send any signal":

```c
int sys_kill(void) {
    int pid, signo;
    if(argint(0, &pid) < 0 || argint(1, &signo) < 0) return -1;
    return kill(pid, signo);
}

int kill(int pid, int signo) {
    struct proc *p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->pid == pid) {
            p->sig_pending |= (1 << signo);   // Set pending bit
            if(p->state == SLEEPING)
                p->state = RUNNABLE;           // Wake up sleeping process
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}
```

### 4. Deliver signals before kernel returns to user mode (src/trap.c)

At the end of the `trap()` function (before user process returns from system call/interrupt), check and deliver pending signals:

```c
// Before trap() returns:
if(myproc() && myproc()->state == RUNNING)
    deliver_pending_signals(myproc(), tf);
```

```c
// src/proc.c or src/trap.c:
void deliver_pending_signals(struct proc *p, struct trapframe *tf) {
    for(int sig = 1; sig < NSIG; sig++) {
        if(!(p->sig_pending & (1 << sig))) continue;
        if(p->sig_mask & (1 << sig)) continue;     // Blocked, skip
        p->sig_pending &= ~(1 << sig);             // Clear pending bit

        sighandler_t handler = p->sig_handlers[sig];
        if(handler == SIG_IGN) continue;
        if(handler == SIG_DFL) {
            // Default handling: terminate process (like original kill behavior)
            p->killed = 1;
            return;
        }
        // User-defined handler: modify trapframe to "return" to handler
        setup_signal_frame(p, tf, sig, handler);
        return;
    }
}
```

### 5. Implement signal frame setup and restoration (src/proc.c)

`setup_signal_frame` saves the current trapframe on the user stack, then makes the process "return" to the signal handler:

```c
void setup_signal_frame(struct proc *p, struct trapframe *tf,
                         int sig, sighandler_t handler) {
    // 1. Save current trapframe on user stack (so sigreturn can restore it)
    uint sp = tf->esp;
    sp -= sizeof(struct trapframe);
    if(copyout(p->pgdir, sp, tf, sizeof(struct trapframe)) < 0) {
        p->killed = 1;
        return;
    }
    // 2. Push the signal handler's argument (signo) and return address (sigreturn stub)
    sp -= sizeof(int);
    int signo_val = sig;
    copyout(p->pgdir, sp, &signo_val, sizeof(int));
    // Return address: user-mode sigreturn stub (code that calls sys_sigreturn)
    sp -= sizeof(uint);
    uint sigreturn_addr = (uint)p->sigreturn_stub;  // See step 6
    copyout(p->pgdir, sp, &sigreturn_addr, sizeof(uint));
    // 3. Modify trapframe: eip points to handler, esp points to new stack top
    tf->eip = (uint)handler;
    tf->esp = sp;
}
```

### 6. Implement sigreturn and user-mode stub (src/sysproc.c, user/)

After the signal handler finishes executing, `sigreturn` must restore the original context:

```c
int sys_sigreturn(void) {
    struct proc *p = myproc();
    struct trapframe *tf = p->tf;
    // Restore previously saved trapframe from user stack
    struct trapframe saved;
    uint sp = tf->esp;
    // Skip signo argument to find the saved trapframe
    copyin(p->pgdir, (char*)&saved, sp + sizeof(uint) + sizeof(int),
           sizeof(struct trapframe));
    *tf = saved;
    return 0;
}
```

User-mode sigreturn stub (inline assembly, in `user/signal.c`):

```c
// Automatically jumped to after signal handler returns
void sigreturn_stub(void) {
    // Invoke sys_sigreturn system call
    asm volatile("movl %0, %%eax; int $64" :: "i"(SYS_sigreturn));
}
```

### 7. Write tests (user/signaltest.c)

```
Test 1: signal(SIGINT, handler) + kill(pid, SIGINT) -> handler is called
Test 2: SIGKILL cannot be caught, process terminates
Test 3: SIG_IGN: process does not terminate after kill sends SIGTERM
Test 4: After signal handling completes, original execution resumes (verify sigreturn correctness)
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Asynchronous event notification | Signals are the kernel's mechanism for delivering asynchronous events to processes |
| Trapframe modification | The kernel controls user-mode execution flow by modifying tf->eip/esp |
| Signal frame (user stack frame) | The kernel saves context on the user stack, restored when signal returns |
| Non-catchable signals | SIGKILL cannot be overridden, ensuring system control |
| Signal masking | sig_mask implements temporary signal deferral, preventing re-entrancy |
| User/kernel boundary | Signal handlers run in user mode but are triggered by kernel-initiated jumps |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| include/proc.h | Modify | Add signal-related fields |
| src/proc.c | Modify | `allocproc`/`fork` initialize signal fields, implement signal delivery |
| src/sysproc.c | Modify | Implement `sys_signal`, `sys_kill` (extended), `sys_sigreturn` |
| src/trap.c | Modify | Call `deliver_pending_signals` before returning to user mode |
| include/syscall.h | Modify | Add `SYS_signal`, `SYS_sigreturn` |
| user/signaltest.c | New | Signal mechanism verification test |

## Verification

```bash
make clean && make qemu-nox
$ signaltest
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| Signal handler is called | Handler prints message after kill | signaltest output |
| SIGKILL cannot be caught | signal(SIGKILL, fn) returns error | signaltest verification |
| SIG_IGN takes effect | Ignored signals do not trigger handler or terminate process | signaltest verification |
| sigreturn is correct | After handler returns, original code continues executing | signaltest verifies subsequent code runs |
| usertests passes | Does not break existing functionality | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Crash after handler executes | sigreturn does not correctly restore %esp | Check signal frame stack layout and offset calculations |
| Process does not respond after kill | deliver function not called at the right position | Confirm it is called at end of trap(), before user-mode return |
| SLEEPING process does not receive signal in time | kill does not wake SLEEPING process | Confirm `p->state = RUNNABLE` after setting pending |

## Key Code Paths

- Register handler: `sys_signal()` -> `p->sig_handlers[signo] = handler`
- Send signal: `sys_kill(pid, signo)` -> `p->sig_pending |= (1<<signo)` -> wake SLEEPING
- Deliver signal: end of `trap()` -> `deliver_pending_signals()` -> `setup_signal_frame()`
- Resume execution: signal handler returns -> `sigreturn_stub` -> `sys_sigreturn()` -> restore trapframe

## Design Trade-offs

| Aspect | Original kill | Full Signal Mechanism |
|--------|--------------|----------------------|
| Signal types | 1 (terminate) | NSIG=16 types |
| User handling | Not supported | Can register custom handler |
| Signal ignoring | Not supported | SIG_IGN |
| Implementation complexity | Minimal (set killed bit) | High (signal frame, sigreturn) |
| Async safety | Not applicable | Handler can only call async-signal-safe functions |

## Advanced Challenges

- [ ] Implement `sigprocmask`: Runtime modification of signal mask set
- [ ] Implement `sigaction` (more complete interface): Support `SA_RESTART`, `SA_ONESHOT` and other flags
- [ ] Implement `SIGCHLD`: Automatically sent to parent when child exits, enabling non-blocking wait
- [ ] Implement `alarm(seconds)`: Send `SIGALRM` after a timeout (requires kernel timer support)
- [ ] Analyze and implement **signal multi-thread safety**: When user-level threads (lab-lib-03) exist, which thread receives signals?
