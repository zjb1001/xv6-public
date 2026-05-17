# Lab: Crash Consistency Experiment

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★★

## Motivation

A file system must guarantee that even if power is lost at any moment, the file system is in a consistent state after reboot — either the operation completed entirely, or it did not happen at all. It must never be in an intermediate state.

xv6 implements crash consistency through a **Write-Ahead Log (WAL)**: `begin_op`/`end_op` wrap all file system modifications, `commit()` first writes modifications to the log area, then writes to the actual disk locations. On reboot, the log is checked and replayed.

This lab is a **destructive experiment**:

1. **Analyze** at which points in time the logging mechanism (`src/log.c`) is safe to crash, and at which points a crash would cause inconsistency
2. **Inject** artificial crashes (call `panic()` at critical points in `commit()`)
3. **Observe** and analyze inconsistent disk states
4. **Verify** that log recovery correctly restores the disk to a consistent state

Core question: *"Why is writing twice (write log + write data) safer than writing only once?"*

## Prerequisites

- **xv6 log structure**: In the disk layout, the log area follows the superblock, containing a log header (logheader) + log blocks. See `struct logheader` in `include/fs.h` and the full implementation in `src/log.c`
- **Four steps of `commit()`**: 1. `write_log()` (write modified blocks to disk log area) → 2. `write_head()` (write log header to disk, marking the commit point) → 3. `install_trans()` (copy from log area to actual locations) → 4. Clear log header
- **Commit Point**: The instant `write_head()` completes is the sole "atomicity guarantee point". Crash before: no log on reboot, no replay; crash after: log found on reboot, replay completes
- **`recover_from_log()`**: Called by `initlog()` at system startup, checks the log header, and replays any incomplete commits

```
commit() four-step timeline and crash safety:
Step 1: write_log()   ← Crash: log header not written → no commit → no replay on reboot → safe
Step 2: write_head()  ← Crash: atomic header write → commit point
Step 3: install_trans ← Crash: log header exists → replay on reboot → safe
Step 4: Clear header  ← Crash: data already written → replay is idempotent → safe
```

## Lab Tasks

### 1. Read and annotate the logging code (src/log.c)

Before starting the experiment, read `log.c` line by line and answer:

- What does `log.lh.n` record? When is its value modified?
- Why does `write_head()` only need a single `bwrite` to guarantee atomicity?
- What is the `recovering` parameter in `install_trans` used for?

### 2. Scenario analysis: crash point safety

For each of the four crash points below, analyze what state the disk would be in and whether it can recover on reboot:

| Crash Point | Completed | Not Completed | Reboot Behavior | Disk State |
|-------------|-----------|---------------|-----------------|------------|
| A: after write_log, before write_head | Log data written | Log header not written | No commit, no replay | ? |
| B: after write_head, before install_trans | Log header written | Data not installed | Commit found, replay | ? |
| C: during install_trans (partial write) | Some data written | Some not written | Commit found, replay all | ? |
| D: after install_trans, before header clear | All data written | Header not cleared | Commit found, replay (idempotent) | ? |

**Question**: Which scenario represents the "inconsistency window"? How does the logging eliminate this window?

### 3. Inject panic at crash point B, observe disk

In `src/log.c:commit()`, insert immediately after `write_head()` completes:

```c
static void commit() {
    if(log.lh.n > 0) {
        write_log();
        write_head();    // Commit point
        // === Inject crash ===
        // Uncomment the following line to trigger B-point crash:
        // panic("crash-experiment: B point");
        install_trans(0);
        log.lh.n = 0;
        write_head();
    }
}
```

**Observation steps**:

```bash
# 1. Start xv6, perform a file operation (e.g., echo hello > test.txt), crash occurs
make qemu-nox
# After crash is triggered, QEMU exits
# 2. Use qemu-img or a custom tool to inspect the disk image
# 3. Restart xv6 (without injecting crash), observe recover_from_log output
```

### 4. Remove logging: direct write, observe inconsistency

Modify the `commit()` call in `end_op()` to call `install_trans` directly without writing the log header (i.e., skip `write_head()`, bypassing the atomicity guarantee):

```c
// Dangerous experiment: bypass logging, write data directly (for study only, never for production)
static void dangerous_commit() {
    if(log.lh.n > 0) {
        write_log();
        // Intentionally skip write_head()!
        install_trans(0);   // Write log data directly to actual locations
        log.lh.n = 0;
        // Panic at any intermediate point
    }
}
```

