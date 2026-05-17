# Lab: Lazy Allocation (Demand Paging)

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

xv6's `sys_sbrk` immediately calls `growproc` -> `allocuvm` when it receives a request to extend the heap, **promptly allocating and mapping** all requested physical pages. This means:

- A program calls `malloc(100MB)` -> the kernel immediately allocates 100MB of physical memory
- Even if the program only uses 1MB of it, 99MB remains occupied
- When physical memory is scarce, eager allocation leads to unnecessary `kalloc` failures

**Lazy Allocation** is a deferral strategy:

- `sbrk(n)` only updates the process's `sz` field, **without allocating physical pages or creating page table mappings**
- When the program first accesses an address in the extended region, the MMU finds no corresponding mapping in the page table and triggers a **page fault**
- The kernel **allocates a physical page on demand** for that address during page fault handling

Core question: *"Physical memory allocation can be deferred until truly needed — what existing assumptions does this break?"*

## Prerequisites

- **`sys_sbrk` and `growproc`**: `src/sysproc.c:sys_sbrk()` calls `src/vm.c:growproc(n)` to extend the heap; `growproc` calls `allocuvm` to allocate and map physical pages
- **Page fault (#PF)**: Accessing an invalid address triggers interrupt 14 (`T_PGFLT`), `rcr2()` reads the faulting virtual address
- **xv6 address space**: User space `[0, KERNBASE)`, heap region `[data_end, p->sz)` managed by sbrk
- **`walkpgdir`**: Function in `src/vm.c` for traversing page tables, used to check whether an address already has a mapping

```
Lazy allocation timeline:
sbrk(4096):  p->sz += 4096  (no physical page allocated)
              page table: [p->oldsz, p->sz) has no mapping

Access p->oldsz:
  MMU looks up page table -> no mapping -> #PF interrupt
  trap.c: allocate new page, map [PGROUNDDOWN(va), +PGSIZE)
  return to user mode, re-execute the instruction that triggered #PF
```

## Lab Tasks

### 1. Modify sys_sbrk: only update sz (src/sysproc.c)

Change `sys_sbrk` from eager allocation to lazy allocation:

```c
int sys_sbrk(void) {
    int n, addr;
    if(argint(0, &n) < 0) return -1;
    addr = myproc()->sz;
    // Original: if(growproc(n) < 0) return -1;
    // Changed to:
    if(n < 0) {
        if(growproc(n) < 0) return -1;  // shrinking still handled immediately
    } else {
        myproc()->sz += n;               // extending only updates sz
    }
    return addr;
}
```

**Key constraint**: Shrinking the heap (`n < 0`) still requires immediately freeing physical pages to prevent memory leaks.

### 2. Implement lazy allocation in page fault handling (src/trap.c)

Add lazy allocation handling in the `T_PGFLT` branch of `trap()`:

```c
case T_PGFLT: {
    uint va = rcr2();
    struct proc *p = myproc();
    // Check 1: va is within user address space (< p->sz)
    // Check 2: va >= 0 (not a null pointer dereference)
    // Check 3: page table indeed has no mapping (not a COW page fault)
    if(va < p->sz && va >= 0) {
        char *mem = kalloc();
        if(mem == 0) {
            // Physical memory exhausted, kill the process
            p->killed = 1;
            break;
        }
        memset(mem, 0, PGSIZE);
        uint aligned_va = PGROUNDDOWN(va);
        if(mappages(p->pgdir, (void*)aligned_va, PGSIZE,
                    V2P(mem), PTE_W|PTE_U) < 0) {
            kfree(mem);
            p->killed = 1;
        }
    } else {
        // True illegal access, kill the process
        cprintf("pid %d: invalid page fault at va %x\n", p->pid, va);
        p->killed = 1;
    }
    break;
}
```

### 3. Fix copyuvm: skip unmapped regions (src/vm.c)

When `fork()` calls `copyuvm()` to copy the address space, it traverses all addresses in `[0, p->sz)`. With lazy allocation, `[0, p->sz)` may contain unmapped pages, and `walkpgdir` calls within `copyuvm` will encounter invalid PTEs:

```c
// In copyuvm, skip invalid PTEs (rather than panicking)
pte = walkpgdir(pgdir, (void*)i, 0);
if(pte == 0 || !(*pte & PTE_P))
    continue;   // lazy allocation region, child also lazily allocates
```

### 4. Fix freevm: skip unmapped pages (src/vm.c)

When `deallocuvm` traverses the address range to free physical pages, it also needs to skip unmapped addresses:

```c
pte = walkpgdir(pgdir, (char*)a, 0);
if(pte == 0 || !(*pte & PTE_P))
    continue;    // never allocated, no need to free
```

### 5. Handle unmapped addresses in system calls (src/vm.c)

System calls such as `sys_read`/`sys_write` validate user pointers via `argptr`, which checks `walkpgdir` — if an address in a lazy-allocated region is passed in, validation will fail.

Two approaches (choose one):
- Approach A: In `argptr`, if the address is within `[0, p->sz)`, trigger an allocation first then validate
- Approach B: Modify the validation logic to allow any address within `[0, p->sz)` to pass (defer to actual access which triggers #PF)

### 6. Write tests (user/lazytest.c)

```
Test 1: sbrk(64*1024) returns immediately (no OOM), write byte by byte
Test 2: sbrk(1024*1024) but only write first 4096 bytes, only one physical page allocated
Test 3: Access a gap address within sbrk extended region (expect no crash)
Test 4: After fork, child accesses parent's lazy-allocated region (independent allocation)
```

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Demand Paging | Physical pages are allocated only on first access — the essence of lazy allocation |
| Page fault handling | #PF interrupt is the kernel's intervention point, analogous to Linux's `handle_mm_fault` |
| Address space decoupled from physical memory | `p->sz` describes virtual space size, not physical memory usage |
| Zero page | `memset(mem, 0)` ensures uninitialized memory is zeroed (security isolation) |
| Process address space | Valid address range checking (lazy allocation only allowed within `[0, p->sz)`) |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| src/sysproc.c | Modify | `sys_sbrk` only updates `sz` on extension, does not call `growproc` |
| src/trap.c | Modify | `T_PGFLT` branch: lazy allocation handling + kill process on illegal access |
| src/vm.c | Modify | `copyuvm`/`deallocuvm` skip unmapped pages |
| user/lazytest.c | New | Lazy allocation behavior verification |
| Makefile | Modify | Add `lazytest` |

## Verification

### Build and Run

```bash
make clean && make qemu-nox
$ lazytest
$ usertests
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| sbrk does not allocate immediately | `sbrk(1MB)` returns quickly, physical pages don't increase immediately | Add a counter in trap.c |
| On-demand trigger allocation | First access to new address triggers one #PF | trap.c cprintf debugging |
| Valid access doesn't crash | Lazy-allocated region within `[0, sz)` can be read and written normally | lazytest all PASS |
| Illegal access handled correctly | Accessing addresses `>= sz`: process is killed | lazytest expected behavior verification |
| usertests passes | Existing functionality is not affected | All `usertests` PASS |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Kernel panic in copyuvm | Encountered unmapped PTE | copyuvm should skip `!PTE_P` entries |
| System call argptr failure | read/write passed lazy-allocated address not validated | Modify argptr validation logic |
| Data not zero after lazy allocation | kalloc page has stale data | Confirm `memset(mem, 0, PGSIZE)` |
| Illegal address not correctly killed | Condition check error | Only allow lazy allocation when `va < p->sz && va >= 0` |

## Key Code Paths

- Heap extension: `sysproc.c:sys_sbrk()` -> `p->sz += n` (no physical allocation)
- First access: user program writes -> MMU page fault -> `trap.c:trap(T_PGFLT)` -> `kalloc` + `mappages`
- Skip during fork: `vm.c:copyuvm()` -> `walkpgdir` -> if PTE invalid then `continue`
- Process exit: `vm.c:freevm()` -> `deallocuvm` -> skip unmapped pages

## Design Trade-offs

| Aspect | Eager Allocation (original) | Lazy Allocation |
|--------|----------------------------|-----------------|
| sbrk latency | Proportional to allocation size | O(1), very fast |
| Physical memory usage | Immediately occupied per request | Only accessed pages are occupied |
| Page fault overhead | None | One-time #PF overhead per new page |
| Implementation complexity | Simple | Need to handle copyuvm/freevm compatibility |
| OOM behavior | Fails at sbrk call time | May only OOM on first access |

## Advanced Challenges

- [ ] Combine with **COW fork** (lab-mm-02): lazy allocation + COW both in effect
- [ ] Implement **Guard Page**: set an inaccessible page below the user stack to detect stack overflow
- [ ] Implement **Prefetching**: detect sequential access patterns, pre-allocate subsequent pages
- [ ] Track **page fault count** vs. **actual allocated pages**, compare lazy allocation efficiency across different programs
- [ ] Implement `mincore(addr, len, vec)`: query which lazy-allocated pages have been physically allocated
