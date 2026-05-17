# Lab: Waitpid and Process Groups (waitpid 与进程组)
[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

xv6 的 `wait()` 系统调用只能等待**任意一个**子进程退出，且调用者被强制阻塞。这意味着：

- 无法等待**特定**子进程（必须等到调用 wait 的那个子进程退出）
- 无法**非阻塞**轮询子进程状态（不能同时做其他事）
- 没有**进程组**概念，无法对一组进程统一管理

本实验实现 POSIX 标准的 `waitpid(pid, &status, options)` 和进程组机制：

```c
pid_t waitpid(pid_t pid, int *status, int options);
// pid > 0:   等待特定 pid 的子进程
// pid == -1: 等待任意子进程（等同于 wait）
// options & WNOHANG: 若无子进程退出则立即返回 0（非阻塞）
```

核心问题：*"孤儿进程、僵尸进程分别是什么？为什么它们都是操作系统必须处理的问题？"*

## 前置知识

- **xv6 `wait()` 实现**: `src/proc.c:wait()` 持有 `ptable.lock`，遍历查找 ZOMBIE 子进程，找到则清理并返回；找不到则 `sleep(curproc)` 阻塞，待子进程 `exit()` 时唤醒
- **僵尸进程（Zombie）**: 进程退出但父进程尚未调用 `wait` 回收。`struct proc` 仍占用槽位，持有退出状态（`p->xstate`），等父进程 wait
- **孤儿进程（Orphan）**: 父进程先于子进程退出。xv6 中孤儿进程被 `init`（pid=1）收养（`reparent`），由 `init` 周期性 wait
- **进程组**: 一组相关进程的集合（如 shell 启动的管道命令）。向整个进程组发送信号（如 Ctrl+C）可以同时终止管道中所有进程

```
进程树示例:
init (pgid=1)
  └─ sh (pgid=100)
       └─ grep pattern | wc -l
            ├─ grep (pgid=101)  ← 同一进程组
            └─ wc   (pgid=101)  ← 同一进程组

Ctrl+C 发送 SIGINT 给 pgid=101 → grep 和 wc 同时终止
```

## 实验内容

### 1. 在进程控制块中添加进程组字段 (include/proc.h)

```c
struct proc {
    // ... 现有字段 ...
    int  pgid;       // 进程组 ID（通常等于组长进程的 pid）
    int  xstate;     // 退出状态（exit 的参数，wait 返回给父进程）
};
```

**初始化**: `fork()` 中子进程继承父进程 `pgid`；`allocproc()` 中 `xstate = 0`。

### 2. 实现 sys_waitpid (src/sysproc.c)

```c
int sys_waitpid(void) {
    int pid, options;
    int *status;
    if(argint(0, &pid) < 0 || argptr(1, (char**)&status, sizeof(int)) < 0
       || argint(2, &options) < 0) return -1;
    return waitpid(pid, status, options);
}

int waitpid(int pid, int *status, int options) {
    struct proc *p, *curproc = myproc();
    acquire(&ptable.lock);

    for(;;) {
        int havekids = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            // 确认 p 是 curproc 的子进程
            if(p->parent != curproc) continue;
            // 确认 pid 匹配（pid==-1 匹配任意，pid>0 精确匹配）
            if(pid != -1 && p->pid != pid) continue;
            havekids = 1;
            if(p->state == ZOMBIE) {
                // 回收子进程
                int cpid = p->pid;
                if(status) *status = p->xstate;
                // 清理 p（freeing stack, pgdir, etc.）
                freeproc(p);
                release(&ptable.lock);
                return cpid;
            }
        }
        if(!havekids || curproc->killed) {
            release(&ptable.lock);
            return -1;
        }
        if(options & WNOHANG) {
            // 非阻塞：没有子进程退出，立即返回 0
            release(&ptable.lock);
            return 0;
        }
        // 阻塞等待，直到某个子进程退出
        sleep(curproc, &ptable.lock);
    }
}
```

### 3. 修改 exit：记录退出状态 (src/proc.c)

扩展 `sys_exit(status)` 接受退出状态参数，保存到 `p->xstate`：

```c
int sys_exit(void) {
    int status;
    if(argint(0, &status) < 0) status = 0;
    myproc()->xstate = status;
    exit();
    return 0;  // 不可达
}
```

### 4. 实现进程组操作 (src/sysproc.c)

```c
int sys_setpgid(void) {
    int pid, pgid;
    if(argint(0, &pid) < 0 || argint(1, &pgid) < 0) return -1;
    if(pid == 0) pid = myproc()->pid;
    if(pgid == 0) pgid = pid;
    // 找到目标进程，修改其 pgid
    struct proc *p;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->pid == pid) {
            p->pgid = pgid;
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

int sys_getpgid(void) {
    int pid;
    if(argint(0, &pid) < 0) return -1;
    if(pid == 0) return myproc()->pgid;
    // 查找并返回目标进程的 pgid
}
```

### 5. 向进程组发送信号（与 lab-proc-01 结合）

若已实现信号机制，扩展 `kill(-pgid, signo)` 向整个进程组发送信号：

```c
// pid < 0 时，向 abs(pid) 进程组中所有进程发送信号
if(pid < 0) {
    int target_pgid = -pid;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->pgid == target_pgid && p->state != UNUSED)
            p->sig_pending |= (1 << signo);
    }
}
```

### 6. 修改 sh：在管道中使用进程组

修改 `user/sh.c` 中管道命令的创建，使管道两端的进程共享同一进程组：

```c
// 在 sh.c 的 runcmd(pipe) 中，fork 子进程后：
if(fork1() == 0) {
    setpgid(0, pipe_pgid);   // 加入管道进程组
    runcmd(pcmd->left);
}
```

### 7. 编写测试 (user/waitpidtest.c)

```
测试 1: waitpid(specific_pid) 等待特定子进程
测试 2: WNOHANG：子进程未退出时返回 0（非阻塞）
测试 3: 退出状态传递：exit(42) 后 waitpid 获取 status == 42
测试 4: 孤儿进程被 init 收养：父进程先 exit，子进程仍能正常完成
测试 5: 进程组：设置 pgid 后用 kill(-pgid, SIGTERM) 终止整组
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 僵尸进程 | 进程退出后 struct proc 保留 xstate，等父进程 wait 回收 |
| 孤儿进程 | 父进程先退出，子进程被 init (pid=1) 收养 |
| WNOHANG | 非阻塞 wait：轮询子进程状态而不阻塞 |
| 退出状态 | exit 的参数通过 xstate 传递给 waitpid 调用者 |
| 进程组 | pgid 标识相关进程集合，支持组级别信号投递 |
| wait 语义 | 子进程退出后唤醒父进程（sleep/wakeup 机制） |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/proc.h | 修改 | 添加 `pgid`、`xstate` 字段 |
| src/proc.c | 修改 | `fork` 继承 pgid，`exit` 保存 xstate，实现 `waitpid` |
| src/sysproc.c | 修改 | 注册 `sys_waitpid`、`sys_setpgid`、`sys_getpgid`、`sys_exit`（扩展） |
| include/syscall.h | 修改 | 添加新系统调用编号 |
| include/user.h | 修改 | 添加用户态函数声明 |
| user/usys.S | 修改 | 添加系统调用汇编入口 |
| user/waitpidtest.c | 新增 | waitpid 和进程组测试 |

## 验证

```bash
make clean && make qemu-nox
$ waitpidtest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 特定 pid 等待 | waitpid(pid) 仅在该 pid 退出后返回 | waitpidtest 验证 |
| WNOHANG 非阻塞 | 子进程未退出时返回 0 | waitpidtest 验证 |
| 退出状态正确 | exit(N) 后 status 为 N | waitpidtest 验证 |
| 孤儿被 init 收养 | 父进程退出后子进程仍正常运行 | waitpidtest 观察子进程输出 |
| usertests 通过 | wait() 和 exit() 已有行为不变 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| waitpid 对孤儿进程返回 -1 | 孤儿被收养后 parent 已变为 init | 在 `reparent()` 时正确更新 parent |
| WNOHANG 阻塞了 | options 参数解析错误 | 确认 `argint(2, &options)` 正确读取第三个参数 |
| 退出状态丢失 | xstate 在 freeproc 中被清零了 | 在读取 xstate 后再清零 proc 结构 |

## 关键代码路径

- 记录退出状态: `proc.c:exit()` → `p->xstate = ...` → 将子进程标记 ZOMBIE → 唤醒父进程
- 等待特定进程: `waitpid(pid, ...)` → 扫描 ptable 找匹配 pid 且 ZOMBIE 的进程
- 非阻塞检测: waitpid → `options & WNOHANG` → 无 ZOMBIE 时直接返回 0
- 进程组: `setpgid()` → 修改 `p->pgid`；`kill(-pgid, sig)` → 遍历匹配 pgid 的进程

## 设计权衡

| 方面 | 原始 wait() | waitpid() |
|------|------------|---------|
| 等待目标 | 任意一个子进程 | 可指定 pid 或任意 |
| 阻塞行为 | 始终阻塞 | 支持 WNOHANG 非阻塞 |
| 退出状态 | 有但未传递 status 指针 | 通过 *status 传递 |
| POSIX 兼容 | 非标准 | 标准 POSIX 接口 |

## 进阶挑战

- [ ] 实现 `WUNTRACED` 选项：等待进程停止（SIGSTOP）而非退出
- [ ] 实现 `SIGCHLD` 信号：子进程状态变化时通知父进程（需结合 lab-proc-01）
- [ ] 实现完整的**会话（Session）**概念：进程组之上的更高层分组
- [ ] 修改 `sh.c` 实现完整的**作业控制（Job Control）**：fg/bg/jobs 命令
- [ ] 统计**孤儿进程数量**：在 init 中记录每次 reparent，分析系统中孤儿的产生规律
