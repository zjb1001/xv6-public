# Lab: User-Level Thread Library (用户级协程库)

[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 的进程通过 `fork` 创建，每次 fork 都会复制整个地址空间，并通过内核调度器切换——这意味着每次上下文切换都要经历陷入内核、保存/恢复 trapframe、返回用户态等开销。

本实验在**用户空间**实现轻量级线程（协程），完全绕过内核参与：

- 每个线程拥有独立的**栈**（用 `malloc` 分配）
- 线程间切换通过 `setjmp`/`longjmp` 在用户态完成，不触发任何系统调用
- 调度策略为**协作式 Round-Robin**：线程必须主动调用 `thread_yield` 让出 CPU

核心问题：*"切换到另一个函数执行，不经过内核，怎么做到？"*

## 前置知识

- **调用栈结构**: 每个函数调用在栈上分配帧（局部变量、保存的寄存器、返回地址），函数返回时弹出帧
- **`setjmp`/`longjmp`**: `setjmp(jb)` 保存当前寄存器（包括 `%esp`/`%eip`）到 `jmp_buf`；`longjmp(jb, 1)` 恢复这些寄存器，"跳回" setjmp 调用点
- **协作式 vs 抢占式**: 协作式多任务中，线程不会被强制打断，只有主动 yield 才切换；抢占式依赖定时器中断

```
用户级线程的栈布局:
主栈 (sbrk 分配的默认栈)
  ┌──────────────┐
  │  thread lib  │  ← 调度器在这里运行
  └──────────────┘

线程 1 的栈 (malloc 分配)      线程 2 的栈 (malloc 分配)
  ┌──────────────┐               ┌──────────────┐
  │  func_a 的帧 │               │  func_b 的帧 │
  │  local vars  │               │  local vars  │
  └──────────────┘               └──────────────┘
  jmp_buf 保存 %esp 指向这里      jmp_buf 保存 %esp 指向这里
```

## 实验内容

### 1. 定义线程控制块 (lib/xv6_thread.h)

```c
#define THREAD_STACK_SIZE  4096
#define MAX_THREADS        8

typedef enum { THREAD_FREE, THREAD_RUNNABLE, THREAD_RUNNING, THREAD_DONE } thread_state_t;

typedef struct thread {
    int            id;
    thread_state_t state;
    jmp_buf        ctx;          // 线程上下文（寄存器快照）
    char          *stack;        // 线程私有栈（malloc 分配）
    void         (*func)(void);  // 线程入口函数
} thread_t;
```

**关键约束**:
- 线程 0 是"主线程"，使用进程默认栈，不需要 malloc
- `ctx` 仅在线程被 yield 时有效；线程首次启动需要特殊处理（见步骤 3）

### 2. 实现线程创建 (lib/xv6_thread.c)

```c
int thread_create(void (*func)(void))
```

- 从线程表中分配一个空闲槽位
- 用 `malloc(THREAD_STACK_SIZE)` 分配栈空间
- **初始化栈帧**: 在栈顶压入 `thread_entry`（见步骤 3）和 `func` 指针，使得首次 `longjmp` 到此线程时能跳入入口函数
- 设置 `state = THREAD_RUNNABLE`

**关键技巧 — 手动构造初始栈**:

x86 函数调用进入时，栈顶是返回地址，再往下是参数。因此在新栈顶伪造一个"调用帧"：

```c
char *sp = stack + THREAD_STACK_SIZE;  // 栈从高地址向低生长
sp -= sizeof(void*);
*(void**)sp = func;                    // "参数 1"：线程函数
sp -= sizeof(void*);
*(void**)sp = thread_exit;             // "返回地址"：线程结束后调用 exit
// 然后将 ctx.esp 设置为 sp（通过 setjmp 黑魔法或直接赋值）
```

### 3. 实现协作式调度器 (lib/xv6_thread.c)

```c
static void scheduler(void)
```

- 维护 `current_thread` 全局变量
- 从线程表中按 Round-Robin 顺序找下一个 `THREAD_RUNNABLE` 线程
- 如果找到：`longjmp(next->ctx, 1)` 切换到目标线程
- 如果没有 RUNNABLE 线程：所有线程结束，`exit(0)` 退出进程

```c
void thread_yield(void)
```

- 将当前线程设为 RUNNABLE
- `if(setjmp(current->ctx) == 0)` → 首次调用，调用 `scheduler()`
- `if(setjmp(current->ctx) != 0)` → 从 longjmp 返回，恢复执行

```c
static void thread_exit(void)
```

- 设置当前线程 `state = THREAD_DONE`，释放栈
- 调用 `scheduler()` 切换到其他线程（永不返回）

### 4. 实现 `thread_run` 入口 (lib/xv6_thread.c)

```c
void thread_run(void)
```

- 初始化主线程（id=0，使用当前栈）
- 循环调用 `scheduler()` 直到所有线程结束

### 5. 编写测试程序 (user/threadtest.c)

```c
void task_a(void) {
    for(int i = 0; i < 3; i++) {
        printf("A %d\n", i);
        thread_yield();
    }
}
void task_b(void) {
    for(int i = 0; i < 3; i++) {
        printf("B %d\n", i);
        thread_yield();
    }
}
int main(void) {
    thread_create(task_a);
    thread_create(task_b);
    thread_run();
}
// 期望输出: A 0 / B 0 / A 1 / B 1 / A 2 / B 2
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 上下文切换 (Context Switch) | `setjmp`/`longjmp` 保存/恢复寄存器，等同于内核 `swtch.S` |
| 线程控制块 (TCB) | `thread_t` 结构体：状态、栈、上下文 |
| 协作式调度 | 线程必须主动 yield，类比早期 Windows 3.x 消息循环 |
| 栈分配 | 用户态 malloc 分配线程栈，对比内核为每个进程分配内核栈 |
| 调用约定 | 手动构造 x86 初始栈帧，理解 `%esp`/`%eip`/返回地址布局 |
| M:1 模型 | 所有用户线程共享同一个内核进程/调度实体 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| lib/xv6_thread.h | 新增 | TCB 定义、线程 API 声明 |
| lib/xv6_thread.c | 新增 | 完整线程库实现 |
| user/threadtest.c | 新增 | 验证协作式调度顺序 |
| Makefile | 修改 | 添加 `threadtest` 和 `xv6_thread.c` 到构建 |

## 验证

### 编译和运行

```bash
make clean && make qemu-nox
$ threadtest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 交替执行 | A 0 / B 0 / A 1 / B 1 ... 严格交替 | 输出行顺序 |
| 正常结束 | 所有线程 DONE 后进程退出，无挂起 | shell 返回 `$` |
| 独立栈 | 各线程局部变量互不干扰 | 在 task_a/b 中声明大数组验证 |
| yield 语义 | 不调用 yield 的线程独占 CPU | 修改测试去掉 yield，观察串行输出 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 跳转后立即崩溃 | 初始栈帧构造错误，`%esp` 未对齐 | x86 要求 16 字节栈对齐，检查 sp 对齐 |
| 线程函数返回后崩溃 | "返回地址"不是 `thread_exit` | 检查初始栈顶写入的返回地址 |
| 调度器无限循环 | 线程未被正确标记为 DONE | 确认 `thread_exit` 设置了 `state = THREAD_DONE` |
| setjmp 返回值不对 | longjmp 第二参数为 0 时会被改为 1（C 标准行为） | 正常现象，用 `!= 0` 判断 longjmp 返回 |

## 关键代码路径

- 线程创建: `thread_create()` → malloc 栈 → 构造初始栈帧
- 首次切换: `thread_run()` → `scheduler()` → `longjmp(t->ctx)` → 跳入 `thread_entry`
- 让出 CPU: `thread_yield()` → `setjmp(save)` → `scheduler()` → `longjmp(next->ctx)`
- 线程结束: `thread_exit()` → `state=DONE` → `scheduler()`（永不返回）

## 设计权衡

| 方面 | 用户级线程（本实验） | 内核线程（xv6 进程） |
|------|--------------------|--------------------|
| 切换开销 | 极低（纯用户态 setjmp/longjmp） | 较高（陷入内核、保存 trapframe） |
| 并行能力 | 无（M:1，共享单核） | 有（1:1，可多核并行） |
| 阻塞影响 | 一个线程阻塞（read），全进程停止 | 只影响当前内核线程 |
| 抢占 | 无（协作式） | 有（时钟中断） |
| 实现复杂度 | 低（用户态） | 高（需内核支持） |

## 进阶挑战

- [ ] 实现**抢占式**用户线程：用 `SIGALRM`（若实现信号后）定时触发 yield
- [ ] 实现线程局部存储 (TLS)：每个线程的 `errno` 变量独立
- [ ] 实现简单的用户态互斥锁（与 lab-sync-01 配合）
- [ ] 将调度策略从 Round-Robin 改为**优先级调度**
- [ ] 统计每个线程的 yield 次数和执行时间，输出调度统计信息
