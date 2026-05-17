# Lab: Printf Formatting Engine (格式化输出引擎)

[English](../../README.md)

难度: ★★☆☆☆

## 设计初衷

xv6 的 `printf` 是一个精简的内核版本，只支持 `%d`、`%x`、`%s`、`%c`，且直接写 console 文件描述符，没有缓冲、没有宽度对齐、没有浮点数支持。`lib/xv6_stdio.h` 中的 `x6_fprintf` 骨架目前依赖 `x6_printf`，尚未实现完整的格式串解析。

本实验从零实现一个符合 C 标准行为的 `x6_fprintf`：

- **变参列表**: 理解 `va_list`/`va_arg` 背后的 x86 栈帧布局
- **格式解析**: 实现 `%d`、`%u`、`%s`、`%x`、`%p`、`%c`、`%%`，以及宽度和左对齐 `%-10s`
- **缓冲输出**: 通过 `X6_FILE` 的缓冲区批量写，减少系统调用次数

核心问题：*"printf 是怎么知道传入了多少个参数、每个参数是什么类型的？"*

## 前置知识

- **调用约定 (x86 cdecl)**: 参数从右到左压栈，`va_list` 本质是栈上的指针，`va_arg` 按类型大小向高地址移动
- **C 标准格式串**: `%[flags][width][.precision]type`，flags 包括 `-`（左对齐）、`0`（零填充）、`+`（强制符号）
- **整数到字符串转换**: 需要一个临时 buffer 逆序填充，因为除法运算从最低位开始

```
va_list 原理 (x86 cdecl):
调用 f("%d %s", 42, "hello") 时的栈:
  高地址: "hello"指针  ← va_arg 第三次调用取这里
           42          ← va_arg 第二次调用取这里
           "%d %s"指针 ← 格式串参数 (fmt)
  低地址: 返回地址
va_list ap 指向 fmt 之后的第一个参数位置
```

## 实验内容

### 1. 理解并集成变参机制 (lib/xv6_stdio.c)

xv6 用户库已经包含 `<stdarg.h>` 的简化版。验证以下宏可正常工作：

```c
void x6_fprintf(X6_FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    // ... 解析 fmt，用 va_arg(ap, int) / va_arg(ap, char*) 取参数
    va_end(ap);
}
```

**提示**: `va_arg(ap, int)` 取一个 int 并推进指针；`char` 和 `short` 会被提升为 `int` 传递，不要写 `va_arg(ap, char)`。

### 2. 实现格式串解析主循环

```c
static void do_fmt(X6_FILE *f, const char *fmt, va_list ap)
```

状态机结构：

```
遍历 fmt 每个字符:
  普通字符 → 直接写入输出缓冲区
  '%'      → 进入格式解析模式:
    解析 flags: '-' '0' '+'
    解析 width: 连续数字
    解析 type:  d / u / s / x / p / c / %
    按 type 取参数，格式化后写入缓冲区
```

**需要实现的格式说明符**:

| 格式符 | 说明 | 示例 |
|--------|------|------|
| `%d` | 有符号十进制整数 | `42` → `"42"`, `-1` → `"-1"` |
| `%u` | 无符号十进制整数 | `0xFFFFFFFF` → `"4294967295"` |
| `%x` | 十六进制（小写） | `255` → `"ff"` |
| `%p` | 指针（十六进制+前缀） | → `"0x80012340"` |
| `%s` | 字符串 | `"hello"` |
| `%c` | 单个字符 | `'A'` → `"A"` |
| `%%` | 输出 `%` 字面量 | |

### 3. 实现宽度和对齐

解析可选的宽度字段（如 `%10d`、`%-10s`、`%08x`）：

- `width`: 最小输出宽度，不足时在左侧填充空格（或 `0` 如果有 `0` flag）
- `-` flag: 左对齐，在右侧填充空格
- 宽度不截断：实际输出长度 > width 时按实际长度输出

**实现提示**: 先格式化数字/字符串到临时 char buf[32]，计算其长度，再决定如何填充。

### 4. 集成缓冲区写入

所有字符输出通过 `x6_fputc(f, c)` 写入，利用已有的 `X6_FILE` 缓冲区。函数末尾调用 `x6_fflush(f)` 确保刷新。

实现 `x6_printf(fmt, ...)` 为 `x6_fprintf(x6_stdout, fmt, ...)` 的简便包装。

### 5. 验证测试 (user/printftest.c)

