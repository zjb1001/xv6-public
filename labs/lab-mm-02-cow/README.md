# Lab: Copy-on-Write Fork

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6's `fork()` immediately copies **all physical pages** of the parent process when called. If the parent has 100MB of data, fork must allocate and copy 100MB of physical memory — even if the child process immediately calls `exec()` and discards all of that data.

**Copy-on-Write (COW)** is the standard fork implementation in modern operating systems:

- During fork, physical pages are **not copied**; instead, parent and child share the same set of physical pages
- Shared pages are marked as **read-only** in both processes' page tables (clear PTE_W)
- When either process attempts to write to these pages, the MMU triggers a **Page Fault**
- The kernel allocates a new physical page for the writer during page fault handling, copies the contents, and remaps it as writable

Core question: *"Can two processes share the same physical page? When should they actually copy?"*

## Prerequisites

- **xv6 page tables**: `src/vm.c`, two-level page tables (PDE -> PTE -> physical page), `walkpgdir` looks up page table entries
- **PTE flags**: `PTE_P` (present), `PTE_W` (writable), `PTE_U` (user-mode accessible). Defined in `include/mmu.h`
- **Page faults**: When accessing an invalid address or writing to a read-only page, the CPU triggers `#PF` (interrupt vector 14), handled in `src/trap.c:trap()`, `rcr2()` reads the faulting address
- **Reference counting**: When multiple processes share the same physical page, a count of references is needed; the page can only be truly freed when the count reaches 0

```
Address space after fork() with COW:
Parent page table                Child page table
  VA 0x1000 -> PA 0xA000 [R]     VA 0x1000 -> PA 0xA000 [R]
                                           ^ shared physical page, refcount=2
Parent writes 0x1000 -> triggers #PF -> copy -> PA 0xB000 [W], refcount[0xA000]=1
```

## Lab Tasks

### 1. Add reference counting for physical pages (src/kalloc.c)

Add a reference count array to the `kmem` structure:

```c
int page_refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
```

Implement operations:

```c
void  kref_inc(void *pa);   // reference count +1
void  kref_dec(void *pa);   // reference count -1, call kfree if it reaches 0
int   kref_get(void *pa);   // read reference count
```

- When `kalloc()` returns a new page, initialize `refcnt = 1`
- Change `kfree()`: only truly return the page frame when `kref_dec()` drops the count to 0

**Key constraint**: Reference count operations must be performed under `kmem.lock` protection to prevent concurrent free race conditions.

### 2. Define COW flag (include/mmu.h)

PTE has no dedicated COW bit; use an available software-reserved bit (`PTE_AVAIL`):

```c
#define PTE_COW  0x200    // bit 9, software-defined COW marker
```

### 3. Modify fork: shallow copy page tables (src/vm.c)

Modify `copyuvm()` (called by `fork()`) to change from "deep copy physical pages" to "share physical pages":

For each mapped user page:
1. Clear the `PTE_W` flag in the PTE (not writable)
2. Set the `PTE_COW` flag (mark as copy-on-write page)
3. In the child process page table, establish a mapping using **the same physical address** (also clear PTE_W, set PTE_COW)
4. Call `kref_inc(pa)` to increment the physical page's reference count

**Note**: The parent's page table entries must also have PTE_W cleared, because the physical page is now shared — the parent writing to it also triggers COW.

### 4. Implement COW copying in page fault handling (src/trap.c)

In the `#PF` (T_PGFLT, interrupt 14) handling branch of `trap()`:

```c
if(tf->trapno == T_PGFLT) {
    uint va = rcr2();     // faulting address
    // 1. Check if va is within user address space range
    // 2. Look up the PTE for va, check if PTE_COW is set
    // 3. If it's a COW page:
    //    a. If refcnt == 1, just set PTE_W (this process is the sole owner, no copy needed)
    //    b. If refcnt > 1, allocate new page, copy contents, update PTE, kref_dec old page
    // 4. If not a COW page (true illegal access), kill the process
}
```

### 5. Modify kfree and process exit

