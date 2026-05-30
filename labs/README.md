# xv6 Labs

[中文](i18n/zh-CN/README.md)

This directory contains lab exercises for the xv6 teaching operating system, spanning seven series -- Boot, User Libraries, Memory Management, File Systems, Scheduling, Process Management, and Synchronization -- with a total of 29 labs.

## Available Labs

### Boot Series

A set of modification labs centered around the xv6 boot chain (bootasm -> bootmain -> entry -> main -> initcode). Each lab can be completed independently or combined for a richer boot experience. Lower numbers are more fundamental; completing them in order is recommended.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-boot-01-vga](lab-boot-01-vga/) | ★☆☆☆☆ | VGA text mode boot message printing | MMIO, VGA buffer, 512-byte limit |
| 02 | [lab-boot-02-stage2](lab-boot-02-stage2/) | ★★★☆☆ | Multi-stage bootloader | Stage 1/2, disk layout, ELF loading |
| 03 | [lab-boot-03-memdetect](lab-boot-03-memdetect/) | ★★★★☆ | Real-mode E820 memory detection | BIOS interrupts, real/protected mode, bootloader-kernel data passing |
| 04 | [lab-boot-04-multiboot](lab-boot-04-multiboot/) | ★★★★☆ | GRUB Multiboot compatible boot | Multiboot specification, ELF, dual boot path |
| 05 | [lab-boot-05-graphic](lab-boot-05-graphic/) | ★★★★☆ | VGA graphics mode boot splash | VBE, framebuffer, bitmap fonts, page mapping |
| 06 | [lab-boot-06-customfmt](lab-boot-06-customfmt/) | ★★★★★ | Custom kernel image format replacing ELF | Executable formats, linker scripts, build toolchain |

### Other Standalone Labs

| Lab | Difficulty | Description | Key Files |
|-----|------------|-------------|-----------|
| [lab-userspace](lab-userspace/) | ★★☆☆☆ | User identity and file permission management | src/sysproc.c, src/sysfile.c, src/fs.c |
| [lab-userspace-01-shell-edit](lab-userspace-01-shell-edit/) | ★★★☆☆ | Interactive shell line editor (Tab completion + history keys) | user/sh.c |
| [lab-fifo-sched](lab-fifo-sched/) | ★★☆☆☆ | FIFO non-preemptive queue scheduler | src/proc.c, src/trap.c, include/proc.h |

### lib User Library Series

Build a complete runtime library in xv6 user space, from memory allocation to formatted output to coroutine scheduling.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-lib-01-malloc](lab-lib-01-malloc/) | ★★★☆☆ | User-space heap allocator (explicit free list) | sbrk, first-fit, boundary coalescing |
| 02 | [lab-lib-02-printf](lab-lib-02-printf/) | ★★☆☆☆ | Full printf format engine | va_list, format specifiers, buffered output |
| 03 | [lab-lib-03-thread](lab-lib-03-thread/) | ★★★★☆ | User-space coroutine library (setjmp/longjmp) | TCB, cooperative scheduling, context switching |

### mm Memory Management Series

From kernel object allocators to copy-on-write, progressively master xv6 virtual memory management.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-mm-01-slab](lab-mm-01-slab/) | ★★★★☆ | Slab allocator replacing kalloc | kmem_cache, slab, internal fragmentation |
| 02 | [lab-mm-02-cow](lab-mm-02-cow/) | ★★★★☆ | Copy-on-write fork | PTE_COW, reference counting, page fault handling |
| 03 | [lab-mm-03-lazy](lab-mm-03-lazy/) | ★★★☆☆ | Lazy allocation | Lazy sbrk, demand paging, T_PGFLT |
| 04 | [lab-mm-04-mmap](lab-mm-04-mmap/) | ★★★★★ | mmap/munmap system calls | VMA, file mapping, MAP_SHARED |

### fs File System Series

Deep dive into xv6's Unix FFS file system, from capacity expansion to crash consistency.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-fs-01-bigfile](lab-fs-01-bigfile/) | ★★★☆☆ | Double indirect blocks (large file support) | Doubly-indirect block, bmap, itrunc |
| 02 | [lab-fs-02-symlink](lab-fs-02-symlink/) | ★★★☆☆ | Symbolic links | T_SYMLINK, namei, O_NOFOLLOW |
| 03 | [lab-fs-03-lrucache](lab-fs-03-lrucache/) | ★★★★☆ | LRU buffer cache | LRU list, hot/cold data separation, bget/brelse |
| 04 | [lab-fs-04-crash](lab-fs-04-crash/) | ★★★★★ | Crash consistency experiments | WAL, commit phase, fscheck tool |

### sched Scheduling Series

Implement three classic scheduling policies, from static priority to proportional sharing.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-sched-01-priority](lab-sched-01-priority/) | ★★★☆☆ | Static priority scheduler | priority field, priority inversion |
| 02 | [lab-sched-02-mlfq](lab-sched-02-mlfq/) | ★★★★☆ | Multi-Level Feedback Queue (MLFQ) | 3-level queues, time quanta, promotion mechanism |
| 03 | [lab-sched-03-stride](lab-sched-03-stride/) | ★★★★☆ | Stride scheduling (proportional sharing) | tickets, stride, pass, settickets |

### proc Process Management Series

