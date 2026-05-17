# Lab: Graphical Boot Screen

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★★☆

## Motivation

After booting, xv6 only has text -- the 80x25 VGA text mode. But PC graphics cards support graphical modes where you can draw pixels directly. Modern operating systems (Windows, macOS) have graphical interfaces and progress animations during boot, all rendered before the kernel fully starts.

This lab switches to VBE graphical mode during the bootloader stage and draws pixels directly on the framebuffer to implement a boot screen with a progress bar.

This lab connects two worlds: **real-mode BIOS interrupt capabilities** (setting graphical mode) and **protected-mode direct memory writing** (drawing on the framebuffer).

## Prerequisites

- **VBE (VESA BIOS Extensions)**: Standard VGA only supports 320x200x256 colors; VBE extends to higher resolutions. Set via BIOS `int 0x10` with `AX=0x4F02`
- **BIOS Interrupts**: Only callable in real mode (16-bit). bootasm.S can call them before switching to protected mode
- **Framebuffer**: The pixel array mapped to physical memory by the graphics card. Typical address around `0xE0000000`, 3-4 bytes per pixel (RGB)
- **Page Mapping**: After xv6 enables paging, the framebuffer physical address must be mapped to a virtual address for continued access

### Execution Timeline

```
bootasm.S (real mode):       call int 0x10 to switch graphical mode -> save framebuffer address -> switch to protected mode
bootmain.c (protected mode, no paging):  read disk -> draw progress bar on framebuffer -> jump to entry.S
entry.S (protected mode, paging enabled):  map framebuffer physical address to virtual address
kernel/console.c:            output functions rewritten to write to framebuffer
```

## Lab Tasks

### 1. Set VBE Graphical Mode in Real Mode (Modify boot/bootasm.S)

Before switching to protected mode, call BIOS `int 0x10` to set graphical mode.

**What to do**:
- Call `int 0x10, AX=0x4F01` to get info about the specified VBE mode, store result at `0x8000`
- Extract framebuffer physical address from mode info at offset `0x28`, save to an agreed location (e.g., `0x7000`)
- Save resolution (XResolution, YResolution) to `0x7004` and `0x7006`
- Call `int 0x10, AX=0x4F02` to set graphical mode (bit 14 in BX set means use linear framebuffer)

**VBE Mode Number Reference** (may vary by QEMU version):
- `0x143`: 1024x768x32
- `0x118`: 1024x768x24
- `0x112`: 640x480x24 (safest fallback)

### 2. Draw Boot Screen in bootmain.c (Modify boot/bootmain.c)

**Functions to implement**:
- `fb_init()` -- read framebuffer address and resolution from agreed address
- `fb_pixel(x, y, r, g, b)` -- draw a pixel at the specified coordinates (4 bytes per pixel in 32bpp mode)
- `fb_rect(x, y, w, h, r, g, b)` -- draw a filled rectangle
- `fb_progress(percent)` -- draw a progress bar (gray background + blue fill)

**Key Interface**: Framebuffer base address obtained from `0x7000`, pitch = width * 4 (32bpp), pixel address = base + y * pitch + x * 4.

### 3. Map Framebuffer in entry.S (Modify kernel/entry.S)

The framebuffer physical address (e.g., `0xE0000000`) cannot be directly accessed after paging is enabled.

**Two approaches**:
- Add a 4MB large page mapping in `entrypgdir` (simple but inflexible)
- Use `mappages()` to establish 4KB mappings after `kvmalloc()` completes (more precise)

### 4. Modify console.c for Framebuffer Support (Modify kernel/console.c)

This is the largest change -- switching the output target from VGA text mode (0xB8000) to the framebuffer.

**What to implement**:
- Bitmap font renderer: use 8x16 font data, drawing each character pixel by pixel
- Text cursor tracking (cons_col, cons_row)
- Framebuffer scrolling (shift content up by one line)

**Hint**: You can use the PC BIOS built-in font data, or define a minimal ASCII glyph array yourself.

## OS Concepts

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| BIOS Interrupt Services | `int 0x10` only available in real mode |
| VBE/VESA | Abstraction layer for real graphics card drivers |
| Framebuffer | Linear mapping of video memory; writing pixels = writing memory |
| Physical Address vs Virtual Address | Framebuffer mapping differs before and after paging |
| Page Table Mapping | 4MB large page vs 4KB fine-grained mapping |
| Bitmap Font | Pure CPU text rendering without GPU dependency |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| boot/bootasm.S | Modify | VBE mode setup (real mode part) |
| boot/bootmain.c | Modify | Framebuffer drawing functions |
| kernel/entry.S | Modify | entrypgdir maps framebuffer |
| kernel/main.c | Modify | Pass framebuffer information |
| kernel/console.c | Modify | Output switched to framebuffer |
| Makefile | Possibly modify | If font data files are needed |

## Verification

### Build and Run

```bash
# Ensure QEMUOPTS in Makefile has -vga std
make qemu
```

### Verification Targets

| Target | Expected Behavior | How to Observe |
|--------|------------------|----------------|
| VBE mode switch | QEMU window resolution changes | Window size changes from 80x25 to 1024x768 |
| Framebuffer address | Valid address saved at 0x7000 | GDB inspect 0x7000 |
| Progress bar drawn | Blue progress bar from left to right | Watch QEMU window |
| Text rendering | Text displayed in graphical mode | Console output works normally |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| QEMU display unchanged | VBE mode number incorrect | Try 0x112 (640x480) |
| Triple fault | Invalid framebuffer address | Check address saved at 0x7000 |
| Corrupted display after paging | entrypgdir does not map framebuffer | Add mapping in entry.S |
| Garbled text | Font data incorrect | Check font_bitmap array |
| Poor performance | Pixel-by-pixel drawing too slow | Consider writing one line at a time |

## Key Code Paths

- VBE setup: boot/bootasm.S -> real mode int 0x10 -> address/resolution stored at 0x7000
- Framebuffer init: boot/bootmain.c -> read parameters from 0x7000
- Pixel drawing: boot/bootmain.c -> write directly to framebuffer memory
- Page table mapping: kernel/entry.S or kernel/vm.c -> map framebuffer physical address
- Text rendering: kernel/console.c -> bitmap font pixel-by-pixel drawing

## Design Trade-offs

| Aspect | VGA Text Mode (Original) | VBE Graphical Mode (This Lab) |
|--------|--------------------------|-------------------------------|
| Resolution | 80x25 characters | 1024x768+ pixels |
| Colors | 16 colors | 24/32-bit true color |
| Font | Hardware built-in | Requires software rendering |
| Complexity | Minimal | Requires font, mapping, scrolling |
| Boot speed | Fast | Slower (CPU renders text) |

## Advanced Challenges

- [ ] Implement double buffering: draw to a memory buffer first, then copy to framebuffer in one go to eliminate flicker
- [ ] Use Bresenham's line algorithm to draw lines and create an xv6 logo
- [ ] Display actual disk loading progress (update progress bar per sector)
- [ ] Research why the Linux kernel needs the fbcon (framebuffer console) subsystem
