# Lab: Large File Support (Doubly Indirect Blocks)

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

The xv6 inode structure (`include/fs.h`) supports 12 direct blocks + 1 indirect block (128 block pointers):

```
Max file size = (12 + 128) × 512 = 71,680 bytes ≈ 70KB
```

This means xv6 cannot store files larger than 70KB — not even a typical image. Real Unix file systems (UFS, ext2) use **doubly indirect blocks**: one block stores pointers to indirect blocks, each of which in turn stores pointers to data blocks, cascading two levels.

This lab extends xv6's maximum file size to:

```
12 (direct) + 128 (single indirect) + 128×128 (doubly indirect) = 16,396 blocks ≈ 8MB
```

Core question: *"The inode has a fixed size. How do limited pointers to disk block addresses support large files?"*

## Prerequisites

- **xv6 inode structure**: `struct dinode` in `include/fs.h`, `addrs[NDIRECT+1]` stores block numbers, the last one is the indirect block pointer
- **Indirect addressing**: The indirect block itself does not store data; its contents are an array of data block numbers (`uint addrs[NINDIRECT]`, NINDIRECT=128)
- **`bmap(ip, bn)` function**: In `kernel/fs.c`, converts a file's logical block number `bn` to a physical disk block number; the core of file system addressing
- **`itrunc(ip)` function**: Frees all data blocks when truncating a file; must recursively free indirect blocks

```
Original inode address structure:
addrs[0..11]  → Direct blocks (12)
addrs[12]     → Single indirect block → [addr0, addr1, ..., addr127]

After extension:
addrs[0..11]  → Direct blocks (12)
addrs[12]     → Single indirect block → [addr0..addr127]
addrs[13]     → Doubly indirect block → [iblk0, iblk1, ..., iblk127]
                                  each iblk → [addr0..addr127]
```

## Lab Tasks

### 1. Modify inode constants (include/fs.h)

```c
#define NDIRECT   12
#define NINDIRECT (BSIZE / sizeof(uint))           // = 128
#define NDINDIRECT (NINDIRECT * NINDIRECT)          // = 16384 (new)
#define MAXFILE   (NDIRECT + NINDIRECT + NDINDIRECT) // = 16524

// dinode's addrs expands from NDIRECT+1 to NDIRECT+2
struct dinode {
    // ...
    uint addrs[NDIRECT + 2];   // was NDIRECT+1
};
```

**Synchronize** the `addrs` field size in the in-memory inode `struct inode` in `kernel/file.h`.

### 2. Modify bmap: handle doubly indirect blocks (kernel/fs.c)

`bmap(ip, bn)` currently handles direct blocks and single indirect blocks. Add doubly indirect handling at the end:

```c
// Doubly indirect block handling
bn -= NINDIRECT;
if(bn < NDINDIRECT) {
    // Level 1: find doubly indirect block from addrs[NDIRECT+1]
    // Level 2: find the corresponding indirect block within the doubly indirect block
    // Level 3: find the data block within the indirect block
    uint idx1 = bn / NINDIRECT;    // indirect block index within doubly indirect block
    uint idx2 = bn % NINDIRECT;    // data block index within indirect block
    // If intermediate blocks don't exist, allocate with balloc and zero them
    // ...
    return addr;
}
panic("bmap: out of range");
```

**Hint**: You need two `bread`/`brelse` operations — first to read the doubly indirect block, second to read the indirect block. Use `log_write` to mark blocks as dirty when modifying.

### 3. Modify itrunc: recursively free doubly indirect blocks (kernel/fs.c)

`itrunc` currently frees direct blocks and single indirect blocks. Add doubly indirect block freeing logic at the end:

```c
// Free doubly indirect block
if(ip->addrs[NDIRECT+1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    for(j = 0; j < NINDIRECT; j++) {
        if(((uint*)bp->data)[j]) {
            // Free all data blocks in each indirect block
            struct buf *bp2 = bread(ip->dev, ((uint*)bp->data)[j]);
            for(k = 0; k < NINDIRECT; k++) {
                if(((uint*)bp2->data)[k])
                    bfree(ip->dev, ((uint*)bp2->data)[k]);
            }
            brelse(bp2);
            bfree(ip->dev, ((uint*)bp->data)[j]); // Free indirect block
        }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);         // Free doubly indirect block
    ip->addrs[NDIRECT+1] = 0;
}
```