Extend xv6's process model with signals, precise wait, and shared memory.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-proc-01-signal](lab-proc-01-signal/) | ★★★★☆ | Unix signal mechanism | signal frame, sigreturn, sig_pending |
| 02 | [lab-proc-02-waitpid](lab-proc-02-waitpid/) | ★★★☆☆ | waitpid and process groups | WNOHANG, pgid, orphan/zombie processes |
| 03 | [lab-proc-03-shm](lab-proc-03-shm/) | ★★★★☆ | Shared memory IPC | Page table sharing, reference counting, shmget/shmat |

### sync Synchronization Series

Build user-space synchronization primitives from atomic operations and understand classic concurrency problems.

| # | Lab | Difficulty | Description | Key Concepts |
|---|-----|------------|-------------|--------------|
| 01 | [lab-sync-01-mutex](lab-sync-01-mutex/) | ★★★☆☆ | User-space mutex | xchg atomic operation, spin vs sleep, futex |
| 02 | [lab-sync-02-rwlock](lab-sync-02-rwlock/) | ★★★★☆ | Read-write lock | Shared/exclusive lock, writer priority, starvation |
| 03 | [lab-sync-03-semaphore](lab-sync-03-semaphore/) | ★★★☆☆ | Semaphore | P/V operations, producer-consumer, dining philosophers |

## Recommended Learning Path

```
Beginner (★☆ ~ ★★☆)
  lab-boot-01-vga -> lab-lib-02-printf -> lab-mm-03-lazy
  lab-proc-02-waitpid -> lab-sync-01-mutex -> lab-sync-03-semaphore

Intermediate (★★★☆)
  lab-boot-02-stage2 -> lab-boot-03-memdetect -> lab-lib-01-malloc
  lab-sched-01-priority -> lab-fs-01-bigfile -> lab-fs-02-symlink

Challenge (★★★★ ~ ★★★★★)
  lab-mm-02-cow -> lab-mm-04-mmap -> lab-fs-03-lrucache -> lab-fs-04-crash
  lab-sched-02-mlfq -> lab-sched-03-stride -> lab-proc-01-signal
  lab-lib-03-thread -> lab-sync-02-rwlock -> lab-proc-03-shm

Combination Challenges
  lab-proc-01-signal + lab-proc-02-waitpid -> Complete POSIX process model
  lab-proc-03-shm + lab-sync-03-semaphore -> Cross-process producer-consumer
  lab-mm-02-cow + lab-mm-04-mmap -> Modern VM subsystem
```

## Boot Lab Dependency Graph

```
lab-boot-01-vga (beginner, no prerequisites)
    |
    +-- lab-boot-02-stage2 (overcome the 512-byte limit)
    |       |
    |       +-- lab-boot-05-graphic (Stage 2 has room for fonts and drawing code)
    |       +-- lab-boot-06-customfmt (Stage 2 can support compression and other advanced features)
    |
    +-- lab-boot-03-memdetect (insert E820 detection in bootasm.S real-mode segment)
    |
    +-- lab-boot-04-multiboot (replace entire custom bootloader; can combine with memdetect)

Combinable: memdetect + multiboot, stage2 + graphic, stage2 + customfmt
```

## Directory Structure

```
labs/
+-- README.md              # This file
+-- lab-template/          # New lab template
|
+-- lab-boot-01-vga/       # Boot message printing
+-- lab-boot-02-stage2/    # Multi-stage boot
+-- lab-boot-03-memdetect/ # Memory detection
+-- lab-boot-04-multiboot/ # GRUB Multiboot
+-- lab-boot-05-graphic/   # Graphics mode boot splash
+-- lab-boot-06-customfmt/ # Custom kernel image format
|
+-- lab-userspace/         # User identity and file permissions
+-- lab-userspace-01-shell-edit/ # Shell line editor (Tab + history)
+-- lab-fifo-sched/        # FIFO non-preemptive scheduler
|
+-- lab-lib-01-malloc/     # User-space heap allocator
+-- lab-lib-02-printf/     # printf format engine
+-- lab-lib-03-thread/     # User-space coroutine library
|
+-- lab-mm-01-slab/        # Slab allocator
+-- lab-mm-02-cow/         # Copy-on-write fork
+-- lab-mm-03-lazy/        # Lazy allocation
+-- lab-mm-04-mmap/        # mmap/munmap
|
+-- lab-fs-01-bigfile/     # Double indirect blocks
+-- lab-fs-02-symlink/     # Symbolic links
+-- lab-fs-03-lrucache/    # LRU buffer cache
+-- lab-fs-04-crash/       # Crash consistency
|
+-- lab-sched-01-priority/ # Static priority scheduling
+-- lab-sched-02-mlfq/     # Multi-level feedback queue
+-- lab-sched-03-stride/   # Stride scheduling
|
+-- lab-proc-01-signal/    # Unix signal mechanism
+-- lab-proc-02-waitpid/   # waitpid and process groups
+-- lab-proc-03-shm/       # Shared memory IPC
|
+-- lab-sync-01-mutex/     # User-space mutex
+-- lab-sync-02-rwlock/    # Read-write lock
+-- lab-sync-03-semaphore/ # Semaphore
```

## How to Start a New Lab

1. Copy the template: `cp -r labs/lab-template labs/lab-<name>`
2. Edit `labs/lab-<name>/README.md` to describe the lab goals and steps
3. Develop on a new git branch: `git checkout -b lab-<name>`

## Build and Run

```bash
make                    # Build
make qemu-nox           # Run (Ctrl+A X to exit)
make lab-list           # List available labs
```
