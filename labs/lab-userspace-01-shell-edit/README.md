# Lab: Interactive Shell Line Editor (Tab + History)

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Design Motivation

The default xv6 shell reads one line with a minimal `gets` implementation: users can type and backspace, but there is no command history or completion.

This lab turns the shell into a small interactive line editor with two practical features:

- `Tab` auto-completion for command/file name prefixes
- `Up` / `Down` keys to navigate command history

Core question: *"How can we build user-friendly shell interaction while respecting xv6's console line discipline?"*

## Prerequisites

- Understand the shell loop in `user/sh.c`: print prompt -> read line -> parse -> execute
- Know how terminal arrow keys are encoded as escape sequences (`ESC [ A`, `ESC [ B`)
- Understand xv6 keyboard key codes (`KEY_UP`, `KEY_DN`) produced by `kernel/kbd.c`
- Understand xv6 console buffering (`input.r/w/e`) in `kernel/console.c`
- Understand xv6 user-space file APIs (`open`, `read`, `close`, `struct dirent`)
- Be familiar with safe fixed-size buffer editing (cursor index, null terminator, bounds)

## Lab Tasks

### 1. Locate the input path and replace `gets` usage (user/sh.c)

In `main`, the shell currently uses `gets(buf, nbuf)`.

Implement a custom line reader (for example `readline`) that reads one byte at a time from fd 0 and handles control keys directly.

Constraints:
- Keep behavior compatible with xv6 prompt style (`$ `)
- Return `-1` on EOF / read failure to keep exit behavior unchanged

### 2. Fix console wakeup granularity (kernel/console.c)

This is the most commonly missed point in this lab.

By default, xv6 console input wakes readers mainly on newline/EOF. In that mode, user-space `readline` cannot receive arrow/tab events immediately.

Adjust `consoleintr` so readers can be woken on each input character (character-wise wakeup), while still preserving console stability.

### 3. Add a fixed-size history ring buffer (user/sh.c)

### 4. Add a fixed-size history ring buffer (user/sh.c)

Add an in-memory history store, e.g.:

```c
#define HIST_MAX 16
#define HIST_LINE_MAX 100
```

Track:
- total history count
- current browse index when user presses Up/Down

Rules:
- Do not push empty commands
- Consecutive duplicate commands may be skipped (optional)
- Keep only latest `HIST_MAX` commands

### 5. Parse arrow keys (user/sh.c)

Handle both forms:
- xv6 key codes: `KEY_UP`, `KEY_DN`
- optional terminal escape sequences: `ESC [ A`, `ESC [ B`

Reason:
- In xv6 QEMU console, arrow keys often arrive as `KEY_UP/KEY_DN` single-byte values rather than ANSI escape sequences.

Historical three-byte sequences:
- `ESC [ A`: Up (older command)
- `ESC [ B`: Down (newer command)

When switching history entries:
- Replace the whole editing buffer with selected command
- Redraw the line so terminal output matches internal buffer

Hint:
- A simple redraw strategy is fine: print `\r`, prompt, clear tail by spaces, then print current buffer

### 6. Implement Tab prefix completion (user/sh.c)

For current token prefix (usually last token in line):
- Match shell built-ins (`cd`, `exit`)
- Match executables/files in current directory (`.`)

Behavior suggestion:
- Exactly 1 match: complete in place and append a space
- More than 1 match: print candidates on next line, then redraw prompt + current line
- 0 match: do nothing

Constraints:
- Never overflow line buffer
- Keep implementation purely in user space (no kernel changes)

### 7. Keep existing editing behavior (user/sh.c)

Ensure these still work after changes:
- Normal printable characters
- Backspace (`\b` / DEL)
- Enter (`\n`) submits command

Optional:
- `Ctrl-U` clear whole line

### 8. Validate with manual interaction scenarios

Use scripted checklist and interactive test in xv6:
- Repeat command, then Up/Down navigation
- Prefix + Tab for unique and multi-match cases
- Mixed typing + history switch + execution

## OS Concepts Covered

