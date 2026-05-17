# Lab: Slab Allocator (Slab Kernel Object Allocator)

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6's `kalloc()` implementation is extremely simple: it maintains a free list of physical page frames (4KB), allocating or freeing an entire page each time. When the kernel needs to allocate a small object of only a few dozen bytes (such as `struct proc`, `struct file`, `struct buf`), kalloc's coarse granularity causes severe **internal fragmentation** — each small object occupies an entire page, wasting most of the space.

The Linux kernel uses a **Slab allocator** to solve this problem:

- Maintain a **cache (kmem_cache)** for each fixed-size object type
- Each cache consists of several **Slabs** (a group of contiguous physical pages), with each Slab pre-divided into equally-sized slots
- Allocation takes a slot from the Slab's free slot list; freeing returns it to the list — no memory initialization needed

Core question: *"Why doesn't Linux just use malloc for kernel objects? What are the fundamental advantages of Slab?"*

## Prerequisites

- **Physical memory allocation**: xv6 `kalloc()`/`kfree()` in `src/kalloc.c`, manages the free page list for `[end, PHYSTOP)`
- **Internal fragmentation**: When the allocation unit (page) is much larger than the requested size, the wasted space is called internal fragmentation
- **Cache Coloring**: Slab sets different starting offsets for different Slabs, staggering CPU cache lines to reduce false sharing
- **Object construction/destruction**: Slab can call constructor/destructor functions on alloc/free (simplified in this lab, not implemented)

```
kmem_cache structure diagram:
kmem_cache["proc"] (obj_size=sizeof(struct proc))
  |
  +-- Slab 1 (4KB physical page)
  |   +-- slot 0 [allocated]
  |   +-- slot 1 [free] ---- free_list
  |   +-- slot 2 [free] ---- ...
  |   +-- ... (total 4096 / sizeof(struct proc) slots)
  |
  +-- Slab 2 (4KB physical page)
      +-- ...
```

## Lab Tasks

### 1. Define Slab data structures (src/kalloc.c / include/defs.h)

```c
struct slab_obj {
    struct slab_obj *next;   // free slot list
};

struct slab {
    struct slab     *next;   // Slab list
    void            *mem;    // pointer to allocated physical page
    uint             nfree;  // number of free slots in this Slab
    struct slab_obj *freelist; // head of free slot list
};

struct kmem_cache {
    char            name[16]; // debug name
    uint            obj_size; // size of each object (bytes, aligned to 8)
    uint            objs_per_slab; // number of objects per Slab
    struct slab    *slabs;    // head of Slab list
    struct spinlock lock;
};
```

**Key constraint**: `obj_size` must be >= `sizeof(struct slab_obj*)` (when free, the slot itself stores the next pointer)

### 2. Implement kmem_cache initialization

```c
struct kmem_cache* kmem_cache_create(char *name, uint obj_size)
```

- Allocate a `kmem_cache` structure from a static array (avoiding the chicken-and-egg problem)
- Calculate `objs_per_slab = PGSIZE / ALIGN8(obj_size)`
- Initialize spinlock, `slabs = 0`

### 3. Implement Slab growth (acquire new pages from kalloc)

```c
static struct slab* slab_grow(struct kmem_cache *c)
```

- Call `kalloc()` to allocate one physical page as the new Slab's `mem`
- Call `kalloc()` again for a small page to hold `struct slab` metadata (or embed metadata at the beginning of mem)
- Divide mem by `obj_size`, building the slot free list:
  ```c
  for(i = 0; i < objs_per_slab; i++) {
      obj = (struct slab_obj*)((char*)mem + i * obj_size);
      obj->next = freelist;
      freelist = obj;
  }
  ```
- Insert the new Slab at the head of `c->slabs` list

### 4. Implement allocation and freeing

```c
void* kmem_cache_alloc(struct kmem_cache *c)
```
- Acquire lock, traverse Slab list to find a Slab with free slots (`nfree > 0`)
- Remove one slot from `freelist`, `nfree--`
- If all Slabs are full, call `slab_grow` to add a new Slab
- Return slot pointer

