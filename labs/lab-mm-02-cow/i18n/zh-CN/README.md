# Lab: Copy-on-Write Fork (写时复制 fork)

[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 的 `fork()` 在调用时立即复制父进程的**所有物理页**。如果父进程有 100MB 的数据，fork 就必须分配并复制 100MB 的物理内存——即使子进程紧接着调用 `exec()` 把这些数据全部丢弃。

**Copy-on-Write (COW)** 是现代操作系统中 fork 的标准实现：

- fork 时**不复制**物理页，而是让父子进程共享同一组物理页
- 共享页在父子进程的页表中标记为**只读**（清除 PTE_W）
- 当任一进程尝试写入这些页时，MMU 触发**页错误（Page Fault）**
- 内核在页错误处理中为写入方分配新物理页，复制内容，然后重新映射为可写

核心问题：*"两个进程能共享同一个物理页吗？什么时候应该真正复制？"*

## 前置知识

- **xv6 页表**: `src/vm.c`，两级页表（PDE → PTE → 物理页），`walkpgdir` 查询页表项
- **PTE 标志位**: `PTE_P`（存在）、`PTE_W`（可写）、`PTE_U`（用户态可访问）。`include/mmu.h` 定义
- **页错误**: 访问无效地址或写只读页时，CPU 触发 `#PF`（中断向量 14），在 `src/trap.c:trap()` 中处理，`rcr2()` 读取触发地址
- **引用计数**: 多个进程共享同一物理页时，需要计数引用者，引用为 0 才能真正释放

```
fork() 后的地址空间（COW）:
父进程页表                    子进程页表
  VA 0x1000 → PA 0xA000 [R]    VA 0x1000 → PA 0xA000 [R]
                                           ↑ 共享同一物理页，refcount=2
父进程写 0x1000 → 触发 #PF → 复制 → PA 0xB000 [W], refcount[0xA000]=1
```

## 实验内容

### 1. 为物理页添加引用计数 (src/kalloc.c)

在 `kmem` 结构中添加引用计数数组：

```c
int page_refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
```

实现操作函数：

```c
void  kref_inc(void *pa);   // 引用计数 +1
void  kref_dec(void *pa);   // 引用计数 -1，若为 0 则调用 kfree
int   kref_get(void *pa);   // 读取引用计数
```

- `kalloc()` 返回新页时，初始化 `refcnt = 1`
- `kfree()` 改为：只有 `kref_dec()` 将计数降到 0 时才真正归还页帧

**关键约束**: 引用计数操作必须在 `kmem.lock` 保护下进行，防止并发 free 竞态。

### 2. 定义 COW 标志位 (include/mmu.h)

PTE 没有专用 COW 位，使用可用的软件保留位（`PTE_AVAIL`）：

```c
#define PTE_COW  0x200    // 第 9 位，软件定义 COW 标记
```

### 3. 修改 fork：浅拷贝页表 (src/vm.c)

修改 `copyuvm()`（被 `fork()` 调用），从"深拷贝物理页"改为"共享物理页"：

对于每个已映射的用户页：
1. 清除 PTE 中的 `PTE_W` 标志（不可写）
2. 设置 `PTE_COW` 标志（标记为写时复制页）
3. 子进程页表中用**相同的物理地址**建立映射（同样清除 PTE_W，设置 PTE_COW）
4. 调用 `kref_inc(pa)` 增加物理页的引用计数

**注意**: 父进程的页表项也要清除 PTE_W，因为现在物理页是共享的，父进程写入同样触发 COW。

### 4. 在页错误处理中实现 COW 复制 (src/trap.c)

在 `trap()` 的 `#PF`（T_PGFLT，中断 14）处理分支中：

```c
if(tf->trapno == T_PGFLT) {
    uint va = rcr2();     // 触发地址
    // 1. 检查 va 是否在用户地址空间范围内
    // 2. 查找 va 对应的 PTE，检查 PTE_COW 是否置位
    // 3. 若是 COW 页：
    //    a. 若 refcnt == 1，直接设置 PTE_W（本进程唯一拥有者，无需复制）
    //    b. 若 refcnt > 1，分配新页，复制内容，更新 PTE，kref_dec 旧页
    // 4. 若不是 COW 页（真正的非法访问），kill 进程
}
```

### 5. 修改 kfree 和进程退出

- 进程退出时（`freevm()`），对每个映射的物理页调用 `kref_dec(pa)` 而非直接 `kfree(pa)`
- `exec()` 替换地址空间时同样需要通过 `kref_dec` 释放旧页

### 6. 编写测试程序 (user/cowtest.c)

```
测试 1: fork 后父子各自修改同一地址 → 互不干扰
测试 2: 大内存进程 fork → 不立即触发 OOM
测试 3: fork + exec → exec 前触发 COW，exec 后内存正常
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 写时复制 (COW) | fork 不立即复制物理页，延迟到真正写入时 |
| 页错误处理 (#PF) | MMU 写只读页触发中断，内核介入完成复制 |
| 引用计数 | 多进程共享物理页，计数为 0 才真正释放 |
| 页表操作 | 修改 PTE 标志位实现访问控制（只读/可写） |
| TLB 一致性 | 修改 PTE 后需要 `lcr3()` 刷新 TLB |
| 内存安全 | 非 COW 页的非法写入仍应杀死进程 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| src/kalloc.c | 修改 | 添加 `page_refcnt` 数组，实现 `kref_inc`/`kref_dec` |
| include/mmu.h | 修改 | 添加 `PTE_COW` 软件定义标志位 |
| src/vm.c | 修改 | 改造 `copyuvm` 为浅拷贝（设 PTE_COW），修改 `freevm` |
| src/trap.c | 修改 | 在 T_PGFLT 分支添加 COW 处理逻辑 |
| user/cowtest.c | 新增 | COW 行为验证测试 |
| Makefile | 修改 | 添加 `cowtest` |

## 验证

### 编译和运行

```bash
make clean && make qemu-nox CPUS=1
$ cowtest
$ usertests   # 确认不破坏已有功能
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| fork 不复制物理页 | fork 后物理页使用量基本不变 | 对比 fork 前后 `kfree_count` 统计 |
| 父子数据独立 | 父修改不影响子，反之亦然 | cowtest 输出 PASS |
| 页错误触发复制 | 写入 COW 页后 refcnt 变化 | 在 trap.c 添加 cprintf 调试 |
| usertests 通过 | 已有功能不受影响 | `usertests` 全部 PASS |
| OOM 不崩溃 | fork 大进程不立即 kalloc 失败 | 分配 30+ 页后 fork 成功 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 内核 panic: refcnt < 0 | kref_dec 被多次调用 | 检查 `freevm` 是否对同一 pa 调用多次 |
| fork 后子进程数据错误 | 页表未刷新 TLB | `copyuvm` 后调用 `switchuvm` 或 `lcr3` |
| 写入合法地址却 killed | 非 COW 页的写错误被错误处理 | 检查 `PTE_COW` 标志判断逻辑 |
| 无限页错误循环 | COW 处理后未正确更新 PTE_W | 确认修改 PTE 后调用 `lcr3(V2P(pgdir))` |

## 关键代码路径

- fork 路径: `proc.c:fork()` → `vm.c:copyuvm()` → PTE 浅拷贝 + kref_inc
- 写入触发: 用户写只读页 → CPU 产生 #PF → `trap.c:trap()` → COW 处理
- COW 处理: `trap()` → `kalloc()` → `memmove()` → 更新 PTE → `lcr3()` 刷新 TLB
- 进程退出: `proc.c:exit()` → `vm.c:freevm()` → `kref_dec` 每个页

## 设计权衡

| 方面 | 直接复制 fork（原始） | COW fork |
|------|---------------------|---------|
| fork 延迟 | O(进程内存大小) | O(1) — 仅复制页表 |
| 内存使用 | 立即翻倍 | 写入前共享，按需分配 |
| 页错误开销 | 无 | 每次写 COW 页一次性开销 |
| 实现复杂度 | 简单 | 较复杂（引用计数 + 页错误处理） |
| exec 配合 | fork 后 exec 浪费复制时间 | fork 后立即 exec 几乎零内存开销 |

## 进阶挑战

- [ ] 实现 **Zero Page**：所有未初始化的页映射到同一个全零物理页（只读），写入时再 COW
- [ ] 统计 fork 节省的物理页数（COW 时实际复制次数），与直接复制对比
- [ ] 结合 `lab-mm-03-lazy`：sbrk 懒分配 + COW 同时生效
- [ ] 在多核（CPUS=2）下测试，验证引用计数的并发安全性
- [ ] 实现 **`vfork()`**：子进程直接使用父进程地址空间，用于 fork+exec 优化
