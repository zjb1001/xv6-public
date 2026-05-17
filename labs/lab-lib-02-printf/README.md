# Lab: Printf Formatting Engine

[中文](i18n/zh-CN/README.md)

Difficulty: ★★☆☆☆

## Motivation

xv6's `printf` is a minimal kernel version that only supports `%d`, `%x`, `%s`, `%c`, and writes directly to the console file descriptor — no buffering, no width alignment, no floating-point support. The `x6_fprintf` skeleton in `lib/xv6_stdio.h` currently depends on `x6_printf` and does not yet implement complete format string parsing.

This lab implements a fully C-standard-compliant `x6_fprintf` from scratch:

- **Variadic arguments**: Understand the x86 stack frame layout behind `va_list`/`va_arg`
- **Format parsing**: Implement `%d`, `%u`, `%s`, `%x`, `%p`, `%c`, `%%`, as well as width and left-alignment `%-10s`
- **Buffered output**: Batch writes through `X6_FILE`'s buffer to reduce the number of system calls

Core question: *"How does printf know how many arguments were passed and what type each one is?"*

## Prerequisites

- **Calling convention (x86 cdecl)**: Arguments are pushed onto the stack from right to left; `va_list` is essentially a pointer on the stack, and `va_arg` moves toward higher addresses by the type size
- **C standard format strings**: `%[flags][width][.precision]type`, flags include `-` (left-align), `0` (zero-pad), `+` (force sign)
- **Integer-to-string conversion**: Requires a temporary buffer filled in reverse order, since division starts from the least significant digit

```
va_list principle (x86 cdecl):
Stack when calling f("%d %s", 42, "hello"):
  High address: "hello" pointer  ← va_arg 3rd call reads here
                 42              ← va_arg 2nd call reads here
                 "%d %s" pointer ← format string argument (fmt)
  Low address:  return address
va_list ap points to the first argument position after fmt
```

## Lab Tasks

### 1. Understand and Integrate the Variadic Mechanism (lib/xv6_stdio.c)

The xv6 user library already includes a simplified version of `<stdarg.h>`. Verify that the following macros work correctly:

```c
void x6_fprintf(X6_FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    // ... parse fmt, use va_arg(ap, int) / va_arg(ap, char*) to get arguments
    va_end(ap);
}
```

**Hint**: `va_arg(ap, int)` retrieves an int and advances the pointer; `char` and `short` are promoted to `int` when passed, so do not write `va_arg(ap, char)`.

### 2. Implement the Format String Parsing Main Loop

```c
static void do_fmt(X6_FILE *f, const char *fmt, va_list ap)
```

State machine structure:

```
Iterate over each character in fmt:
  Normal character → write directly to output buffer
  '%'             → enter format parsing mode:
    Parse flags: '-' '0' '+'
    Parse width: consecutive digits
    Parse type:  d / u / s / x / p / c / %
    Fetch argument by type, format it, and write to buffer
```

**Format specifiers to implement**:

| Specifier | Description | Example |
|-----------|-------------|---------|
| `%d` | Signed decimal integer | `42` -> `"42"`, `-1` -> `"-1"` |
| `%u` | Unsigned decimal integer | `0xFFFFFFFF` -> `"4294967295"` |
| `%x` | Hexadecimal (lowercase) | `255` -> `"ff"` |
| `%p` | Pointer (hexadecimal + prefix) | -> `"0x80012340"` |
| `%s` | String | `"hello"` |
| `%c` | Single character | `'A'` -> `"A"` |
| `%%` | Output literal `%` | |

### 3. Implement Width and Alignment

Parse the optional width field (e.g., `%10d`, `%-10s`, `%08x`):

- `width`: Minimum output width; pad with spaces on the left if shorter (or `0` if the `0` flag is present)
- `-` flag: Left-align, pad with spaces on the right
- Width does not truncate: if actual output length > width, output at actual length

**Implementation hint**: First format the number/string into a temporary `char buf[32]`, calculate its length, then decide how to pad.

### 4. Integrate Buffered Writing

All character output goes through `x6_fputc(f, c)`, utilizing the existing `X6_FILE` buffer. Call `x6_fflush(f)` at the end of the function to ensure flushing.

Implement `x6_printf(fmt, ...)` as a convenience wrapper for `x6_fprintf(x6_stdout, fmt, ...)`.