```c
void  kmem_cache_free(struct kmem_cache *c, void *obj)
```
- Acquire lock, find the Slab that `obj` belongs to (by address range)
- Insert obj as a `slab_obj` into `freelist`, `nfree++`
- If the Slab is entirely free, optionally return the entire Slab to kalloc

### 5. Integrate into the kernel — replace some kalloc calls

Create the following global caches and replace corresponding allocations in the kernel:

| Cache Name | Replaced Object | Files to Modify |
|------------|----------------|-----------------|
| `kmem_proc` | `struct proc` allocation | src/proc.c |
| `kmem_file` | `struct file` allocation | src/file.c |

### 6. Write tests (kernel panic output statistics)

Output cache statistics during kernel startup:
```
slab: proc cache: obj_size=144 objs_per_slab=28
slab: file cache: obj_size=36 objs_per_slab=113
```

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Internal fragmentation | kalloc allocates an entire page each time, causing severe waste for small objects |
| Slab allocator | Fixed-size object pool, O(1) allocation/freeing |
| Object cache | kmem_cache maintains independent free lists per object type |
| Spinlock protection | Each cache has its own lock, reducing lock contention |
| Physical page management | Slab bulk-acquires pages from kalloc, then subdivides them for small objects |
| Cache Coloring | Slab starting offsets are staggered to exploit CPU cache locality |

## Files to Modify

| File | Change Type | Description |
|------|-------------|-------------|
| src/kalloc.c | Modify | Add Slab implementation: `kmem_cache_*` family of functions |
| include/defs.h | Modify | Export `kmem_cache_create`, `kmem_cache_alloc`, `kmem_cache_free` |
| src/proc.c | Modify | Replace `kalloc()` with `kmem_cache_alloc(kmem_proc)` |
| src/file.c | Modify | Replace `kalloc()` with `kmem_cache_alloc(kmem_file)` |
| src/main.c | Modify | Initialize global Slab caches after `kinit` |

## Verification

### Build and Run

```bash
make clean && make qemu-nox
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| Kernel boots normally | xv6 starts successfully, shell is available | Observe startup output |
| Slab statistics | Cache info printed at startup | Serial output |
| Processes/files work | `ls`, `cat`, `fork`, etc. work normally | Run usertests |
| Memory savings | Fewer physical pages used for the same number of processes | Add `kfree_count` statistics |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Kernel panic: kalloc failed | kalloc returns 0 during Slab growth | Check if physical memory is exhausted, reduce test scale |
| Object data overwritten | Slot used as list node not zeroed, not zeroed on allocation | Call memset(0) before allocation |
| Double free | freelist linkage error after free | Assert slot is not already in freelist |
| Slab obj lookup failed | Address range check error | Use `PGROUNDDOWN(obj) == slab->mem` to locate |

## Key Code Paths

- Initialization: `main.c:main()` -> `kmem_cache_create("proc", sizeof(struct proc))`
- Allocation: `proc.c:allocproc()` -> `kmem_cache_alloc(kmem_proc)` -> take free slot
- Expansion: `kmem_cache_alloc()` -> `slab_grow()` -> `kalloc()` -> divide into slot list
- Freeing: `proc.c:freeproc()` -> `kmem_cache_free(kmem_proc, p)` -> return to freelist

## Design Trade-offs

| Aspect | kalloc (page allocation) | Slab allocator |
|--------|--------------------------|----------------|
| Minimum allocation granularity | 4096 bytes (one page) | `obj_size` bytes |
| Allocation speed | O(1) | O(1) (when Slab exists) |
| Internal fragmentation | Extremely high (small objects) | Extremely low (objects tightly packed) |
| Implementation complexity | Very simple | Moderate |
| Metadata overhead | None | Slab header (can be embedded in page) |

## Advanced Challenges

- [ ] Implement **full empty Slab reclamation**: when all slots in a Slab are free, return the physical page to kalloc
- [ ] Implement **general-purpose size Slabs** (similar to Linux's kmalloc): preset caches for 8/16/32/.../4096 bytes
- [ ] Track **cache hit rate** (ratio of allocations that don't require slab_grow), output performance report
- [ ] Introduce Slab caching for `struct buf` (buffer cache) as well
- [ ] Research **Cache Coloring**: set different starting offsets for different Slabs, measure impact on cache misses
