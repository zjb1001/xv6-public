# Lab: Signal Mechanism (信号机制)
[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 的 `kill(pid)` 系统调用只能向进程发送一种"信号"——强制终止。它通过设置 `p->killed = 1`，进程在下次从内核返回时检查并退出。这种机制极其简陋：没有信号编号、没有用户自定义处理函数、没有信号屏蔽。

Unix 信号机制（POSIX signals）是进程间异步通知的标准方式：

- 进程可以注册**信号处理函数（Signal Handler）**，当收到信号时在用户态执行
- 内核向进程"递送"信号时，会修改进程的返回路径，让它先跑信号处理函数，再恢复原来的执行
- 典型用途：`SIGINT`（Ctrl+C 终止）、`SIGCHLD`（子进程退出通知）、`SIGUSR1/2`（用户自定义）

核心问题：*"信号处理函数明明在用户代码里，内核是怎么让进程'突然跳进去执行'的？"*

## 前置知识

- **Trapframe**: `src/trapasm.S` 中保存的用户态寄存器快照，`struct trapframe` 在 `include/x86.h` 定义。内核通过修改 trapframe 中的 `%eip` 来改变用户进程"返回"后的执行位置
- **用户态信号处理的挑战**: 信号处理函数在用户态运行，运行完后需要恢复到被打断前的状态（包括所有寄存器）。这需要内核在用户栈上保存完整 trapframe，并在处理函数返回时通过 `sigreturn` 系统调用恢复
- **信号屏蔽**: 进程可以暂时屏蔽某些信号（延迟递送），处理信号期间通常自动屏蔽同类信号

```
信号递送的执行路径:
正常: 用户代码 → 系统调用/中断 → 内核 → 返回用户代码

信号递送: 用户代码 → 系统调用/中断 → 内核
  ↓ 返回前检查 pending 信号
  ↓ 修改用户栈和 trapframe
  用户态执行 signal_handler()
  ↓ signal_handler 返回 → 调用 sigreturn()
  ↓ 内核恢复原始 trapframe
  恢复原始用户代码执行
```

## 实验内容

### 1. 定义信号编号和数据结构 (include/proc.h)

```c
#define NSIG     16         // 最多 16 种信号

// 标准信号编号（子集）
#define SIGHUP   1          // 挂断
#define SIGINT   2          // 中断（Ctrl+C）
#define SIGKILL  9          // 强制终止（不可捕获）
#define SIGSEGV  11         // 段错误
#define SIGALRM  14         // 定时器信号
#define SIGTERM  15         // 终止请求

typedef void (*sighandler_t)(int);
#define SIG_DFL  ((sighandler_t)0)   // 默认处理（终止进程）
#define SIG_IGN  ((sighandler_t)1)   // 忽略信号

struct proc {
    // ... 现有字段 ...
    sighandler_t  sig_handlers[NSIG];  // 每个信号的处理函数
    uint          sig_pending;         // 待处理信号位图
    uint          sig_mask;            // 被屏蔽的信号位图
};
```

### 2. 实现 sys_signal（注册信号处理函数）(src/sysproc.c)

```c
// sys_signal(signo, handler) → 返回旧的 handler
sighandler_t sys_signal(void) {
    int signo;
    sighandler_t handler;
    if(argint(0, &signo) < 0 || argptr(1, (char**)&handler, sizeof(handler)) < 0)
        return SIG_ERR;
    if(signo <= 0 || signo >= NSIG) return SIG_ERR;
    if(signo == SIGKILL) return SIG_ERR;  // SIGKILL 不可捕获
    struct proc *p = myproc();
    sighandler_t old = p->sig_handlers[signo];
    p->sig_handlers[signo] = handler;
    return old;
}
```

### 3. 扩展 sys_kill：支持信号编号 (src/sysproc.c)

将 `sys_kill(pid, signo)` 从"只能终止"扩展为"发送任意信号"：

```c
int sys_kill(void) {
    int pid, signo;
    if(argint(0, &pid) < 0 || argint(1, &signo) < 0) return -1;
    return kill(pid, signo);
}

int kill(int pid, int signo) {
    struct proc *p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->pid == pid) {
            p->sig_pending |= (1 << signo);   // 设置 pending 位
            if(p->state == SLEEPING)
                p->state = RUNNABLE;           // 唤醒正在睡眠的进程
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}
```

### 4. 在内核返回用户态前递送信号 (src/trap.c)

在 `trap()` 函数末尾（用户进程从系统调用/中断返回前），检查并递送 pending 信号：

```c
// 在 trap() 返回前：
if(myproc() && myproc()->state == RUNNING)
    deliver_pending_signals(myproc(), tf);
```

```c
// src/proc.c 或 src/trap.c：
void deliver_pending_signals(struct proc *p, struct trapframe *tf) {
    for(int sig = 1; sig < NSIG; sig++) {
        if(!(p->sig_pending & (1 << sig))) continue;
        if(p->sig_mask & (1 << sig)) continue;     // 被屏蔽，跳过
        p->sig_pending &= ~(1 << sig);             // 清除 pending 位

        sighandler_t handler = p->sig_handlers[sig];
        if(handler == SIG_IGN) continue;
        if(handler == SIG_DFL) {
            // 默认处理：终止进程（类似原 kill 行为）
            p->killed = 1;
            return;
        }
        // 用户自定义 handler：修改 trapframe，让进程"返回"到 handler
        setup_signal_frame(p, tf, sig, handler);
        return;
    }
}
```

### 5. 实现信号帧（Signal Frame）的建立和恢复 (src/proc.c)

`setup_signal_frame` 的作用：在用户栈上保存当前 trapframe，然后让进程"返回"到信号处理函数：

```c
void setup_signal_frame(struct proc *p, struct trapframe *tf,
                         int sig, sighandler_t handler) {
    // 1. 在用户栈上保存当前 trapframe（让 sigreturn 能恢复）
    uint sp = tf->esp;
    sp -= sizeof(struct trapframe);
    if(copyout(p->pgdir, sp, tf, sizeof(struct trapframe)) < 0) {
        p->killed = 1;
        return;
    }
    // 2. 压入信号处理函数的参数（signo）和返回地址（sigreturn stub）
    sp -= sizeof(int);
    int signo_val = sig;
    copyout(p->pgdir, sp, &signo_val, sizeof(int));
    // 返回地址：用户态的 sigreturn 存根（一段调用 sys_sigreturn 的代码）
    sp -= sizeof(uint);
    uint sigreturn_addr = (uint)p->sigreturn_stub;  // 见步骤 6
    copyout(p->pgdir, sp, &sigreturn_addr, sizeof(uint));
    // 3. 修改 trapframe：eip 指向 handler，esp 指向新栈顶
    tf->eip = (uint)handler;
    tf->esp = sp;
}
```

### 6. 实现 sigreturn 和用户态存根 (src/sysproc.c, user/)

信号处理函数执行完后，需要通过 `sigreturn` 恢复原始上下文：

```c
int sys_sigreturn(void) {
    struct proc *p = myproc();
    struct trapframe *tf = p->tf;
    // 从用户栈上恢复之前保存的 trapframe
    struct trapframe saved;
    uint sp = tf->esp;
    // 跳过 signo 参数，找到保存的 trapframe
    copyin(p->pgdir, (char*)&saved, sp + sizeof(uint) + sizeof(int),
           sizeof(struct trapframe));
    *tf = saved;
    return 0;
}
```

用户态 sigreturn 存根（内嵌汇编，在 `user/signal.c` 中）：

```c
// 信号处理函数返回后自动跳到这里
void sigreturn_stub(void) {
    // 调用 sys_sigreturn 系统调用
    asm volatile("movl %0, %%eax; int $64" :: "i"(SYS_sigreturn));
}
```

### 7. 编写测试 (user/signaltest.c)

```
测试 1: signal(SIGINT, handler) + kill(pid, SIGINT) → handler 被调用
测试 2: SIGKILL 无法被捕获，进程终止
测试 3: SIG_IGN：kill 发送 SIGTERM 后进程不终止
测试 4: 信号处理完成后恢复原始执行（验证 sigreturn 正确性）
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 异步事件通知 | 信号是内核向进程传递异步事件的机制 |
| Trapframe 修改 | 内核通过修改 tf->eip/esp 控制用户态执行流 |
| 用户栈框架（Signal Frame） | 内核在用户栈上保存上下文，信号返回时恢复 |
| 不可捕获信号 | SIGKILL 无法被覆盖，保证系统控制权 |
| 信号屏蔽 | sig_mask 实现信号的临时延迟，防止重入 |
| 用户态/内核态边界 | 信号处理函数在用户态运行，但由内核触发跳转 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/proc.h | 修改 | 添加信号相关字段 |
| src/proc.c | 修改 | `allocproc`/`fork` 初始化信号字段，实现信号递送 |
| src/sysproc.c | 修改 | 实现 `sys_signal`、`sys_kill`（扩展）、`sys_sigreturn` |
| src/trap.c | 修改 | 在用户态返回前调用 `deliver_pending_signals` |
| include/syscall.h | 修改 | 添加 `SYS_signal`、`SYS_sigreturn` |
| user/signaltest.c | 新增 | 信号机制验证测试 |

## 验证

```bash
make clean && make qemu-nox
$ signaltest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 信号处理函数被调用 | kill 后 handler 打印信息 | signaltest 输出 |
| SIGKILL 不可捕获 | signal(SIGKILL, fn) 返回错误 | signaltest 验证 |
| SIG_IGN 生效 | 被忽略的信号不触发处理也不终止进程 | signaltest 验证 |
| sigreturn 正确 | handler 返回后原始代码继续执行 | signaltest 验证后续代码运行 |
| usertests 通过 | 不破坏已有功能 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 处理函数执行后崩溃 | sigreturn 未正确恢复 %esp | 检查 signal frame 的栈布局和偏移计算 |
| kill 发送后进程不响应 | deliver 函数未在正确位置调用 | 确认在 trap() 末尾、用户态返回前调用 |
| SLEEPING 进程不能及时收到信号 | kill 中未唤醒 SLEEPING 进程 | 确认 `p->state = RUNNABLE` 在设置 pending 之后 |

## 关键代码路径

- 注册处理函数: `sys_signal()` → `p->sig_handlers[signo] = handler`
- 发送信号: `sys_kill(pid, signo)` → `p->sig_pending |= (1<<signo)` → 唤醒 SLEEPING
- 递送信号: `trap()` 末尾 → `deliver_pending_signals()` → `setup_signal_frame()`
- 恢复执行: 信号 handler 返回 → `sigreturn_stub` → `sys_sigreturn()` → 恢复 trapframe

## 设计权衡

| 方面 | 原始 kill | 完整信号机制 |
|------|----------|-----------|
| 信号种类 | 1 种（终止） | NSIG=16 种 |
| 用户处理 | 不支持 | 可注册自定义 handler |
| 信号忽略 | 不支持 | SIG_IGN |
| 实现复杂度 | 极简（设置 killed 位） | 高（signal frame、sigreturn） |
| 异步安全 | 不适用 | handler 中只能调用 async-signal-safe 函数 |

## 进阶挑战

- [ ] 实现 `sigprocmask`：运行时修改信号屏蔽集
- [ ] 实现 `sigaction`（更完整的接口）：支持 `SA_RESTART`、`SA_ONESHOT` 等标志
- [ ] 实现 `SIGCHLD`：子进程退出时自动向父进程发送，实现非阻塞 wait
- [ ] 实现 `alarm(seconds)`：定时发送 `SIGALRM`（需要内核定时器支持）
- [ ] 分析并实现**信号的多线程安全**：当有用户级线程（lab-lib-03）时，哪个线程接收信号？
