# Lab: LRU Buffer Cache

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

The xv6 buffer cache (`src/bio.c`) uses a fixed number of `NBUF=30` buffer blocks. When all buffers are occupied, the `bget` function must evict a "least recently used" block. However, the original implementation only uses a **linear scan** to find any block with `refcnt=0`, without truly implementing an LRU policy — which block gets evicted depends on its position in the array, not its usage time.

This lab transforms the buffer list into a **strict LRU doubly-linked list**:

- **Most recently used** blocks are near the head of the list (MRU end)
- **Least recently used** blocks are at the tail of the list (LRU end)
- After each `bget` hit, move the block to the head of the list
- When evicting, take directly from the tail of the list

Core question: *"Why is LRU a good cache replacement policy? Where else in an OS is LRU replacement used?"*

## Prerequisites

- **How `bio.c` works**: `bread(dev, blockno)` searches the cache (returns on hit), or on miss evicts a block and reads from disk. `brelse(b)` releases the reference to a block (`refcnt--`), making it eligible for eviction
- **`bcache.head`**: In the original code, buffers form a doubly circular linked list (`buf.next`/`buf.prev`), with head as the sentinel node
- **LRU invariant**: At all times, blocks near the head are the most recently accessed, and blocks near the tail (`head.prev`) are the least recently accessed
- **Two phases of `bget`**: First search the list (hit), then evict an available block from the tail end on miss

```
LRU list illustration (MRU → LRU):
head ↔ [Block A (newest)] ↔ [Block B] ↔ [Block C] ↔ [Block D (oldest)] ↔ head

Hit on Block B: move B after head
head ↔ [Block B] ↔ [Block A] ↔ [Block C] ↔ [Block D] ↔ head

Eviction (miss): search from tail (D's predecessor) direction for refcnt==0
```

## Lab Tasks

### 1. Understand and diagram the original list structure (src/bio.c)

Before making changes, read `binit`, `bget`, and `brelse` in `bio.c`, and draw:
- The initial state of the `bcache.head` doubly circular linked list
- The traversal direction of `bget`'s two loops (hit search, eviction search) in the list
- How `brelse` reinserts a block into the list

**Question**: Does the original code implement strict LRU? Does eviction select the "least recently used" or the "first refcnt=0"?

### 2. Move blocks to the MRU end on bget hit (src/bio.c)

In the hit path of `bget` (found matching dev/blockno), add a "move to head" operation:

```c
// After finding hit block b, move it after head (MRU position)
b->prev->next = b->next;
b->next->prev = b->prev;
// Insert after head
b->next = bcache.head.next;
b->prev = &bcache.head;
bcache.head.next->prev = b;
bcache.head.next = b;
```

**Note**: The move operation must be performed under `bcache.lock` protection (already held in `bget`).

### 3. Search from the LRU end during eviction in bget (src/bio.c)

The original `bget` eviction loop traverses the list **forward** (from head onward). Change it to traverse **backward** (from tail back), finding the least recently used block with `refcnt==0`:

```c
// Eviction: search from LRU end (tail) toward MRU end
for(b = bcache.head.prev; b != &bcache.head; b = b->prev) {
    if(b->refcnt == 0) {
        // Evict this block
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // Move to MRU end
        b->prev->next = b->next;
        b->next->prev = b->prev;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
    }
}
```

### 4. Move blocks to the MRU end in brelse (src/bio.c)

When `brelse` decrements `refcnt` to 0, the block becomes eligible for eviction again. To keep recently released blocks on the MRU side of the LRU list (indicating it was "recently used"), also perform a "move after head" operation in the `refcnt==0` branch of `brelse`:

```c
if(b->refcnt == 0) {
    // Move to MRU end, indicating recently used
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
}
```

### 5. Add hit rate statistics (optional, for verification)

Add `hit` and `miss` counters to the `bcache` structure, increment them on `bget` hit/miss respectively, and print statistics in kernel output.

