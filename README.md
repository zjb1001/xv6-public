# xv6-public

> The best way to learn operating systems is to build one yourself.

xv6 is a teaching operating system from MIT's 6.828/6.S081 course, re-implementing Unix Version 6 on x86 multiprocessor. This project builds on xv6 as a hands-on learning platform for OS fundamentals.

**[中文文档](docs/i18n/zh-CN/README.md)**

## Why This Project

The approach to learning operating systems hasn't changed: read source code, understand the design, make modifications, verify results. What *has* changed is that AI-powered tools now dramatically reduce the cost of debugging and problem-solving, making the learning loop much faster.

This project extends the original xv6-public in two ways:

- **Modern project structure** — Restructured from a flat directory into `boot/`, `kernel/`, `user/`, `include/` etc., reflecting how real-world OS projects are organized
- **Lab experiment system** — A comprehensive set of labs covering OS core subsystems, with AI Skill integration for guided hands-on practice

## Directory Structure

```
xv6-public/
├── boot/          Bootloader (bootasm.S, bootmain.c, kernel.ld)
├── kernel/        Kernel source + kernel headers
├── user/          User programs + user libraries (cat, echo, sh, ls, grep, ...)
├── include/       Shared headers (types.h, syscall.h, traps.h, ...)
├── tools/         Build tools (mkfs, vectors.pl, sign.pl, ...)
├── docs/          Documentation
├── labs/          Lab design documents
├── lab-Tests/     Lab test code and verification scripts
├── Makefile       Top-level build file
└── build/         Build output directory (created by make)
```

## Quick Start

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt-get install gcc-multilib qemu-system-x86

# macOS (cross-compiler toolchain required)
# See https://pdos.csail.mit.edu/6.828/
```

### Build and Run

```bash
git clone https://github.com/zjb1001/xv6-public.git
cd xv6-public

make              # Build kernel and user programs
make qemu         # Run in QEMU (with window)
make qemu-nox     # Run in QEMU (terminal only)
make qemu-gdb     # Run with GDB debugging
make clean        # Clean build artifacts
```

Once you see the `$` prompt, xv6 is running. Try `ls`, `cat README`, `echo hello`. Press `Ctrl-A X` to exit QEMU.

### Debugging with GDB

```bash
# Terminal 1: start QEMU (waits for GDB connection)
make qemu-gdb

# Terminal 2: start GDB
gdb build/xv6kernel -x .gdbinit
```

## Lab System

A series of labs designed around xv6's core subsystems. Each lab includes design documentation and automated tests:

| Category | Lab | Key Concepts |
|----------|-----|-------------|
| **Boot** | boot-01-vga | VGA text mode output |
| | boot-02-stage2 | Two-stage bootloader |
| | boot-03-memdetect | E820 memory detection |
| | boot-04-multiboot | GRUB Multiboot protocol |
| **Scheduling** | sched-01-priority | Static priority scheduling |
| | sched-02-mlfq | Multi-level feedback queue |
| | sched-03-stride | Stride proportional-share scheduling |
| **Memory** | mm-01-slab | Slab allocator |
| | mm-02-cow | Copy-on-Write |
| | mm-03-lazy | Lazy page allocation |
| | mm-04-mmap | mmap memory mapping |
| **File System** | fs-01-bigfile | Large file support |
| | fs-02-symlink | Symbolic links |
| | fs-03-lrucache | LRU buffer cache |
| | fs-04-crash | Crash consistency |
| **Synchronization** | sync-01-mutex | Mutex locks |
| | sync-02-rwlock | Read-write locks |
| | sync-03-semaphore | Semaphores |
| **Process** | proc-01-signal | Signal mechanism |
| | proc-02-waitpid | waitpid system call |
| | proc-03-shm | Shared memory |
| **User Library** | lib-01-malloc | malloc allocator |
| | lib-02-printf | printf formatting |
| | lib-03-thread | User-level threads |

### Running a Lab

Each lab in `lab-Tests/<lab-name>/` has its own Makefile:

```bash
cd lab-Tests/<lab-name>

make              # Apply patches + build
make qemu-nox     # Apply patches + build + run
make apply        # Apply patches only (idempotent, safe to re-run)
make unapply      # Revert all patches (git restore)
make clean        # Clean build artifacts (keep patches)
```

`apply` patches the lab code into the xv6 source tree. `unapply` restores original code via `git restore`.

## AI-Powered Learning with Skills

This project includes Claude Code Skills that deeply integrate AI capabilities with xv6 learning:

| Skill | Command | Description |
|-------|---------|-------------|
| **xv6-dev** | `/xv6-dev` | Develop new features — code with OS concept explanations + automatic code review |
| **xv6-explain** | `/xv6-explain` | Explain code — map source to OS textbook concepts, auto-trace execution paths |
| **xv6-simulate** | `/xv6-simulate` | Simulate execution — step through syscalls, scheduling, and memory operations |
| **xv6-review** | `/xv6-review` | Review code — evaluate changes from an OS design perspective |
| **xv6-debug** | `/xv6-debug` | Diagnose issues — locate root cause of boot failures, panics, deadlocks |

### Examples

```
# Explain a concept
/xv6-explain How does process scheduling work in xv6?

# Simulate a system call
/xv6-simulate Trace the full path of a write syscall from user space to kernel

# Develop a new feature
/xv6-dev Implement a semaphore mechanism in xv6

# Review a code change
/xv6-review Review the latest commit

# Diagnose a runtime issue
/xv6-debug xv6 hangs at scheduler after boot
```

## Technical Details

### Kernel Architecture

xv6 implements the core Unix V6 functionality:

- **Process Management** — PCB (struct proc), context switching (swtch.S), round-robin scheduling
- **Virtual Memory** — Two-level page tables, 4KB pages, kernel/user address space isolation
- **File System** — Unix FFS, inodes, buffer cache, write-ahead logging (WAL)
- **System Calls** — 21 syscalls, entering kernel via INT 64
- **Synchronization** — Spinlocks + sleep locks, SMP multiprocessor support
- **Interrupt Handling** — IDT, trap frames, LAPIC/IOAPIC

### Memory Layout

```
Physical Memory:
0x00000000 ┌──────────────┐
           │   I/O Space  │
0x00100000 ├──────────────┤
           │  Kernel Code │
           │  Free Pages  │
0xE0000000 ├──────────────┤ PHYSTOP
0xFE000000 ├──────────────┤
           │  MMIO Devices│
0xFFFFFFFF └──────────────┘

Process Virtual Address Space:
0x00000000 ┌──────────────┐
           │  User Text   │
           │  User Heap   │ (sbrk)
           │  User Stack  │
0x80000000 ├──────────────┤ KERNBASE
           │ Kernel Text  │ (P2V mapping)
0xFE000000 ├──────────────┤
           │  Devices     │
0xFFFFFFFF └──────────────┘
```

## Acknowledgments

- **xv6 Authors** — Frans Kaashoek, Robert Morris, Russ Cox (MIT PDOS)
- **Original Project** — [mit-pdos/xv6-public](https://github.com/mit-pdos/xv6-public)
- xv6 is inspired by John Lions' *Commentary on UNIX 6th Edition*

## License

The original xv6 code is Copyright 2006-2018 Frans Kaashoek, Robert Morris, and Russ Cox.
This project extends and restructures the original codebase under the same license.
