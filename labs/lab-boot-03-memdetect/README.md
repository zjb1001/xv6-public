# Lab: Boot-Time Memory Detection

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6's physical memory range is hardcoded: `PHYSTOP = 0xE000000` (224MB), and `kinit1/kinit2` directly add all physical pages in the `[end, PHYSTOP)` range to the free list. This works well in QEMU because QEMU provides exactly 128MB or 256MB of memory.

But on real PCs, the physical memory layout is far from contiguous. ACPI, video ROM, ISA holes, and other regions occupy multiple ranges in the low address space. If the kernel blindly treats `[0, PHYSTOP)` as available memory, it will overwrite hardware-reserved regions and crash.

Real operating systems (Linux, Windows) query the available memory map at boot time through **BIOS interrupt `int 0x15/E820`**. This interrupt can only be called in **real mode** -- once switched to protected mode, BIOS can no longer be called.

This lab inserts memory detection code in bootasm.S before switching to protected mode, passes the results to the kernel, and lets `kalloc.c` initialize the free list based on the actual memory map.

## Prerequisites

- **BIOS int 0x15/E820**: The standard interface for querying system memory map in real mode. Each call returns one address range descriptor; iteration is controlled by the `CF` flag and `EBX` continuation value
- **Real Mode vs Protected Mode**: BIOS interrupts are only available in real mode (16-bit). Once bootasm.S sets `CR0.PE = 1`, BIOS can no longer be called
- **"Holes" in PC Memory Layout**:
  ```
  0x00000000 ~ 0x0009FFFF   640KB conventional memory (usable)
  0x000A0000 ~ 0x000BFFFF   VGA video memory (reserved)
  0x000C0000 ~ 0x000FFFFF   BIOS ROM / ISA hole (reserved)
  0x00100000 ~ ...           Extended memory (usable, but may have ACPI and other reserved ranges)
  ```
- **Bootloader-to-Kernel Data Passing**: There is no function call interface between bootloader and kernel; data can only be passed through agreed memory addresses or registers

## Lab Tasks

### 1. Understand the Current kalloc Hardcoding

Read `kernel/kalloc.c`'s `kinit1()` and `kinit2()`, and understand how they use `PHYSTOP` and `freerange`. Think: what happens if the actual physical memory is not 224MB?

### 2. Call E820 in bootasm.S (Modify boot/bootasm.S)

Before switching to protected mode (before `CR0.PE = 1`), insert the E820 detection loop.

**E820 Call Protocol**:
- Input: `EAX=0xE820`, `EBX=0` (first call), `ECX=buffer size`, `EDX=0x534D`('SM'), `ES:DI=buffer address`
- Output: `CF=0` for success, `EBX` continuation value (0 means done), buffer filled with one 24-byte entry

**What to Implement**:
- Loop calling `int 0x15`, advancing the DI pointer after each success, until EBX=0 or CF=1
- Save the entry count at an agreed location (e.g., `0x7FFE`), and entry data starting at buffer `0x8000`

**E820 Entry Structure** (24 bytes each):
- `+0`  uint32 addr_low / addr_high -- base address
- `+8`  uint32 len_low / len_high -- length
- `+16` uint32 type -- 1=usable, 2=reserved, 3=ACPI, 4=NVS, 5=defective

**Address Selection**: `0x8000` is below the bootloader and above the BDA; it is not used by any boot code.

### 3. Define Kernel-Side Data Structures (Modify include/memlayout.h)

**What to define**:
- `struct e820_entry` -- corresponding to the E820 return entry format
- `E820_MAP_ADDR`, `E820_COUNT_ADDR` -- agreed memory address constants
- `E820_TYPE_USABLE` and other type constants
- `get_e820_map()` / `get_e820_count()` -- helper functions that access via P2V

### 4. Modify kalloc.c to Use E820 Map (Modify kernel/kalloc.c)

**Implement `kinit_e820()`**:
- Iterate E820 entries, processing only those with `type == E820_TYPE_USABLE`
- Use P2V to convert physical addresses to virtual addresses
- After page alignment, skip the kernel code region `[end, ...)`, and call `freerange` for the remaining ranges

### 5. Modify main.c Initialization Sequence (Modify kernel/main.c)

Replace the original `kinit2()` call with `kinit_e820()`. Note: must be called after `kvmalloc()` because full virtual address mapping is required.

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| BIOS System Calls | `int 0x15/E820` is the standard interface for querying hardware info in real mode |
| Physical Memory Map | PC memory is not contiguously usable; VGA, ROM, ISA holes are reserved |
| Real Mode vs Protected Mode | BIOS interrupts only available in real mode; must complete in bootasm.S early stage |
| Bootloader-Kernel Protocol | Data passed via agreed memory addresses, similar to Linux's `boot_params` |
| Memory Allocator | kalloc goes from hardcoded range to dynamic range based on hardware detection |
| Page Alignment | Physical memory allocator only operates on page-aligned addresses |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| boot/bootasm.S | Modify | Insert E820 detection code before protected mode switch |
| include/memlayout.h | Modify | Add E820 data structures and address constants |
| kernel/kalloc.c | Modify | Add `kinit_e820()` |
| kernel/main.c | Modify | Replace `kinit2()` with `kinit_e820()` |

## Verification

### Build and Run

```bash
make clean && make qemu-nox
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| E820 detection succeeds | Returns multiple entries | Print function outputs entry count and content |
| Available memory correct | Only type=1 added to free list | kinit_e820 logs |
| Reserved regions skipped | VGA/ROM/ISA holes not allocated | No crash under different memory sizes |
| Multiple memory sizes supported | Auto-adapts to 64MB/128MB/256MB | Test with `QEMUEXTRA="-m 64"` |

### Multiple Memory Size Testing

```bash
make qemu-nox QEMUEXTRA="-m 256"
make qemu-nox QEMUEXTRA="-m 64"
```

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| E820 returns 0 entries | Call timing is wrong (already in protected mode) | Ensure execution before `CR0.PE=1` |
| Data is all zeros | Buffer address is overwritten | Check if 0x8000 conflicts with entryother |
| kinit panic | E820 range includes kernel code | Skip the `[end, ...)` region |
| bootblock oversized | E820 assembly code too large | Compact encoding, place before A20 |

## Key Code Paths

- E820 detection: boot/bootasm.S -> real mode `int 0x15` loop call
- Data storage: boot/bootasm.S -> entries written to 0x8000, count stored at 0x7FFE
- Data reading: include/memlayout.h -> access after P2V conversion
- Memory initialization: kernel/kalloc.c -> iterate E820 entries, only type=1 calls freerange

## Design Trade-offs

| Aspect | Hardcoded PHYSTOP (Original) | E820 Dynamic Detection (This Lab) |
|--------|------------------------------|----------------------------------|
| Portability | Only works for fixed memory size in QEMU | Works on any PC |
| Complexity | Minimal | Requires BIOS call + data passing |
| Safety | May corrupt hardware on real PCs | Correctly skips reserved regions |
| Flexibility | Changing memory size requires recompilation | Auto-adapts |

## Advanced Challenges

- [ ] Add magic number validation at the E820 buffer header to prevent reading uninitialized data
- [ ] Support `int 0x15/E801` and `int 0x15/88` as fallback alternatives to E820
- [ ] Calculate total available memory and print the total memory size at boot
- [ ] Research Linux's `BOOT_PARAMS` structure: it passes far more than just E820
