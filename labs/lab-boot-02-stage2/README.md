# Lab: Two-Stage Bootloader

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

xv6's bootloader (bootasm.S + bootmain.c) must accomplish everything within a single 512-byte sector: switch to protected mode, read disk, parse ELF, and load kernel segments. This space is extremely tight -- adding just a few more lines of code could cause an overflow.

Real PC bootloaders (GRUB, syslinux) are almost always multi-stage: the first stage is tiny (< 512 bytes) and only responsible for loading a larger second stage; the second stage then handles complex tasks (filesystem drivers, ELF parsing, user interface).

This lab splits xv6's boot process into two stages, where Stage1 does only one thing -- "load Stage2" -- and Stage2 freely handles ELF loading.

## Prerequisites

- **The 512-Byte Curse**: BIOS reads only the first sector (512 bytes) to `0x7C00`, and the last 2 bytes must be `0x55AA`
- **Disk Addressing**: xv6 uses LBA mode to read disks via IDE ports `0x1F0-0x1F7`
- **ELF Format**: The kernel is compiled as ELF; bootmain.c parses the ELF header and program headers to determine load addresses
- **Memory Layout**: bootasm.S is at `0x7C00`, kernel ELF header is read to `0x10000`, kernel code is loaded to `0x100000`

### Current vs Target Architecture

```
Current (single-stage):
  BIOS -> [bootasm + bootmain = 510 bytes] -> kernel

Target (two-stage):
  BIOS -> [stage1: bootasm + mini-bootmain = 510 bytes]
                    | reads disk sectors 2~N
              [stage2: bootstage2.c = no size limit]
                    | parses ELF, loads kernel segments
              kernel
```

## Lab Tasks

### 1. Slim Down bootmain.c into Stage1 (Modify boot/bootmain.c)

Stage1's sole responsibility: load Stage2 from disk into memory, then jump to it.

**What to do**:
- Keep `waitdisk`, `readsect`, and `readseg` disk I/O functions unchanged
- Replace `bootmain()` with a minimal version: call `readseg` to load Stage2, then jump

**Key Parameters**:
- Stage2 load address: `0x10000` (or choose another non-conflicting address)
- Stage2 sector count: must be coordinated with the `dd seek` parameter in Makefile
- Disk sector offset: Stage2 starts at sector 1 (sector 0 is Stage1 itself)

### 2. Create Stage2 Full ELF Loader (New file boot/bootstage2.c)

Stage2 has no size limit. It performs all the work of the original bootmain.c: parse ELF, load segments, jump to kernel.

**What to do**:
- Copy disk I/O functions from original bootmain.c (waitdisk, readsect, readseg)
- Implement `bootstage2()` function: read ELF header -> iterate program headers -> load each LOAD segment -> jump to entry

**Note**: Stage2's ELF load offset differs from the original -- the kernel's starting position on disk is shifted by the number of sectors occupied by Stage2. Define a `KERNEL_DISK_OFFSET` constant for the kernel's starting sector number.

**Memory Conflict**: Stage2 itself is loaded at `0x10000`, and reading the ELF header also needs to go to some address. Choose a non-conflicting address (e.g., `0x90000`) as temporary storage for the ELF header.

### 3. Modify the Build Process (Modify Makefile)

**Rules to add**:
- Compile bootstage2.c as a standalone binary (link address must match load address)
- Use `dd` to assemble the disk image: Stage1 (sector 0) + Stage2 (sectors 1~N) + kernel (sectors N+1~)

**Disk Layout**:

```
Sector 0      : stage1 (bootblock, 512 bytes, 0x55AA signed)
Sector 1 ~ N  : stage2 (N * 512 bytes, N determined at compile time)
Sector N+1 ~  : kernel (ELF format)
```

**Stage1-to-Stage2 Parameter Passing** (optional):
- Compile-time: Makefile passes offsets via `-D` macros
- Runtime: Stage1 writes offsets to an agreed memory address (e.g., `0x7000`) before jumping

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Multi-stage Boot | Stage1 loads Stage2, Stage2 loads the kernel, similar to GRUB |
| 512-Byte Limit | BIOS hard constraint; Stage1 must fit in one sector |
| Disk Addressing (LBA) | IDE ports 0x1F0-0x1F7, direct sector number addressing |
| ELF Loading | Parse ELF header -> iterate program headers -> load segments |
| Memory Conflicts | Stage1 at 0x7C00, Stage2 at 0x10000, kernel at 0x100000 |
| Build System | dd assembles multiple binaries into disk image, seek controls offset |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| boot/bootmain.c | Modify | Slim down to only load Stage2 |
| boot/bootstage2.c | **New** | Stage2 full ELF loader |
| Makefile | Modify | Add Stage2 build rules, modify disk image layout |

## Verification

### Build and Run

```bash
make clean && make
ls -l build/bootblock   # Must be <= 510 bytes
ls -l build/stage2       # No size limit, but should be < 16KB
make qemu-nox
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| Stage1 size compliant | bootblock <= 510 bytes | `ls -l build/bootblock` |
| Stage2 loads correctly | Correctly read from disk and executed | GDB: `break *0x10000` |
| Kernel loads correctly | Kernel read from correct disk offset | `hexdump` to check xv6.img |
| System boots normally | Shell is available | Type commands in xv6 |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Black screen, no output | Stage1 failed to load Stage2 correctly | Check disk offset and dd seek parameters |
| Triple fault loop | Stage2 jump address is wrong | Check linker script `-Ttext` address |
| Kernel load failure | KERNEL_DISK_OFFSET is incorrect | Ensure it matches the dd seek parameter |
| Stage2 entry crash | Stage2 address conflicts with ELF header | Use a different address for temporary ELF header storage |

## Key Code Paths

- Stage1 loading: boot/bootmain.c -> readseg loads Stage2 -> jump to Stage2 entry
- Stage2 ELF parsing: boot/bootstage2.c -> read ELF header -> iterate program headers
- Segment loading: boot/bootstage2.c -> readseg + stosb to zero BSS
- Control transfer: boot/bootstage2.c -> jump to ELF entry point

## Design Trade-offs

| Aspect | Single-Stage (Original) | Two-Stage (This Lab) |
|--------|------------------------|---------------------|
| Code space | Extremely tight (510 bytes) | Stage2 has no limit |
| Complexity | Simple | Adds Stage2 coordination |
| Boot speed | Fast (direct load) | Slightly slower (extra disk reads) |
| Extensibility | Poor | Good (Stage2 can add menus, etc.) |

## Advanced Challenges

- [ ] Add a boot menu in Stage2: choose which kernel to load
- [ ] Implement Stage1-to-Stage2 parameter passing (disk geometry info)
- [ ] Make Stage2 support loading kernel from a filesystem (not raw sectors)
- [ ] Research GRUB Stage1.5 design: why is the "1.5" stage needed
