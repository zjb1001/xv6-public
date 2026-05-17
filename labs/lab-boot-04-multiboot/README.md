# Lab: Multiboot-Compatible Kernel

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

xv6's boot process relies on its own custom bootloader (bootasm.S + bootmain.c). But in the real world, almost nobody writes their own bootloader -- everyone uses GRUB.

GRUB is the standard bootloader for Linux distributions and already handles all the complex tasks: recognizing filesystems, parsing configuration files, displaying boot menus, and loading kernels. The kernel only needs to comply with the **Multiboot Specification** -- a "contract" between the bootloader and the kernel.

Interestingly, xv6's `entry.S` **already contains a Multiboot header**, but `flags = 0` requests no information, and the Makefile does not build a Multiboot-compatible image. This lab brings that header to life, enabling xv6 to be loaded directly by GRUB.

Core question: **What is the "interface" between the kernel and the bootloader?**

## Prerequisites

- **Multiboot Specification**: The kernel embeds a header with magic number `0x1BADB002` within the first 8KB of the ELF file; after GRUB recognizes it, it loads the kernel directly and jumps to it
- **GRUB's Role**: GRUB reads the kernel ELF from disk into memory, switches to 32-bit protected mode, and jumps to the entry point. At this point EAX = `0x2BADB002` (magic), EBX = physical address of the multiboot_info structure, A20 is enabled, paging is disabled, and interrupts are disabled
- **multiboot_info Structure**: The information packet GRUB passes to the kernel, containing memory map, command line arguments, boot device, etc.

### Current Boot vs Multiboot Boot

```
Current (self-bootstrapping):
  BIOS -> bootasm.S -> bootmain.c -> entry.S -> main()

Multiboot (GRUB-loaded):
  BIOS -> GRUB -> entry.S (GRUB has already done protected mode switch and ELF loading)
  (bootasm.S and bootmain.c are completely skipped!)
```

## Lab Tasks

### 1. Activate the Multiboot Header (Modify kernel/entry.S)

The current Multiboot header in entry.S has `flags = 0`, requesting no information.

**What to do**: Change flags to `0x01 | 0x04` (request memory info and boot device info), and update the checksum (`-(magic + flags)`) so the three .long values sum to 0.

### 2. Define Multiboot Information Structures (New file include/multiboot.h)

**Structures to define**:
- `struct multiboot_info` -- contains flags, mem_lower/mem_upper, mmap_addr/mmap_length, etc.
- `struct multiboot_mmap_entry` -- memory map entry (size, addr, len, type)
- `MULTIBOOT_MAGIC` constant (`0x2BADB002`)
- Flag bit masks: `MBI_MEM_INFO`, `MBI_MMAP`, `MBI_LOADER`, etc.

**Reference**: [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)

### 3. Modify entry.S to Receive Multiboot Info (Modify kernel/entry.S)

**Key Design**: entry.S must be compatible with both boot methods.

- Compare EAX with `0x2BADB002`: if it matches, save EBX (multiboot_info physical address) to an agreed variable
- Regardless of which method, continue with normal paging setup and jump to main()
- Declare `multiboot_info_ptr` variable using `.comm`

**Note**: EBX must be saved before enabling paging (the physical address in it requires P2V conversion after paging is enabled).

### 4. Parse Multiboot Information (Modify kernel/main.c)

**Implement `parse_multiboot()`**:
- Check if `multiboot_info_ptr` is non-zero (distinguish Multiboot from self-bootstrap boot)
- After P2V conversion, read flags to determine which information is available
- Print loader name, memory size, and memory map (if available)

Call it in `main()` after `kvmalloc()` (virtual address mapping is required).

### 5. Create GRUB Boot Image (Modify Makefile)

**Build targets to add**:
- `grub-qemu` -- boot xv6 with GRUB ISO
- `xv6.iso` -- create ISO containing kernel + grub.cfg using `grub-mkrescue`
- `grub.cfg` -- configure GRUB to load xv6 kernel with `multiboot` command