```c
x6_printf("%d\n", 42);                  // "42"
x6_printf("%-10s|\n", "left");          // "left      |"
x6_printf("%010d\n", 42);               // "0000000042"
x6_printf("%x %p\n", 255, &i);         // "ff 0x..."
x6_printf("%d %d %d\n", 1, 2, 3);      // "1 2 3"
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 调用约定 (Calling Convention) | `va_list` 直接操作 x86 cdecl 栈帧 |
| 缓冲 I/O | `X6_FILE` 缓冲区减少 `write` 系统调用次数 |
| 格式化 DSL | `printf` 格式串是一个微型语言，解析其状态机 |
| 系统调用开销 | 每次 `write(1, buf, 1)` vs 批量写的性能差异 |
| 类型提升 | C 变参中 `char`/`short` 被提升为 `int` 的 ABI 规则 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| lib/xv6_stdio.c | 修改 | 实现完整的 `x6_fprintf`/`do_fmt`，替换原有骨架 |
| lib/xv6_stdio.h | 修改 | 添加 `x6_printf`、`x6_fflush` 声明 |
| user/printftest.c | 新增 | 格式化输出验证测试 |
| Makefile | 修改 | 添加 `printftest` 到 `UPROGS` |

## 验证

### 编译和运行

```bash
make clean && make qemu-nox
```

在 xv6 shell 中：

```
$ printftest
```

### 验证目标

| 目标 | 预期输出 | 观察方式 |
|------|---------|---------|
| 整数格式化 | `x6_printf("%d", -123)` → `-123` | printftest 逐行对比 |
| 十六进制 | `x6_printf("%x", 255)` → `ff` | printftest 输出 |
| 宽度对齐 | `x6_printf("%10d", 42)` → `        42` | 空格计数 |
| 左对齐 | `x6_printf("%-10d\|", 42)` → `42        \|` | 竖线位置 |
| 零填充 | `x6_printf("%08x", 255)` → `000000ff` | printftest 输出 |
| 多参数 | `%d %s %x` 三参数不错位 | printftest 输出 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 参数错位/乱码 | `va_arg` 用错类型，如 `va_arg(ap, char)` | 所有整型用 `int`，指针用 `char*` 或 `void*` |
| 负数打印成大正数 | `%u` 和 `%d` 混用 | `%d` 用 `int`，`%u` 用 `uint`，取参时注意类型 |
| 宽度对齐多/少空格 | 字符串长度计算有偏差 | 用 `strlen` 而非手算，统一处理 |
| `%p` 前缀缺失 | 忘记输出 `0x` | 在输出十六进制前特判 `%p` 加前缀 |
| 缓冲区不刷新 | `x6_fputc` 写入缓冲但未刷新 | 函数末尾或 `\n` 时调用 `x6_fflush` |

## 关键代码路径

- 入口: `lib/xv6_stdio.c:x6_fprintf()` → `va_start` → `do_fmt()`
- 格式解析: `do_fmt()` → 状态机逐字符处理 `fmt`
- 整数转字符串: `do_fmt()` → `int_to_buf()` → 逆序填充临时 buffer
- 输出字符: `do_fmt()` → `x6_fputc(f, c)` → 写入 `X6_FILE` 缓冲区
- 刷新: `x6_fprintf()` → `x6_fflush(f)` → `write(f->fd, buf, len)`

## 设计权衡

| 方面 | 内核 printf（原始） | 用户库 x6_fprintf |
|------|-------------------|--------------------|
| 输出目标 | 固定写 console fd | 任意 `X6_FILE*`（文件/stderr/stdout） |
| 缓冲 | 无缓冲，每字符一次 write | 缓冲批量写，减少 syscall |
| 格式支持 | `%d %x %s %c` | 增加 `%u %p`，支持宽度和对齐 |
| 浮点数 | 不支持 | 不支持（xv6 无 FPU 支持） |
| 错误处理 | 无 | 返回写入字节数，可检测 I/O 错误 |

## 进阶挑战

- [ ] 实现 `%*d`（运行时宽度）和 `%.*s`（精度截断）
- [ ] 实现 `x6_sprintf(char *buf, fmt, ...)`：输出到字符串而非文件
- [ ] 实现 `x6_snprintf`：有边界的字符串输出（防止缓冲区溢出）
- [ ] 对比 `write(1, "x", 1)` 循环 10000 次 vs 缓冲后一次写，测量系统调用次数差异
- [ ] 实现 `%e`/`%f` 浮点格式化（需要先为 xv6 启用 SSE 浮点）
