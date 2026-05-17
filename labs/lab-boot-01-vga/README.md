# Lab: VGA Boot Messages

[中文](i18n/zh-CN/README.md)

Difficulty: ★☆☆☆☆

## Motivation

From the moment xv6 jumps from BIOS into bootasm.S, through bootmain.c loading the kernel, to entry.S jumping to main(), the screen is completely black -- there is no visual feedback at all. Only after console initialization completes does the user see `init: starting sh`.

Real PCs provide feedback during boot: BIOS POST messages, GRUB's "Loading" text, Linux's penguin logo. This lab writes directly to the VGA text buffer in bootmain.c, making every stage of the boot process visible.

Core question: *"How do you print to the screen before the kernel is even running?"*

## Prerequisites

- **VGA Text Mode**: After boot, the PC defaults to 80x25 text mode, with video memory mapped at physical address `0xB8000`. Each character occupies 2 bytes: the low byte is the ASCII code, the high byte is the color attribute
- **Real Mode / Protected Mode**: bootasm.S has already switched to protected mode, but the physical address 0xB8000 is still directly accessible (no paging during the bootloader stage)
- **Color Attribute Byte**: `high 4 bits = background color | low 4 bits = foreground color`, e.g., `0x07` = gray background with white text, `0x02` = black background with green text, `0x0C` = black background with bright red text

```
VGA Color Table:
0=Black 1=Blue 2=Green 3=Cyan 4=Red 5=Magenta 6=Brown 7=Gray
8=Dark Gray 9=Light Blue A=Light Green B=Light Cyan C=Light Red D=Light Magenta E=Yellow F=White
```

## Lab Tasks

### 1. Implement VGA Output Functions (boot/bootmain.c)

Implement two basic functions in bootmain.c:

- `vgaputc(char c, uchar color)` -- Write a character at the cursor position and advance the cursor. Handle `\n` for newlines and screen scrolling (when cursor exceeds 80x25, shift content up by one line)
- `vgaprint(char *s, uchar color)` -- Iterate through the string, calling vgaputc for each character

**Key Interface**: VGA text buffer starts at address `0xB8000`, typed as `ushort*`, where the low byte of each element is the character and the high byte is the color attribute.

**Hint**:
- Use a static variable `cursor` to track the current position (row * 80 + col)
- Scrolling: copy the first 24 lines to the previous 24 lines, clear the 25th line with spaces

### 2. Insert Print Statements at Each Stage in bootmain()

Call vgaprint at key points in `bootmain()`, using different colors for different stages:

| Location | Suggested Color | Content |
|----------|----------------|---------|
| Function entry | 0x07 (gray-white) | Bootloader title |
| Before ELF magic check | 0x02 (green) | Disk read status |
| ELF check failure | 0x0C (red) | Error message |
| Segment loading loop | 0x0B (light cyan) | Parsing status |
| Before jumping to entry | 0x0E (yellow) | Control transfer notice |

### 3. Constraint: 512-Byte Limit

bootmain.c + bootasm.S must be <= 510 bytes (bootloader hard limit). Adding strings and functions increases code size. Use `ls -l build/bootblock` to check the size. If over the limit, shorten strings or use abbreviations.

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Memory-Mapped I/O | VGA buffer is not a "port write" -- it's writing directly to memory address 0xB8000 |
| Real Mode vs Protected Mode | bootasm.S has switched to protected mode, but VGA address is still directly accessible |
| Bootloader Space Limit | 512-byte sector -> 510 bytes actually usable + 0x55AA signature |
| Linear Address Space | No segment offsets in protected mode -- an address is an address |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| boot/bootmain.c | Modify | Add VGA output functions and print at each stage |

## Verification

### Build and Run

```bash
make clean && make qemu
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| VGA output visible | Colored text appears during boot | Watch the QEMU window at boot |
| Color differentiation | Different colors at different stages | Gray title, green I/O, red errors |
| bootblock size | <= 510 bytes | `ls -l build/bootblock` |
| Console overwrite | VGA output is overwritten after console init | Normal behavior |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Compilation error from sign.pl | bootblock exceeds 510 bytes | Shorten strings, reduce functions |
| Garbled display | cursor initial value incorrect or overflow | Check cursor range [0, 80*25) |
| Cannot see output | Console initialization overwrites content | Normal -- can modify console.c to delay screen clear |
| Incorrect colors | Attribute byte high/low bits swapped | Check the order of `(color << 8) \| char` |

## Key Code Paths

- VGA buffer write: boot/bootmain.c -> write directly to `0xB8000 + cursor`
- Scroll handling: boot/bootmain.c -> line copy + last line clear
- Stage printing: boot/bootmain.c -> vgaprint inserted before/after readseg/ELF parsing

## Design Trade-offs

| Aspect | No Boot Output (Original) | VGA Printing (This Lab) |
|--------|--------------------------|------------------------|
| User experience | Completely dark boot process | Visual feedback at each stage |
| Code space | Full 510 bytes available | Need to reserve space for VGA functions and strings |
| Debugging difficulty | Can only see final result | Can see which stage it gets stuck at |

## Advanced Challenges

- [ ] Implement `vgaprintf`: support `%d` formatting to print the number of loaded sectors
- [ ] Print a message in bootasm.S during real mode (using BIOS int 0x10)
- [ ] Add a boot animation: rotating `|/-\` cursor using ASCII art
- [ ] Investigate why console initialization overwrites your output, and think about how to preserve it
