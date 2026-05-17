# Lab: Heap Allocator

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

xv6 user programs currently manage heap memory through `malloc()`/`free()`, but the underlying implementation is extremely simplistic — `malloc` simply calls `sbrk` to expand the heap and never reclaims memory; `free` is a no-op. This means programs cannot reuse freed memory, and long-running processes will inevitably exhaust their address space.

This lab requires implementing a truly functional allocator using an **explicit free list**:

- **Allocation**: First-Fit strategy to find the first sufficiently large block in the free list
- **Deallocation**: Return the block to the free list and coalesce with adjacent free blocks
- **Expansion**: When no suitable block is found in the free list, call `sbrk` to request new pages from the kernel

Core question: *"After free(p), how does this block of memory know how large it is and where it should go?"*

## Prerequisites

- **`sbrk` system call**: Adjusts the process data segment limit (heap break), returns the old break address. `sbrk(n)` expands upward by n bytes, `sbrk(0)` returns the current break
- **Block Header**: The allocator hides a header in front of each memory block, recording the block's size and allocation status
- **Memory alignment**: x86 requires data to be 4-byte aligned; the allocator needs to round up all requests to the alignment granularity
- **Fragmentation**: Internal fragmentation (block is larger than requested) and external fragmentation (free blocks are too small and non-contiguous)

```
Heap memory layout:
┌──────┬─────────────────┬──────┬─────────────────┬──────┐
│ HDR  │   User Data     │ HDR  │   User Data     │ ...  │
│8 bytes│   (payload)    │8 bytes│   (payload)    │      │
└──────┴─────────────────┴──────┴─────────────────┴──────┘
  ↑ malloc returns a pointer to the start of the payload; the HDR is before it
```

## Lab Tasks

### 1. Design the Block Header Structure (lib/xv6_stdlib.h)

Define a metadata structure describing each memory block:

```c
typedef struct block_hdr {
    uint   size;          // Total size of this block (including header), in bytes
    uint   in_use;        // 1=allocated, 0=free
    struct block_hdr *next; // Next free block in the free list
    struct block_hdr *prev; // Previous free block in the free list (doubly-linked)
} block_hdr_t;
```

**Key constraints**:
- `size` must always be >= `sizeof(block_hdr_t)`
- All allocation sizes are rounded up to 8-byte alignment
- The pointer returned by `malloc(n)` is `(char*)hdr + sizeof(block_hdr_t)`

### 2. Implement Heap Expansion (lib/xv6_stdlib.c)

```c
static block_hdr_t *heap_extend(uint min_size)
```

- Calculate the number of pages needed (`PGSIZE = 4096`), call `sbrk` to obtain new memory
- Construct the new memory as a large free block and insert it at the tail of the free list
- Attempt to coalesce with the previous free block (if adjacent)
- Return the new free block pointer, or 0 on failure

### 3. Implement First-Fit Allocation (lib/xv6_stdlib.c)

Replace the `x6_malloc` implementation:

```c
void* x6_malloc(uint n)
```

Algorithm flow:
1. Round `n` up to 8-byte alignment, add header size to get `total`
2. Traverse the free list to find the first free block with `size >= total`
3. If the found block is large enough (remaining part > `MIN_SPLIT_SIZE`), **split** it into two blocks: the allocated block + a new free block
4. Remove the allocated block from the free list, set `in_use = 1`
5. If the free list traversal finds nothing -> call `heap_extend` and retry

**Hints**:
- `MIN_SPLIT_SIZE = sizeof(block_hdr_t) + 8`; remaining blocks that are too small should not be split (to avoid fragmentation proliferation)
- When splitting, the new free block is cut from the tail of the current block: `new = (block_hdr_t*)((char*)hdr + total)`

### 4. Implement Free and Coalescing (lib/xv6_stdlib.c)

Replace the `x6_free` implementation:

```c
void x6_free(void *p)
```

Algorithm flow:
1. Derive `hdr = (block_hdr_t*)p - 1` from `p`
2. Assert `hdr->in_use == 1` (double-free detection)
3. Set `hdr->in_use = 0`, insert the block at the head of the free list
4. **Forward coalesce**: Check if the block at `(char*)hdr + hdr->size` is free; if so, merge
5. **Backward coalesce**: Traverse the free list to find the predecessor block; if it is adjacent to this block, merge (or maintain additional backward pointers for speed)

**Coalescing illustration**:
```
Before free: [In-use A][Free B][In-use C][Free D]
Free A:      [Free A][Free B][In-use C][Free D]
After merge: [    Free A+B   ][In-use C][Free D]
```