**Dependencies**: Requires `grub-common` and `xorriso` installed on the system.

### 6. Support Both Boot Methods

Core design: `multiboot_info_ptr` being 0 indicates self-bootstrap boot (original behavior), non-zero indicates GRUB boot. Both entry.S and main.c branch through this variable.

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Multiboot Specification | Standard interface protocol between bootloader and kernel |
| ELF Loading | GRUB parses ELF and loads segments to specified physical addresses |
| Dual Boot Path | Kernel detects boot source, compatible with multiple bootloaders |
| Boot Information Passing | GRUB passes info via registers (EAX/EBX) and memory structures |
| Memory Map | GRUB provides E820-equivalent memory map |
| ISO Image | grub-mkrescue creates El Torito bootable ISO |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| kernel/entry.S | Modify | Activate Multiboot header, save EBX |
| include/multiboot.h | **New** | Multiboot information structure definitions |
| kernel/main.c | Modify | Add `parse_multiboot()` |
| Makefile | Modify | Add GRUB ISO build target |

## Verification

### Method 1: Regression Testing (Original Bootloader)

```bash
make clean && make qemu-nox
```

Should behave exactly the same as the original. `multiboot_info_ptr` should be 0.

### Method 2: GRUB ISO Boot

```bash
make grub-qemu
```

### Method 3: QEMU -kernel Direct Loading

```bash
qemu-system-i386 -kernel build/kernel -drive file=build/fs.img,index=1,media=disk,format=raw -m 128 -nographic
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| Dual-path compatibility | Both original boot and GRUB can start | Run each method separately |
| Multiboot detection | EAX=0x2BADB002 when GRUB boots | parse_multiboot prints loader info |
| Memory map obtained | GRUB provides E820-equivalent data | Print memory map entries |
| Information correctness | mem_upper approximately equals QEMU memory size | 128MB -> mem_upper ~ 130048KB |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| GRUB reports "not multiboot compliant" | Header not in first 8KB of ELF | Check header position in entry.S |
| Triple fault after boot | entry.S address references wrong when loaded by GRUB | Check CR3 setup |
| multiboot_info_ptr is zero | EBX save location overwritten | Ensure saving before enabling paging |
| GRUB ISO creation fails | Missing grub-mkrescue or xorriso | `sudo apt install grub-common xorriso` |
| Original boot fails | entry.S changes broke the bootmain path | Ensure non-Multiboot jump path is correct |

## Key Code Paths

- GRUB detection: kernel/entry.S -> compare EAX with 0x2BADB002
- Information saving: kernel/entry.S -> EBX stored to multiboot_info_ptr
- Information parsing: kernel/main.c -> read flags/memory map after P2V conversion
- Dual-path entry: kernel/entry.S -> both paths converge at paging setup

## Design Trade-offs

| Aspect | Custom Bootloader (Original) | GRUB Multiboot (This Lab) |
|--------|------------------------------|--------------------------|
| Complexity | Need to write own 512-byte bootloader | Only need to add a header |
| Filesystem | Not supported; reads raw sectors | GRUB supports ext2/fat/iso9660 |
| Boot Menu | None | GRUB provides full menu |
| Memory Info | Need to call E820 yourself | GRUB provides automatically |
| Dependencies | No external dependencies | Requires GRUB installed |

## Advanced Challenges

- [ ] Pass arguments to kernel via GRUB command line (e.g., `xv6 maxproc=128`), parse in main.c
- [ ] Use GRUB module mechanism to pass fs.img as a module to the kernel
- [ ] Implement Multiboot 2 specification (supports EFI)
- [ ] Research Linux's `startup_32`: how Linux detects Multiboot and parses information

## Further Reading

- [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
- [OSDev Wiki: Multiboot](https://wiki.osdev.org/Multiboot)
