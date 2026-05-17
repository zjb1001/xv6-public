# Lab: mmap System Call (Memory-Mapped Files)

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★★

## Motivation

Unix systems have two paradigms for file access: through `read`/`write` system calls (copying between kernel buffer and user buffer), or through `mmap` to directly map files into the process's virtual address space (loaded on demand via page faults, zero-copy access).

`mmap` is one of the most important memory management interfaces in modern operating systems:
- **File mapping**: maps a range of a file to virtual addresses, enabling direct pointer-based reading and writing of file contents
- **Anonymous mapping**: allocates private zero pages, serving as the underlying mechanism for heap allocation (glibc `malloc` uses this for large allocations)
- **Shared mapping**: multiple processes map the same file, enabling IPC without any IPC overhead

Core question: *"Why can a process access a file faster by using pointers directly 'without system calls' compared to read?"*

## Prerequisites

- **VMA (Virtual Memory Area)**: In Linux, each contiguous virtual address mapping is described by `struct vm_area_struct`; xv6 requires you to implement this yourself
- **File page cache**: File contents are cached in the kernel buffer cache (`bio.c`); mmap can directly map these cached pages to user space, avoiding additional copies
- **`mappages`**: Function in `src/vm.c` that establishes PTEs, supporting arbitrary physical addresses and protection flags
- **`munmap` writeback**: If mapped as `MAP_SHARED`, modifications to the mapped region must be written back to the file during munmap

```
Two types of mmap mappings:
File mapping: va [0x5000, 0x6000) -> file foo.txt [offset 0, len 4096]
              access va 0x5000 -> #PF -> read file offset 0 -> physical page -> map

Anonymous mapping: va [0x7000, 0x8000) -> zero page (no file associated)
                   access va 0x7000 -> #PF -> kalloc zero page -> map
```

## Lab Tasks

### 1. Define VMA structure (include/proc.h)

```c
#define NVMA  16       // max 16 VMAs per process

#define MAP_SHARED   0x1
#define MAP_PRIVATE  0x2
#define MAP_ANON     0x4

#define PROT_READ    0x1
#define PROT_WRITE   0x2

struct vma {
    uint   valid;      // 1=in use, 0=empty slot
    uint   addr;       // mapping start virtual address
    uint   len;        // mapping length (bytes, page-aligned)
    int    prot;       // PROT_READ | PROT_WRITE
    int    flags;      // MAP_SHARED | MAP_PRIVATE | MAP_ANON
    struct file *f;    // associated file (0 for MAP_ANON)
    uint   offset;     // file offset (bytes)
};
```

Add `struct vma vmas[NVMA]` to `struct proc`.

### 2. Implement sys_mmap (src/sysfile.c)

```c
void* sys_mmap(void)
```

Parameters (following Linux conventions): `addr hint`, `len`, `prot`, `flags`, `fd`, `offset`

Implementation steps:
1. Parse parameters with `argint`/`argfd`, validate (len > 0, flags valid, prot consistent with file open mode)
2. Choose mapping address: ignore hint, allocate directly above process `p->sz` (`addr = PGROUNDUP(p->sz)`), update `p->sz`
3. Find a free `vmas[]` slot, fill in VMA information
4. If it's a file mapping, call `filedup(f)` to increment file reference count
5. **Do not immediately establish page table mappings** (lazy mapping), return `addr`

### 3. Handle VMA in page fault (src/trap.c)

Extend `T_PGFLT` handling:

```c
// After lazy allocation handling, check if va hits a VMA
struct vma *vma = vma_find(p, va);   // find VMA containing va
if(vma) {
    char *mem = kalloc();
    memset(mem, 0, PGSIZE);
    if(vma->flags & MAP_ANON) {
        // anonymous mapping: zero page is sufficient
    } else {
        // file mapping: read data from file
        uint page_offset = PGROUNDDOWN(va) - vma->addr + vma->offset;
        readi(vma->f->ip, mem, page_offset, PGSIZE);
    }
    uint perm = PTE_U;
    if(vma->prot & PROT_WRITE) perm |= PTE_W;
    mappages(p->pgdir, (void*)PGROUNDDOWN(va), PGSIZE, V2P(mem), perm);
}
```

### 4. Implement sys_munmap (src/sysfile.c)

```c
int sys_munmap(void)
```

Parameters: `addr`, `len`

Implementation steps:
1. Find the corresponding VMA (addr and len must exactly match or be a subset of the VMA)
2. If `MAP_SHARED` file mapping, write dirty pages back to file (traverse address range, find mapped pages, call `writei`)
3. Remove page table mappings (call `deallocuvm` or unmap page by page)
4. `fileclose(vma->f)` to release file reference
5. Clear the VMA slot