### 6. Write tests to verify LRU effectiveness (user/lrucachetest.c)

Design an access pattern where LRU hit rate is significantly better than random replacement:
- Sequentially access 20 files (hot), then access a cold block, then access the hot blocks again
- Measure disk I/O count under LRU (via miss counter), compare with random replacement

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| Buffer cache | bio.c: kernel-maintained in-memory cache of disk blocks, reducing disk I/O |
| LRU replacement policy | Least recently used blocks are evicted first, based on temporal locality |
| Doubly-linked list maintenance | List order adjusted on every hit/release, O(1) operations |
| Reference counting | Blocks with `refcnt > 0` are in use and cannot be evicted |
| Temporal locality | Recently accessed blocks are likely to be accessed again soon (theoretical basis for LRU) |
| Lock granularity | `bcache.lock` protects the entire cache; consider sharding for high concurrency |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| src/bio.c | Modify | `bget` moves to MRU on hit, evicts from LRU end; `brelse` moves to MRU end |
| include/buf.h | Modify (optional) | Add `hit`/`miss` counters to the `bcache` structure |
| user/lrucachetest.c | New | Test program verifying LRU hit rate |
| Makefile | Modify | Add `lrucachetest` |

## Verification

```bash
make clean && make qemu-nox
$ lrucachetest
$ usertests
```

### Verification Goals

| Goal | Expected Behavior | How to Observe |
|------|------------------|----------------|
| LRU order correct | Evicts least recently used block (list tail end) | Print evicted blockno in bget |
| Hit rate improvement | LRU has higher hit rate than original implementation | hit/miss counter comparison |
| Concurrency safety | No crashes with CPUS=2 | `make qemu-nox CPUS=2` + usertests |
| usertests passes | File system functionality unaffected | All usertests PASS |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| bget panic "no buffers" | Eviction loop direction wrong, skipping refcnt=0 blocks | Confirm backward traversal: `b = bcache.head.prev` |
| List corruption causing infinite loop | Incorrect order of pointer modifications during move | Strictly follow: disconnect old links first, then insert at new position |
| Hit rate not improved | Forgot to move block to MRU end on bget hit | Check that hit branch has the move operation |
| Deadlock | Did not release bcache.lock before acquiresleep | Check lock operation order in bget |

## Key Code Paths

- Cache hit: `bio.c:bget()` → found dev/blockno → move to MRU end → return
- Cache miss: `bget()` → search backward from LRU end for refcnt=0 → evict → move to MRU end → return
- Release buffer: `bio.c:brelse()` → `refcnt--` → if 0 → move to MRU end

## Design Trade-offs

| Aspect | Original (unordered list) | LRU doubly-linked list |
|--------|--------------------------|----------------------|
| Hit operation | None (no order adjustment) | Move to MRU end (O(1) list operation) |
| Eviction policy | Any refcnt=0 | Least recently used (strict LRU) |
| Hit rate | Depends on initialization order | Significantly higher than random replacement (locality principle) |
| Implementation complexity | Simple | Slightly more complex (must correctly maintain doubly-linked list order) |
| Concurrency scaling | Single global lock | Single global lock (can further split into hash + bucket locks) |

## Advanced Challenges

- [ ] Implement **hash buckets + per-bucket locks**: shard bcache by `blockno % NBUCKETS` to reduce lock contention
- [ ] Implement **Clock algorithm** (approximate LRU): maintain a referenced bit per block, scan clockwise for replacement
- [ ] Compare hit rates of LRU vs. Clock under different access patterns (sequential, random, Zipf distribution)
- [ ] Implement **write-back caching**: don't write to disk immediately on `brelse`, only write on eviction (mind crash consistency)
- [ ] Research the **2Q algorithm** (used in Linux page cache): distinguish "first access" from "multiple accesses" to prevent large sequential reads from evicting hot data
