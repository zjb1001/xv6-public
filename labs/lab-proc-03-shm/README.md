# Lab: Shared Memory IPC
[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6 currently only has **pipes** for inter-process communication (IPC): unidirectional byte streams, kernel-buffered, requiring system calls for reads and writes. For scenarios requiring high-frequency, large-volume data exchange, each data transfer must pass through kernel buffer copies, limiting performance.

**Shared Memory** is the fastest IPC mechanism:

- A range in both processes' virtual address spaces maps to the **same physical page**
- Processes can directly read and write shared memory, **without system calls** and without data copying
- Requires additional synchronization mechanisms (locks, semaphores) to protect concurrent access

This lab implements System V-style shared memory interfaces: `shmget`/`shmat`/`shmdt`.

Core question: *"Is it safe for two processes' page tables to point to the same physical address? Who manages the lifecycle of this memory?"*

## Prerequisites

- **Page table sharing**: Different virtual addresses in two processes map to the same physical page frame (same physical address) in their page tables; modifications to physical memory are visible to both
- **Reference counting**: Shared physical pages cannot be `kfree`d until all processes have called `shmdt`; requires reference counting (analogous to `lab-mm-02` COW reference counting)
- **Named objects**: `shmget(key, size, flags)` creates or looks up a shared memory object by key, analogous to finding files by path name
- **System V IPC**: A traditional set of Unix IPC interfaces (shmget/msgsnd/semop), gradually being replaced by POSIX IPC and mmap in modern systems

```
Shared memory mapping illustration:
Process A address space:       Process B address space:
  VA 0x6000 ──────┐             VA 0x8000 ──────┐
                  │                              │
                  ▼                              ▼
              Physical page PA 0xC000 (shared memory)
              refcnt = 2

A writes *(char*)0x6000 = 'X' -> B reads *(char*)0x8000 == 'X'
```

## Lab Tasks

### 1. Define shared memory objects (src/shm.c / include/shm.h)

```c
#define NSHM  16        // Maximum shared memory segments
#define SHM_MAXSIZE  (4 * PGSIZE)  // Max 16KB per segment

struct shmobj {
    int     valid;      // Whether in use
    int     key;        // Identifier (agreed-upon integer key between processes)
    uint    size;       // Size (bytes, page-aligned)
    char   *pages[4];   // Pointers to physical pages (max 4 pages)
    int     npages;     // Actual number of pages
    int     refcnt;     // Number of shmat references
    struct  spinlock lock;
};

struct shmobj shmtable[NSHM];   // Global shared memory table
struct spinlock shmtable_lock;
```

### 2. Implement shmget (create or get shared memory segment) (src/sysproc.c)

```c
int sys_shmget(void) {
    int key, size, flags;
    if(argint(0, &key) < 0 || argint(1, &size) < 0 || argint(2, &flags) < 0)
        return -1;

    acquire(&shmtable_lock);
    // 1. First look up existing key
    for(int i = 0; i < NSHM; i++) {
        if(shmtable[i].valid && shmtable[i].key == key) {
            int id = i;
            release(&shmtable_lock);
            return id;  // Return existing segment id
        }
    }
    // 2. Create new segment
    for(int i = 0; i < NSHM; i++) {
        if(!shmtable[i].valid) {
            shmtable[i].valid = 1;
            shmtable[i].key = key;
            shmtable[i].size = PGROUNDUP(size);
            shmtable[i].refcnt = 0;
            shmtable[i].npages = shmtable[i].size / PGSIZE;
            // Allocate physical pages
            for(int j = 0; j < shmtable[i].npages; j++) {
                shmtable[i].pages[j] = kalloc();
                if(!shmtable[i].pages[j]) {
                    // Rollback and fail
                    for(int k = 0; k < j; k++) kfree(shmtable[i].pages[k]);
                    shmtable[i].valid = 0;
                    release(&shmtable_lock);
                    return -1;
                }
                memset(shmtable[i].pages[j], 0, PGSIZE);
            }
            release(&shmtable_lock);
            return i;  // Return new segment id
        }
    }
    release(&shmtable_lock);
    return -1;  // Segment table full
}
```

### 3. Define process SHM attachment records (include/proc.h)

```c
#define NATTACH  8    // Each process can attach up to 8 shared memory segments

struct shmattach {
    int   shmid;    // -1 = empty slot
    uint  addr;     // Virtual address mapped to
};

struct proc {
    // ... existing fields ...
    struct shmattach shm_attach[NATTACH];
};
```

### 4. Implement shmat (attach shared memory to process address space) (src/sysproc.c)

```c
void* sys_shmat(void) {
    int shmid, shmflg;
    void *shmaddr;
    if(argint(0, &shmid) < 0 || argptr(1, (char**)&shmaddr, 0) < 0
       || argint(2, &shmflg) < 0) return (void*)-1;
    if(shmid < 0 || shmid >= NSHM || !shmtable[shmid].valid) return (void*)-1;

    struct proc *p = myproc();
    struct shmobj *shm = &shmtable[shmid];

    // Choose mapping address (ignore shmaddr hint, use above p->sz)
    uint addr = PGROUNDUP(p->sz);
    p->sz = addr + shm->size;

    // Set up page table mapping
    uint perm = PTE_U | PTE_W;
    for(int i = 0; i < shm->npages; i++) {
        if(mappages(p->pgdir, (void*)(addr + i * PGSIZE), PGSIZE,
                    V2P(shm->pages[i]), perm) < 0) {
            // Rollback
            p->sz -= shm->size;
            return (void*)-1;
        }
    }

    // Record attachment info
    for(int i = 0; i < NATTACH; i++) {
        if(p->shm_attach[i].shmid == -1) {
            p->shm_attach[i].shmid = shmid;
            p->shm_attach[i].addr  = addr;
            break;
        }
    }

    acquire(&shmtable_lock);
    shm->refcnt++;
    release(&shmtable_lock);

    return (void*)addr;
}
```

### 5. Implement shmdt (detach shared memory) (src/sysproc.c)

```c
int sys_shmdt(void) {
    void *shmaddr;
    if(argptr(0, (char**)&shmaddr, 0) < 0) return -1;
    struct proc *p = myproc();

    for(int i = 0; i < NATTACH; i++) {
        if(p->shm_attach[i].shmid != -1 && p->shm_attach[i].addr == (uint)shmaddr) {
            int shmid = p->shm_attach[i].shmid;
            struct shmobj *shm = &shmtable[shmid];
            // Unmap page table entries (do NOT free physical pages!)
            for(int j = 0; j < shm->npages; j++) {
                pte_t *pte = walkpgdir(p->pgdir, (void*)((uint)shmaddr + j*PGSIZE), 0);
                if(pte) *pte = 0;
            }
            p->shm_attach[i].shmid = -1;
            p->shm_attach[i].addr  = 0;
            // Decrement reference count; if 0, free shared memory object
            acquire(&shmtable_lock);
            shm->refcnt--;
            if(shm->refcnt == 0) {
                for(int j = 0; j < shm->npages; j++) kfree(shm->pages[j]);
                shm->valid = 0;
            }
            release(&shmtable_lock);
            return 0;
        }
    }
    return -1;
}
```

### 6. Auto-shmdt on process exit (src/proc.c)

In `exit()`, iterate `shm_attach[]` and perform shmdt for all valid attachments.

### 7. Write tests (user/shmtest.c)

```
Test 1: Two shmget calls with the same key return the same id
Test 2: Parent does shmget+shmat, after fork child can see data written by parent
        (fork requires copyuvm; verify shared memory pages are NOT copied)
Test 3: Two unrelated processes share data via shmget with the same key (need sequential startup)
Test 4: After the last shmdt, shmget with the same key returns an empty segment (memory freed)
```

## OS Concepts

| Concept | Manifestation in this lab |
|---------|--------------------------|
| Page table sharing | Two processes' PTEs point to the same physical page frame |
| IPC (Inter-Process Communication) | Shared memory is the fastest IPC, no copying needed |
| Reference counting | refcnt manages shared memory lifecycle |
| Named objects | key is an agreed-upon integer identifier between processes |
| Address space management | shmat allocates new regions in the process's virtual address space |
| Special fork handling | Shared memory pages should not be copied during fork (unlike private pages) |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| src/shm.c | New | Shared memory table and `shminit()` |
| include/shm.h | New | `struct shmobj` definition |
| include/proc.h | Modify | Add `shm_attach[]` field |
| src/sysproc.c | Modify | Implement `sys_shmget`/`sys_shmat`/`sys_shmdt` |
| src/proc.c | Modify | `allocproc` initializes `shm_attach`, `exit` auto-shmdt, `fork` handles shared memory inheritance |
| src/main.c | Modify | Call `shminit()` |
| include/syscall.h | Modify | Add system call numbers |
| user/shmtest.c | New | Shared memory IPC test |

## Verification

```bash
make clean && make qemu-nox
$ shmtest
$ usertests
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|-------------------|----------------|
| Data sharing | Process A writes, process B immediately reads | shmtest verification |
| Reference counting | Physical pages freed after last shmdt | Add kalloc count verification |
| Fork inheritance | Child can access parent's shmat region | shmtest fork test |
| usertests passes | Existing functionality not affected | All usertests PASS |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Process B cannot see data written by A | TLB not flushed | Call `lcr3(V2P(pgdir))` after shmat to flush TLB |
| Reference count error | After fork, child also references shared pages but refcnt not incremented | Iterate shm_attach in fork, refcnt++ for each valid attachment |
| Page still accessible after shmdt | Page table entry not zeroed, or TLB not flushed | Call `lcr3` after `*pte = 0` in shmdt |

## Key Code Paths

- Create shared segment: `sys_shmget()` -> look up key -> not found -> `kalloc` physical pages -> return id
- Attach to address space: `sys_shmat()` -> choose VA -> `mappages` -> record `shm_attach` -> `refcnt++`
- Detach: `sys_shmdt()` -> find addr -> clear PTEs -> `refcnt--` -> if 0, kfree

## Design Trade-offs

| Aspect | Pipe | Shared Memory (shm) |
|--------|------|---------------------|
| Data copying | 2 times (write->kernel, kernel->read) | 0 times (direct access to same physical page) |
| Synchronization | Built-in (read blocks until data available) | None built-in, requires external synchronization |
| Communication direction | Unidirectional | Bidirectional |
| Persistence | None (auto-released on process exit) | Controlled by explicit shmdt |
| Use case | Streaming data transfer | High-frequency, large-volume data exchange |

## Advanced Challenges

- [ ] Combine with **lab-sync-03-semaphore**: Use semaphores to synchronize concurrent access to shared memory (classic producer-consumer)
- [ ] Implement **shmctl(IPC_RMID)**: Force-delete a shared memory segment (mark for deletion even if refcnt > 0; free when all shmdt complete)
- [ ] Implement **access control**: Specify access permissions on shmget creation, check permissions in shmat (combine with lab-userspace)
- [ ] Implement **POSIX named shared memory** (`shm_open`/`mmap`) interface, based on mmap (combine with lab-mm-04)
- [ ] Benchmark **throughput comparison** between shared memory IPC and pipe IPC (system call count and time for transferring the same data volume)