| Concept | How It Appears in This Lab |
|---------|---------------------------|
| Console line discipline | `kernel/console.c` controls wakeup timing and what user-space can read |
| Keyboard scan-code mapping | Arrow keys can arrive as `KEY_UP/KEY_DN`, not just `ESC [ A/B` |
| User-space terminal handling | Parse raw byte stream from stdin, including control sequences |
| Line discipline (simplified) | Basic editing, history browse, redraw logic |
| Ring buffer | Bounded command history with overwrite of old entries |
| Prefix matching | Token extraction + candidate lookup |
| UI state machine | Key events drive transitions of input buffer state |

## Files Involved

| File | Change Type | Description |
|------|-------------|-------------|
| kernel/console.c | Modified | Character-wise input wakeup for interactive key handling |
| user/sh.c | Modified | Replace `gets` with interactive line editor; add history and tab completion |
| user/user.h (optional) | Modified | Add helper declarations only if you extract reusable helpers |
| lab-Tests/lab-userspace-01-shell-edit/console.c | Added | Patch file for reproducible console behavior |
| lab-Tests/lab-userspace-01-shell-edit/keyecho.c | Added | Optional debug tool to inspect key input bytes |
| lab-Tests/lab-userspace-01-shell-edit/Makefile | Added | One-command build/run workflow for this lab |

## Verification

### Build and Run

```bash
cd lab-Tests/lab-userspace-01-shell-edit
make start
```

Inside xv6 shell:

```bash
# optional: inspect key sequences
keyecho

# then manually validate shell enhancements
# (type commands, use Tab, Up, Down)
```

Quit QEMU with `Ctrl-A X`, then:

```bash
make exit
```

### Verification Goals

| Goal | Expected Behavior | How to Observe |
|------|-------------------|----------------|
| Up/Down history | Up shows older commands; Down returns to newer/current line | Interactive shell session |
| Unique tab completion | Prefix expands to full command/file + trailing space | Type prefix + Tab |
| Multi-match tab completion | Candidate list shown, prompt line restored | Type short prefix + Tab |
| Buffer safety | No crash or garbage with long input / repeated keys | Stress typing and repeats |
| Compatibility | Existing commands and parser behavior remain valid | Run `ls`, `echo`, pipelines |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Up/Down prints `^[` characters | Escape sequence parsing incomplete | Read and classify `ESC`, then `[` and final code |
| Up/Down never trigger before Enter | Console still line-buffered (wakeup on newline only) | Patch `kernel/console.c` to wake readers per character |
| Up/Down not recognized in xv6 QEMU | Code only handles `ESC [ A/B`, but keyboard emits `KEY_UP/KEY_DN` | Support both key forms in `readline` |
| History replaces line incorrectly | Redraw does not clear old tail | Print spaces to clear line before reprint |
| Tab completion corrupts buffer | Prefix boundaries or length checks are wrong | Compute token start carefully; enforce max length |
| Shell freezes after key combos | Blocking reads mishandled during ESC parsing | Only read extra bytes after confirming first byte is `ESC` |

## Key Code Paths

- `user/sh.c:main()` -> prompt -> `readline()` -> `parsecmd()` -> `runcmd()`
- `kernel/console.c:consoleintr()` -> commit+wakeup input bytes -> `consoleread()`
- `readline()` -> byte dispatch: printable / backspace / enter / tab / escape
- `readline()` -> history browse -> redraw line
- `readline()` -> tab completion -> update buffer -> redraw line

## Design Trade-offs

| Aspect | Original shell | This lab |
|--------|----------------|----------|
| Console wakeup | mostly line-oriented | character-oriented for interactive editor |
| Input model | `gets` line read | Event-driven byte parser |
| History | None | Fixed-size ring buffer |
| Completion | None | Prefix-based (built-ins + cwd entries) |
| Complexity | Very low | Medium, but still user-space only |
| Kernel changes | None | `console.c` wakeup behavior adjusted |

## Advanced Challenges

- [ ] Support left/right cursor movement (`ESC [ C` / `ESC [ D`)
- [ ] Add history prefix search (`Ctrl-R` style simplified)
- [ ] Persist history to a `.sh_history` file
- [ ] Add completion for absolute/relative paths with `/`
- [ ] Rank completion candidates by recent usage