- When a process exits (`freevm()`), call `kref_dec(pa)` for each mapped physical page instead of directly calling `kfree(pa)`
- When `exec()` replaces the address space, old pages must also be released via `kref_dec`

### 6. Write test program (user/cowtest.c)

```
Test 1: After fork, parent and child each modify the same address -> no interference
Test 2: Large memory process forks -> does not immediately trigger OOM
Test 3: fork + exec -> COW triggered before exec, memory normal after exec
```

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Copy-on-Write (COW) | fork does not immediately copy physical pages; copying is deferred until actual writes |
| Page fault handling (#PF) | MMU write to read-only page triggers interrupt; kernel intervenes to complete the copy |
| Reference counting | Multiple processes share physical pages; pages are only truly freed when count reaches 0 |
| Page table operations | Modifying PTE flags to implement access control (read-only/writable) |
| TLB consistency | After modifying PTE, need `lcr3()` to flush TLB |
| Memory safety | Illegal writes to non-COW pages should still kill the process |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| src/kalloc.c | Modify | Add `page_refcnt` array, implement `kref_inc`/`kref_dec` |
| include/mmu.h | Modify | Add `PTE_COW` software-defined flag |
| src/vm.c | Modify | Refactor `copyuvm` to shallow copy (set PTE_COW), modify `freevm` |
| src/trap.c | Modify | Add COW handling logic in T_PGFLT branch |
| user/cowtest.c | New | COW behavior verification test |
| Makefile | Modify | Add `cowtest` |

## Verification

### Build and Run

```bash
make clean && make qemu-nox CPUS=1
$ cowtest
$ usertests   # confirm existing functionality is not broken
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| fork does not copy physical pages | Physical page usage barely changes after fork | Compare `kfree_count` statistics before and after fork |
| Parent and child data are independent | Parent's modifications don't affect child and vice versa | cowtest output PASS |
| Page fault triggers copying | refcnt changes after writing to COW page | Add cprintf debugging in trap.c |
| usertests passes | Existing functionality is not affected | All `usertests` PASS |
| No OOM crash | Forking a large process does not immediately fail kalloc | Allocate 30+ pages then fork successfully |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Kernel panic: refcnt < 0 | kref_dec called multiple times | Check if `freevm` calls same pa multiple times |
| Child process data incorrect after fork | Page table TLB not flushed | Call `switchuvm` or `lcr3` after `copyuvm` |
| Killed when writing to valid address | Non-COW page write error handled incorrectly | Check `PTE_COW` flag judgment logic |
| Infinite page fault loop | PTE_W not correctly updated after COW handling | Confirm `lcr3(V2P(pgdir))` is called after modifying PTE |

## Key Code Paths

- fork path: `proc.c:fork()` -> `vm.c:copyuvm()` -> PTE shallow copy + kref_inc
- Write trigger: user writes to read-only page -> CPU generates #PF -> `trap.c:trap()` -> COW handling
- COW handling: `trap()` -> `kalloc()` -> `memmove()` -> update PTE -> `lcr3()` flush TLB
- Process exit: `proc.c:exit()` -> `vm.c:freevm()` -> `kref_dec` each page

## Design Trade-offs

| Aspect | Direct Copy fork (original) | COW fork |
|--------|----------------------------|----------|
| fork latency | O(process memory size) | O(1) — only copies page tables |
| Memory usage | Immediately doubled | Shared before writes, allocated on demand |
| Page fault overhead | None | One-time overhead per write to COW page |
| Implementation complexity | Simple | More complex (reference counting + page fault handling) |
| exec cooperation | fork then exec wastes copy time | fork then immediate exec has nearly zero memory overhead |

## Advanced Challenges

- [ ] Implement **Zero Page**: map all uninitialized pages to the same zeroed physical page (read-only), COW on write
- [ ] Track the number of physical pages saved by fork (actual COW copy count), compare with direct copying
- [ ] Combine with `lab-mm-03-lazy`: sbrk lazy allocation + COW both in effect
- [ ] Test under multicore (CPUS=2), verify reference counting concurrency safety
- [ ] Implement **`vfork()`**: child process directly uses parent's address space, for fork+exec optimization