### 4. Update mkfs and disk size (tools/mkfs.c / Makefile)

- `MAXFILE` and inode structure definitions in `mkfs.c` need to be synchronized
- The disk image size may need to be increased (adjust disk size in `QEMUOPTS` in Makefile)

### 5. Write test program (user/bigfiletest.c)

```c
// Test 1: Write a file slightly larger than the original maximum size
// Test 2: Write a file close to the new maximum size (8MB)
// Test 3: Seek + read to verify data integrity
// Test 4: Large file truncation (itrunc) correctly frees disk blocks
```

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| Indirect addressing | inode's `addrs` stores pointers to pointers, two-level cascading addressing |
| Disk block numbers | All addresses are logical block numbers; `balloc`/`bfree` manage the free bitmap |
| Buffer cache | `bread` reads blocks, `bwrite`/`log_write` writes blocks, `brelse` releases |
| Write-ahead log (WAL) | Modifying metadata blocks must go through `log_write` for crash safety |
| File system consistency | itrunc must completely free all levels of blocks, preventing disk block leaks |
| mkfs | Disk formatting tool; must stay consistent with the kernel's inode format |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| include/fs.h | Modify | Add `NDINDIRECT`, modify `MAXFILE`, `addrs[NDIRECT+2]` |
| kernel/file.h | Modify | Synchronize in-memory inode's `addrs` size |
| kernel/fs.c | Modify | Add doubly indirect handling in `bmap`, add doubly indirect freeing in `itrunc` |
| tools/mkfs.c | Modify | Synchronize `MAXFILE` and inode structure |
| user/bigfiletest.c | New | Large file read/write test |
| Makefile | Modify | Add `bigfiletest` |

## Verification

```bash
make clean && make qemu-nox
$ bigfiletest
$ usertests     # Confirm existing file system functionality is not broken
```

### Verification Goals

| Goal | Expected Behavior | How to Observe |
|------|------------------|----------------|
| Write exceeds 70KB successfully | Writing an 80KB file does not return an error | bigfiletest output |
| Read-write consistency | seek + read content matches write | bigfiletest data verification |
| itrunc correctness | Disk space freed after deleting large file | Check df or bitmap manually after deletion |
| usertests passes | Existing functionality unaffected | All usertests PASS |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| bmap panic "out of range" | MAXFILE or bmap boundary calculation error | Carefully check the position of `bn -= NINDIRECT` |
| Data read-write inconsistency | bmap returns wrong physical block number | Debug idx1/idx2 with cprintf in bmap |
| Disk block leak after itrunc | Doubly indirect block not fully freed | Check that the three-level freeing logic is complete |
| mkfs compilation error | mkfs.c out of sync with fs.h | Confirm NDIRECT/addrs in mkfs.c matches fs.h |

## Key Code Paths

- Writing a large file: `sysfile.c:sys_write()` → `filewrite()` → `writei()` → `bmap(bn)` → allocate blocks
- Doubly indirect addressing: `fs.c:bmap()` → compute idx1/idx2 → `bread` doubly indirect block → `bread` indirect block → return data block number
- Freeing a large file: `sys_unlink()` → `iput()` → `itrunc()` → three-level loop bfree

## Design Trade-offs

| Aspect | Original (12+1-level indirect) | Extended (12+1+2-level indirect) |
|--------|-------------------------------|----------------------------------|
| Max file size | ~70KB | ~8MB |
| Normal file access | At most 1 bread (indirect block) | Unchanged (direct block path unchanged) |
| Large file access | Not supported | Extra 1 bread (doubly indirect path) |
| inode disk structure | addrs[13] | addrs[14] (4 more bytes) |
| Implementation complexity | Simple | Medium (two-level loop) |

## Advanced Challenges

- [ ] Add **triply indirect blocks** to extend max file size to 1GB
- [ ] Implement **block count in `stat` output** (`blocks` field in `struct stat`)
- [ ] Measure random read **access latency** for large files (compare I/O count: direct blocks vs. single indirect vs. doubly indirect)
- [ ] Research how ext2 uses the same principle to support 2TB files, compare inode structure differences