### 5. Modify fork and exit to handle VMAs (src/proc.c)

- **fork**: copy parent's `vmas[]` array, call `filedup(vma->f)` for each valid VMA, handle page tables with COW (or simply re-lazy-map)
- **exit**: perform munmap operation for each valid VMA (writeback + unmap + fileclose)

### 6. Write tests (user/mmaptest.c)

```
Test 1: anonymous mmap + read/write + munmap, verify memory independence
Test 2: file mapping (read-only): mmap a text file, read contents with pointer, compare with read() result
Test 3: file mapping (read-write MAP_SHARED): modify mapped region, after munmap use read to verify file was updated
Test 4: MAP_PRIVATE: modify mapped region, after munmap verify file was not modified
Test 5: after fork, child inherits mapping; child's MAP_PRIVATE modification doesn't affect parent
```

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| VMA (Virtual Memory Area) | `struct vma` describes the properties and associated file of each mapping |
| File page cache | File mapping data is read in via `readi`, utilizing the buffer cache |
| Writeback | MAP_SHARED munmap writes dirty pages back to file |
| Demand paging | mmap only records the VMA; actual physical pages are allocated on page fault |
| Reference counting | `filedup`/`fileclose` manage file references held by mappings |
| Process address space management | VMA array describes the process address space, a simplified version of Linux's mm_struct |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| include/proc.h | Modify | Add `struct vma` and `vmas[NVMA]` field |
| src/sysfile.c | Modify | Implement `sys_mmap`, `sys_munmap` |
| src/trap.c | Modify | Handle VMA lazy loading in `T_PGFLT` |
| src/proc.c | Modify | Handle VMA inheritance and cleanup in fork/exit |
| include/syscall.h | Modify | Add `SYS_mmap`, `SYS_munmap` numbers |
| include/user.h | Modify | Add `mmap`/`munmap` user-space declarations |
| user/usys.S | Modify | Add system call entries |
| user/mmaptest.c | New | Complete test suite |

## Verification

```bash
make clean && make qemu-nox
$ mmaptest
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| Anonymous mapping read/write | Mapped region can be read and written normally; address invalid after munmap | mmaptest output |
| File mapping read | Pointer-read content matches read() | mmaptest compares both methods |
| MAP_SHARED writeback | After munmap, read verifies file update | mmaptest verification |
| MAP_PRIVATE no writeback | File contents unchanged | mmaptest verification |
| usertests passes | Existing functionality is not affected | All usertests PASS |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| mmap returns illegal address | `p->sz` not correctly updated | Confirm `addr = PGROUNDUP(p->sz); p->sz = addr + len` |
| File content read error | offset calculation error | `page_offset = PGROUNDDOWN(va) - vma->addr + vma->offset` |
| Panic during munmap writeback | inode not locked | `ilock(ip)` before writeback, `iunlock(ip)` after completion |
| Child mmap data incorrect after fork | File reference count not incremented after VMA copy | Call `filedup` for each VMA in fork |

## Key Code Paths

- mmap registration: `sysfile.c:sys_mmap()` -> find free VMA -> fill in -> no page table -> return addr
- Lazy loading: user access -> `#PF` -> `trap.c` -> `vma_find()` -> `readi`/`kalloc` -> `mappages`
- munmap: `sys_munmap()` -> traverse dirty pages for writeback -> `deallocuvm` -> `fileclose`
- exit cleanup: `proc.c:exit()` -> traverse `vmas[]` -> cleanup each valid VMA

## Design Trade-offs

| Aspect | read/write System Calls | mmap File Mapping |
|--------|------------------------|-------------------|
| Data copying | Kernel -> user buffer copy | Zero-copy (direct access to cached pages) |
| Interface complexity | Simple (read/write) | More complex (need to manage VMAs) |
| Random access efficiency | Requires lseek + read | Direct pointer, O(1) |
| Kernel implementation complexity | Simple | Higher (VMA + lazy loading + writeback) |
| Use cases | Sequential I/O | Random access, large files, shared memory |

## Advanced Challenges

- [ ] Implement **msync(addr, len, MS_SYNC)**: actively flush dirty pages without munmap
- [ ] Implement **mprotect(addr, len, prot)**: dynamically change protection attributes of mapped regions
- [ ] Combine with **COW** (lab-mm-02): MAP_PRIVATE uses COW for deferred copying after fork
- [ ] Support **addr hint**: if hint address is free, use it preferentially (POSIX suggested behavior)
- [ ] Track **page fault count** for mmap, compare system call count with equivalent read operations
