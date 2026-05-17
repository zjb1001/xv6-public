# Lab: Waitpid and Process Groups
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

xv6's `wait()` system call can only wait for **any one** child process to exit, and the caller is forced to block. This means:

- Cannot wait for a **specific** child process (must wait until whichever child called wait exits)
- Cannot **non-blockingly** poll child process status (cannot do other work simultaneously)
- No **process group** concept, cannot manage a set of processes collectively

This lab implements the POSIX-standard `waitpid(pid, &status, options)` and process group mechanisms:

```c
pid_t waitpid(pid_t pid, int *status, int options);
// pid > 0:   Wait for a specific child with that pid
// pid == -1: Wait for any child process (equivalent to wait)
// options & WNOHANG: If no child has exited, return 0 immediately (non-blocking)
```

Core question: *"What are orphan processes and zombie processes respectively? Why must the operating system handle both?"*

## Prerequisites

- **xv6 `wait()` implementation**: `src/proc.c:wait()` holds `ptable.lock`, iterates to find ZOMBIE children, cleans up and returns if found; if not found, `sleep(curproc)` blocks, to be woken when a child calls `exit()`
- **Zombie process**: Process has exited but parent has not called `wait` to reap it. `struct proc` still occupies a slot, holding the exit status (`p->xstate`), waiting for parent's wait
- **Orphan process**: Parent exits before the child. In xv6, orphans are adopted by `init` (pid=1) via `reparent`, and `init` periodically calls wait
- **Process groups**: A collection of related processes (e.g., shell-launched pipeline commands). Sending a signal to the entire process group (e.g., Ctrl+C) can terminate all processes in the pipeline simultaneously

```
Process tree example:
init (pgid=1)
  └─ sh (pgid=100)
       └─ grep pattern | wc -l
            ├─ grep (pgid=101)  ← Same process group
            └─ wc   (pgid=101)  ← Same process group

Ctrl+C sends SIGINT to pgid=101 -> grep and wc both terminate
```

## Lab Tasks

### 1. Add process group field to the process control block (include/proc.h)

```c
struct proc {
    // ... existing fields ...
    int  pgid;       // Process group ID (usually equals the group leader's pid)
    int  xstate;     // Exit status (argument to exit, returned to parent by wait)
};
```

**Initialization**: In `fork()`, child inherits parent's `pgid`; in `allocproc()`, `xstate = 0`.

### 2. Implement sys_waitpid (src/sysproc.c)

```c
int sys_waitpid(void) {
    int pid, options;
    int *status;
    if(argint(0, &pid) < 0 || argptr(1, (char**)&status, sizeof(int)) < 0
       || argint(2, &options) < 0) return -1;
    return waitpid(pid, status, options);
}

int waitpid(int pid, int *status, int options) {
    struct proc *p, *curproc = myproc();
    acquire(&ptable.lock);

    for(;;) {
        int havekids = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            // Confirm p is a child of curproc
            if(p->parent != curproc) continue;
            // Confirm pid matches (pid==-1 matches any, pid>0 exact match)
            if(pid != -1 && p->pid != pid) continue;
            havekids = 1;
            if(p->state == ZOMBIE) {
                // Reap child process
                int cpid = p->pid;
                if(status) *status = p->xstate;
                // Clean up p (freeing stack, pgdir, etc.)
                freeproc(p);
                release(&ptable.lock);
                return cpid;
            }
        }
        if(!havekids || curproc->killed) {
            release(&ptable.lock);
            return -1;
        }
        if(options & WNOHANG) {
            // Non-blocking: no child has exited, return 0 immediately
            release(&ptable.lock);
            return 0;
        }
        // Block and wait until some child exits
        sleep(curproc, &ptable.lock);
    }
}
```

### 3. Modify exit: record exit status (src/proc.c)

Extend `sys_exit(status)` to accept an exit status argument, saving it to `p->xstate`:

```c
int sys_exit(void) {
    int status;
    if(argint(0, &status) < 0) status = 0;
    myproc()->xstate = status;
    exit();
    return 0;  // Unreachable
}
```

### 4. Implement process group operations (src/sysproc.c)

```c
int sys_setpgid(void) {
    int pid, pgid;
    if(argint(0, &pid) < 0 || argint(1, &pgid) < 0) return -1;
    if(pid == 0) pid = myproc()->pid;
    if(pgid == 0) pgid = pid;
    // Find target process and modify its pgid
    struct proc *p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->pid == pid) {
            p->pgid = pgid;
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

int sys_getpgid(void) {
    int pid;
    if(argint(0, &pid) < 0) return -1;
    if(pid == 0) return myproc()->pgid;
    // Find and return target process's pgid
}
```