### 5. Verification Tests (user/printftest.c)

```c
x6_printf("%d\n", 42);                  // "42"
x6_printf("%-10s|\n", "left");          // "left      |"
x6_printf("%010d\n", 42);               // "0000000042"
x6_printf("%x %p\n", 255, &i);         // "ff 0x..."
x6_printf("%d %d %d\n", 1, 2, 3);      // "1 2 3"
```

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| Calling Convention | `va_list` directly manipulates the x86 cdecl stack frame |
| Buffered I/O | `X6_FILE` buffer reduces the number of `write` system calls |
| Formatting DSL | `printf` format strings are a mini-language; parsing uses a state machine |
| System call overhead | Performance difference between `write(1, buf, 1)` per character vs. batch writes |
| Type promotion | C variadic `char`/`short` are promoted to `int` per ABI rules |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| lib/xv6_stdio.c | Modify | Implement complete `x6_fprintf`/`do_fmt`, replace original skeleton |
| lib/xv6_stdio.h | Modify | Add `x6_printf`, `x6_fflush` declarations |
| user/printftest.c | New | Format output verification test |
| Makefile | Modify | Add `printftest` to `UPROGS` |

## Verification

### Build and Run

```bash
make clean && make qemu-nox
```

In the xv6 shell:

```
$ printftest
```

### Verification Goals

| Goal | Expected Output | How to Observe |
|------|----------------|----------------|
| Integer formatting | `x6_printf("%d", -123)` -> `-123` | printftest line-by-line comparison |
| Hexadecimal | `x6_printf("%x", 255)` -> `ff` | printftest output |
| Width alignment | `x6_printf("%10d", 42)` -> `        42` | Count spaces |
| Left alignment | `x6_printf("%-10d\|", 42)` -> `42        \|` | Pipe position |
| Zero padding | `x6_printf("%08x", 255)` -> `000000ff` | printftest output |
| Multiple arguments | `%d %s %x` three arguments without misalignment | printftest output |

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Argument misalignment/garbage | Wrong type for `va_arg`, e.g., `va_arg(ap, char)` | Use `int` for all integer types, `char*` or `void*` for pointers |
| Negative number prints as large positive | Mixing `%u` and `%d` | Use `int` for `%d`, `uint` for `%u`; pay attention to type when fetching arguments |
| Width alignment has too many/few spaces | String length calculation is off | Use `strlen` instead of manual calculation; handle uniformly |
| `%p` prefix missing | Forgot to output `0x` | Special-case `%p` to add prefix before hexadecimal output |
| Buffer not flushed | `x6_fputc` writes to buffer but does not flush | Call `x6_fflush` at function end or on `\n` |

## Key Code Paths

- Entry: `lib/xv6_stdio.c:x6_fprintf()` -> `va_start` -> `do_fmt()`
- Format parsing: `do_fmt()` -> state machine processes `fmt` character by character
- Integer to string: `do_fmt()` -> `int_to_buf()` -> fill temporary buffer in reverse
- Output character: `do_fmt()` -> `x6_fputc(f, c)` -> write to `X6_FILE` buffer
- Flush: `x6_fprintf()` -> `x6_fflush(f)` -> `write(f->fd, buf, len)`

## Design Trade-offs

| Aspect | Kernel printf (original) | User library x6_fprintf |
|--------|--------------------------|-------------------------|
| Output target | Fixed write to console fd | Any `X6_FILE*` (file/stderr/stdout) |
| Buffering | Unbuffered, one write per character | Buffered batch writes, fewer syscalls |
| Format support | `%d %x %s %c` | Adds `%u %p`, supports width and alignment |
| Floating point | Not supported | Not supported (xv6 has no FPU support) |
| Error handling | None | Returns number of bytes written; can detect I/O errors |

## Advanced Challenges

- [ ] Implement `%*d` (runtime width) and `%.*s` (precision truncation)
- [ ] Implement `x6_sprintf(char *buf, fmt, ...)`: output to a string instead of a file
- [ ] Implement `x6_snprintf`: bounded string output (prevents buffer overflow)
- [ ] Compare `write(1, "x", 1)` in a 10000-iteration loop vs. buffered single write; measure system call count difference
- [ ] Implement `%e`/`%f` floating-point formatting (requires enabling SSE floating point for xv6 first)