- Inject `panic` in the middle of `install_trans`
- Restart xv6, observe whether the file system shows inconsistency (e.g., inode allocated but directory entry not written → orphan inode)

### 5. Write a disk image checking tool (tools/fscheck.c)

A simple fsck tool that runs on the host machine, reading `build/fs.img`:

```c
// Check contents:
// 1. Whether superblock inode count is consistent with bitmap
// 2. Whether each inode's nlink matches the number of directory references to that inode
// 3. Whether the log header (logheader) has any incomplete commits
```

**Hint**: Refer to how `tools/mkfs.c` reads the disk format.

### 6. Verify correctness of log replay

For the crash point B scenario:

1. Trigger crash → QEMU exits
2. Do not modify the disk image, restart xv6 directly
3. Add `cprintf` output in `recover_from_log` to confirm it is called and successfully replays
4. Verify that the file operation before the crash completed fully (or did not happen at all)

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| Write-ahead log (WAL) | Write log area first, then write data area; commit point atomicity |
| Crash consistency | Power loss at any moment leaves FS in consistent state after reboot |
| Idempotency | Log replay can safely execute multiple times with the same result |
| Atomicity | `write_head()` is the single atomic operation (single block write) |
| Transaction | Operations wrapped in `begin_op`/`end_op` form one transaction |
| fsck | File system consistency checker; logging makes most checks unnecessary |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| src/log.c | Modify (experimental) | Insert panic at critical points in `commit()`, restore after experiment |
| tools/fscheck.c | New | Host-side disk consistency checking tool |
| Makefile | Modify (optional) | Add `fscheck` build target |

## Verification

### Crash recovery verification flow

```bash
# 1. Inject crash point B in commit()
# 2. Start xv6, execute:
$ echo "hello" > test.txt     # Trigger file system operation → panic
# QEMU exits
# 3. Restart (do not modify image)
make qemu-nox
# Observe boot log: should see recover_from_log successfully replay
# 4. Verify:
$ cat test.txt                # Expected: hello (log replay succeeded)
```

### Inconsistency verification flow (dangerous experiment)

```bash
# 1. Bypass logging (dangerous_commit), panic in the middle of install_trans
# 2. Restart xv6
# 3. ls or cat → may see: orphan inodes, corrupted directories, inconsistent bitmap
# 4. Run fscheck to verify inconsistency
```

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| After panic injection, QEMU restarts instead of exiting | QEMU configured to reboot | Add `-no-reboot` to QEMU parameters in Makefile |
| recover_from_log not called | Log header was not written at crash time (crash point A) | This is expected behavior; use crash point B instead |
| fscheck reads disk format incorrectly | Endianness or struct alignment differs from host | Use `__attribute__((packed))` |

## Key Code Paths

- Transaction start: `src/log.c:begin_op()` → wait for log space → increment `log.outstanding`
- Record modification: `src/log.c:log_write(b)` → add buf to log block set
- Commit transaction: `src/log.c:end_op()` → outstanding-- → if 0 call `commit()`
- Commit flow: `commit()` → `write_log` → `write_head` (**commit point**) → `install_trans` → clear header
- Crash recovery: `src/log.c:recover_from_log()` → read log header → `install_trans(1)` → clear header

## Design Trade-offs

| Aspect | No logging (direct write) | WAL logging |
|--------|--------------------------|-------------|
| Crash consistency | No guarantee (arbitrary inconsistency) | Guaranteed (write twice for atomicity) |
| Write amplification | 1x (write data only) | 2x (write log first, then data) |
| Recovery time | Requires fsck (potentially slow) | O(log size) (fixed, fast) |
| Implementation complexity | Simple | Medium (begin_op/commit/recover) |
| Write throughput | High | Lower (log becomes bottleneck) |

## Advanced Challenges

- [ ] Implement **group commit**: accumulate multiple transactions and commit once, reducing write amplification
- [ ] Implement **checksum verification**: record per-log-block checksums in the log header to prevent corrupted logs from being replayed
- [ ] Read and compare **ext4 journaling modes**: differences between data=writeback, data=ordered, and data=journal
- [ ] Implement **log performance statistics**: count blocks written per `commit()`, observe write amplification factor
- [ ] Design a **crash testing framework**: randomly inject power loss in QEMU, verify automatic file system recovery