### 5. Send signals to process groups (combining with lab-proc-01)

If the signal mechanism has been implemented, extend `kill(-pgid, signo)` to send a signal to the entire process group:

```c
// When pid < 0, send signal to all processes in abs(pid) process group
if(pid < 0) {
    int target_pgid = -pid;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->pgid == target_pgid && p->state != UNUSED)
            p->sig_pending |= (1 << signo);
    }
}
```

### 6. Modify sh: use process groups in pipelines

Modify pipeline command creation in `user/sh.c` so that processes on both ends of the pipe share the same process group:

```c
// In sh.c's runcmd(pipe), after forking the child:
if(fork1() == 0) {
    setpgid(0, pipe_pgid);   // Join the pipeline process group
    runcmd(pcmd->left);
}
```

### 7. Write tests (user/waitpidtest.c)

```
Test 1: waitpid(specific_pid) waits for a specific child process
Test 2: WNOHANG: returns 0 when child has not exited (non-blocking)
Test 3: Exit status passing: after exit(42), waitpid gets status == 42
Test 4: Orphan adopted by init: parent exits first, child still completes normally
Test 5: Process groups: set pgid then use kill(-pgid, SIGTERM) to terminate the whole group
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Zombie process | After process exits, struct proc retains xstate, waiting for parent's wait to reap |
| Orphan process | Parent exits first; child is adopted by init (pid=1) |
| WNOHANG | Non-blocking wait: poll child status without blocking |
| Exit status | exit's argument is passed to waitpid caller via xstate |
| Process group | pgid identifies a set of related processes, supporting group-level signal delivery |
| wait semantics | Parent is woken after child exits (sleep/wakeup mechanism) |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| include/proc.h | Modify | Add `pgid`, `xstate` fields |
| src/proc.c | Modify | `fork` inherits pgid, `exit` saves xstate, implement `waitpid` |
| src/sysproc.c | Modify | Register `sys_waitpid`, `sys_setpgid`, `sys_getpgid`, `sys_exit` (extended) |
| include/syscall.h | Modify | Add new system call numbers |
| include/user.h | Modify | Add user-space function declarations |
| user/usys.S | Modify | Add system call assembly stubs |
| user/waitpidtest.c | New | waitpid and process group test |

## Verification

```bash
make clean && make qemu-nox
$ waitpidtest
$ usertests
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| Specific pid wait | waitpid(pid) only returns when that pid exits | waitpidtest verification |
| WNOHANG non-blocking | Returns 0 when child has not exited | waitpidtest verification |
| Exit status correct | After exit(N), status is N | waitpidtest verification |
| Orphan adopted by init | Child runs normally after parent exits | waitpidtest observe child output |
| usertests passes | wait() and exit() existing behavior unchanged | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| waitpid returns -1 for orphan | After adoption, parent has changed to init | Correctly update parent in `reparent()` |
| WNOHANG blocks | options argument parsing error | Confirm `argint(2, &options)` correctly reads the third argument |
| Exit status lost | xstate zeroed in freeproc | Read xstate before zeroing the proc structure |

## Key Code Paths

- Record exit status: `proc.c:exit()` -> `p->xstate = ...` -> mark child as ZOMBIE -> wake parent
- Wait for specific process: `waitpid(pid, ...)` -> scan ptable for matching pid and ZOMBIE
- Non-blocking check: waitpid -> `options & WNOHANG` -> return 0 immediately when no ZOMBIE
- Process group: `setpgid()` -> modify `p->pgid`; `kill(-pgid, sig)` -> iterate processes matching pgid

## Design Trade-offs

| Aspect | Original wait() | waitpid() |
|--------|----------------|-----------|
| Wait target | Any one child process | Can specify pid or any |
| Blocking behavior | Always blocks | Supports WNOHANG non-blocking |
| Exit status | Has it but no status pointer passed | Passed via *status |
| POSIX compatibility | Non-standard | Standard POSIX interface |

## Advanced Challenges

- [ ] Implement `WUNTRACED` option: Wait for process to stop (SIGSTOP) rather than exit
- [ ] Implement `SIGCHLD` signal: Notify parent when child state changes (requires lab-proc-01)
- [ ] Implement complete **Session** concept: Higher-level grouping above process groups
- [ ] Modify `sh.c` to implement complete **Job Control**: fg/bg/jobs commands
- [ ] Track **orphan process count**: Record each reparent in init, analyze orphan generation patterns in the system
