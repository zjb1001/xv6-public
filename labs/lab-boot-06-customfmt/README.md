# Lab: Custom Kernel Image Format

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★★

## Motivation

xv6's `bootmain.c` loads the kernel using the standard ELF format. ELF is the executable file standard for Unix systems -- flexible and versatile, but overly complex for a teaching kernel. The ELF header is 52 bytes, each program header is 32 bytes, supporting dynamic linking, section tables, symbol tables, and other features that xv6 never uses.

This lab designs a **minimal kernel image format** to replace ELF, letting students understand from scratch the essence of "executable file formats": **they are a contract between the bootloader and the linker**.

After completing this lab, you will understand why every ELF field exists (because you need to design replacements yourself), how linker scripts control output format, and that "loading a kernel" is simply "moving data from a file to specified positions in memory."

## Prerequisites

- **ELF Format Structure**: xv6 kernel ELF contains an ELF header (52B) + 2 program headers (32B each) + 2 LOAD segments (.text and .data/.bss)
- **Linker Script (kernel.ld)**: Controls segment virtual addresses, load addresses, and alignment
- **bootmain.c ELF Loading Logic**: Read ELF header -> iterate program headers -> readseg + stosb to zero BSS
- **Build Toolchain**: `gcc` compile -> `ld` link -> `objcopy` format conversion -> custom tool to generate image

### Target Build Flow

```
kernel/*.c -> gcc -c -> *.o -> ld -T kernel.ld -> kernel.elf (ELF)
                                                      |
                                               mkkernel (custom tool)
                                                      |
                                                kernel.bin (custom format)
                                                      |
                                               dd -> xv6.img
```

## Lab Tasks

### 1. Analyze Current Kernel ELF Structure

Use `readelf -l build/kernel` or `objdump -l build/kernel` to see which LOAD segments the kernel ELF actually has.

Typically there are only **2 LOAD segments**: .text (code + read-only data) and .data (data + BSS). xv6 does not need dynamic linking or symbol table loading -- most ELF fields are wasted.

### 2. Design Custom Format XKIF (xv6 Kernel Image Format)

**Structures to design**:
- `xkif_header` -- magic number, version, entry address, segment count, checksum, etc.
- `xkif_segment` -- physical address, file size, memory size, file offset for each segment

**Design Decisions**:
- How large should the header be? Which fields are needed? Which ELF fields does xv6 actually need?
- How large should segment descriptors be? How much can be trimmed compared to ELF program headers (32B)?
- Is a checksum needed? What algorithm (summation, CRC32)?
- Data alignment strategy: align to 512-byte sectors or not?

### 3. Implement mkkernel Conversion Tool (New file tools/mkkernel.c)

**Functionality**: Read ELF kernel -> extract LOAD segment info -> generate XKIF format file.

**Key Steps**:
- Verify ELF magic
- Iterate program headers, only extracting segments with `type == LOAD`
- Convert ELF entry point to physical address (note: in xv6, `_start = V2P_WO(entry)`, so ELF entry is already a physical address)
- Recalculate segment data offsets within the XKIF file
- Write header + segment descriptors + segment data

**Entry Point Physical Address Issue**: The ELF entry field stores `_start`, which is set to `V2P_WO(entry)` (a physical address) by the linker script. In XKIF, you should **explicitly** record the physical address, eliminating this implicit "coincidence."

### 4. Modify bootmain.c to Parse XKIF Format (Modify boot/bootmain.c)

**What to implement**:
- Define XKIF format structures (consistent with those in mkkernel.c)
- Replace ELF parsing logic: read XKIF header -> verify magic -> read segment descriptors -> load each segment
- Keep waitdisk, readsect, readseg unchanged

**Benefit**: bootmain.c becomes smaller -- XKIF parsing is more direct than ELF, freeing up space for other features.

### 5. Modify the Build Process (Modify Makefile)

**Rules to adjust**:
- Link stage generates `kernel.elf` (intermediate product, no longer used directly)
- Add mkkernel compilation rule
- Kernel build changes to: `kernel.elf -> mkkernel -> kernel (XKIF)`
- Disk image dd rules remain unchanged

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Executable File Format | The format is a contract between the loader and the linker |
| ELF Format | Understand why each field exists by designing a replacement |
| Linker Script | kernel.ld controls segment virtual addresses, load addresses, and alignment |
| Build Toolchain | Complete pipeline of gcc -> ld -> custom tool -> dd |
| Magic Number | Format identification, same role as ELF's `0x7F454C46` |
| Checksum | Detect disk data corruption |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| tools/mkkernel.c | **New** | ELF -> XKIF conversion tool |
| boot/bootmain.c | Modify | Change from ELF loader to XKIF loader |
| Makefile | Modify | Add mkkernel build rule |
| kernel.ld | Possibly modify | If segment layout adjustment is needed |

## Verification

### Build

```bash
make clean && make
```

### Verify mkkernel Output

```bash
xxd -l 4 build/kernel     # Check XKIF magic number
hexdump -C build/kernel | head -20   # View header structure
```

### Run

```bash
make qemu-nox
```

Should boot normally with behavior identical to the original ELF version.

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| XKIF magic correct | File header has custom magic number | `xxd -l 4 build/kernel` |
| Segment count correct | Typically nsegments = 2 | hexdump to view header |
| Kernel loads normally | bootmain parses and jumps successfully | `make qemu-nox` boots normally |
| BSS zeroed correctly | memsz > filesz portion zeroed | No garbage data in kernel data segment |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| bootmain parse failure | Segment offset calculation incorrect | Check mkkernel offset calculation |
| Kernel crash after jump | entry_point is virtual address, not physical | Confirm physical address semantics of ELF entry |
| Segment data misaligned | File alignment and read offset mismatch | Ensure both sides use the same alignment rules |
| Some segments not loaded | Non-LOAD segments were counted | Only count `type == LOAD` |

## Key Code Paths

- ELF analysis: tools/mkkernel.c -> read ELF header, extract LOAD segments
- Format conversion: tools/mkkernel.c -> build XKIF header + segment descriptors, rewrite offsets
- Magic verification: boot/bootmain.c -> compare custom magic number
- Segment loading: boot/bootmain.c -> iterate segment descriptors, readseg + stosb
- Build pipeline: Makefile -> kernel.elf -> mkkernel -> kernel (XKIF) -> dd -> xv6.img

## Design Trade-offs

| Aspect | ELF (Original) | XKIF (This Lab) |
|--------|----------------|-----------------|
| Universality | Industry standard | xv6-specific |
| Complexity | header 52B + phdr 32B/segment | Self-designed |
| Debugging tools | readelf, objdump fully supported | Need to write your own |
| Understanding depth | "Using" ELF | "Designing" a format -> deeper understanding |

## Advanced Challenges

- [ ] Add compression support: decompress in bootmain (combine with multi-stage bootloader lab for more space)
- [ ] Implement multi-kernel support: header includes kernel name, bootmain displays boot menu
- [ ] Support both ELF and XKIF formats: bootmain detects magic number and auto-selects
- [ ] Research Linux's bzImage format: kernel image with embedded decompression code and setup data
- [ ] Research UEFI's PE/COFF format: why UEFI requires PE format instead of ELF

## Further Reading

- [ELF Format Specification](https://refspecs.linuxbase.org/elf/elf.pdf)
- [OSDev Wiki: Executable Formats](https://wiki.osdev.org/Executable_Formats)