### 5. Write Test Program (user/malloctest.c)

Verify the following scenarios:

| Test | What it verifies |
|------|-----------------|
| Basic alloc/free | malloc(n) returns non-null, free allows re-allocation |
| Memory alignment | Returned address % 8 == 0 |
| Coalescing effect | free A, free B (A and B are adjacent), then malloc(A+B size) succeeds without heap expansion |
| Many small objects | Loop malloc/free 1000 times; heap size does not grow continuously |
| Zero-byte allocation | `malloc(0)` returns a non-NULL valid address (glibc behavior) |

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| `sbrk` system call | The sole interface for the allocator to request memory from the kernel |
| Heap | The dynamic memory region where the process data segment grows upward |
| Memory alignment | x86 requires 4/8-byte alignment; violations may trigger bus errors |
| Fragmentation | First-Fit produces external fragmentation; coalescing is the key countermeasure |
| Linked list operations | Free blocks are managed with a doubly-linked list; insert/delete are O(1) |
| Implicit metadata | The header sits right before the payload; the user is unaware of its existence |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| lib/xv6_stdlib.h | Modify | Add `block_hdr_t` structure and `PGSIZE`, `MIN_SPLIT_SIZE` macros |
| lib/xv6_stdlib.c | Modify | Replace `x6_malloc`/`x6_free`, add `heap_extend`, linked list operations |
| user/malloctest.c | New | Allocator test program |
| Makefile | Modify | Add `malloctest` to `UPROGS` |

## Verification

### Build and Run

```bash
make clean && make qemu-nox
```

In the xv6 shell, run:

```
$ malloctest
```

### Verification Goals

| Goal | Expected Behavior | How to Observe |
|------|------------------|----------------|
| Basic allocation | `malloc(100)` returns non-null, data is writable | malloctest outputs PASS |
| Address alignment | All returned addresses % 8 == 0 | malloctest checks each and outputs |
| Memory reuse | malloc after free does not expand heap indefinitely | Call `sbrk(0)` to check if break is stable |
| Boundary coalescing | Can allocate a larger block after freeing adjacent blocks | Coalescing test outputs `merge OK` |
| No dangling pointers | Freed memory is no longer used by the program | Code review |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| `malloc` returns NULL | `sbrk` fails in `heap_extend` | Check xv6 heap limit (MAXVA/USERTOP) |
| Data overwritten by header | `malloc` returns the header start instead of payload start | Return `(char*)hdr + sizeof(block_hdr_t)` |
| Infinite heap expansion | Coalescing not working; freed blocks not reused | Check `in_use = 0` and list insertion logic |
| Alignment error | Split block address is not aligned | Ensure `total` goes through alignment calculation |
| Double-free crash | `free` on the same pointer twice | Assert `hdr->in_use == 1` at the beginning of `free` |

## Key Code Paths

- Allocation entry: `lib/xv6_stdlib.c:x6_malloc()` -> traverse free list -> found -> split -> remove
- Heap expansion path: `x6_malloc()` -> `heap_extend()` -> `sbrk()` -> construct free block
- Free entry: `lib/xv6_stdlib.c:x6_free()` -> mark as free -> insert into list -> attempt coalesce
- Forward coalesce: `x6_free()` -> check next block `in_use == 0` -> modify `size`, remove adjacent block

## Design Trade-offs

| Aspect | Original Implementation (sbrk, no reclaim) | First-Fit Free List |
|--------|----------------------------------------------|---------------------|
| Allocation speed | O(1) but memory grows continuously | O(n) traversing the list |
| Memory utilization | Cannot reuse, continuous waste | Freed memory can be reused |
| Implementation complexity | Minimal | Moderate (must correctly maintain the linked list) |
| Fragmentation control | No fragmentation (only grows) | External fragmentation exists, mitigated by coalescing |
| Real-world comparison | - | glibc uses segmented bins + coalescing, more complex |

## Advanced Challenges

- [ ] Implement **Best-Fit** strategy: traverse the entire list to find the smallest suitable block; compare fragmentation levels with First-Fit
- [ ] Implement **Buddy System**: allocate in powers of 2, only merge with "buddies" during coalescing
- [ ] Add `realloc(p, new_size)`: try in-place expansion; if it fails, re-allocate and copy
- [ ] Add **memory leak detection**: record call stacks during allocation; report unfreed blocks at program exit
- [ ] Add **thread safety** (once multi-threading is implemented): add a spinlock for the free list
