# Lab: Lazy Allocation (懒分配与按需分页)

[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

xv6 的 `sys_sbrk` 在收到扩展堆的请求时，立即调用 `growproc` → `allocuvm`，**马上分配并映射**所有请求的物理页。这意味着：

- 程序调用 `malloc(100MB)` → 内核立刻分配 100MB 物理内存
- 即使程序只用了其中的 1MB，99MB 依然被占用
- 物理内存稀缺时，提前分配会导致不必要的 `kalloc` 失败

**懒分配（Lazy Allocation）** 是一种推迟策略：

- `sbrk(n)` 只更新进程的 `sz` 字段，**不分配物理页、不建立页表映射**
- 当程序首次访问扩展区域中的地址时，MMU 发现页表中没有对应映射，触发**页错误**
- 内核在页错误处理中为该地址**按需分配**一个物理页

核心问题：*"物理内存的分配可以推迟到真正需要时再做，这打破了哪些原有假设？"*

## 前置知识

- **`sys_sbrk` 与 `growproc`**: `src/sysproc.c:sys_sbrk()` 调用 `src/vm.c:growproc(n)` 完成堆扩展，`growproc` 调用 `allocuvm` 分配并映射物理页
- **页错误 (#PF)**: 访问无效地址触发中断 14（`T_PGFLT`），`rcr2()` 读取触发的虚拟地址
- **xv6 地址空间**: 用户空间 `[0, KERNBASE)`，堆区 `[data_end, p->sz)` 由 sbrk 管理
- **`walkpgdir`**: `src/vm.c` 中遍历页表的函数，用于检查某地址是否已有映射

```
懒分配的时序:
sbrk(4096):  p->sz += 4096  (不分配物理页)
              页表: [p->oldsz, p->sz) 无映射

访问 p->oldsz:
  MMU 查页表 → 无映射 → #PF 中断
  trap.c: 分配新页，映射 [PGROUNDDOWN(va), +PGSIZE)
  返回用户态，重新执行触发 #PF 的指令
```

## 实验内容

### 1. 修改 sys_sbrk：仅更新 sz (src/sysproc.c)

将 `sys_sbrk` 从立即分配改为懒分配：

```c
int sys_sbrk(void) {
    int n, addr;
    if(argint(0, &n) < 0) return -1;
    addr = myproc()->sz;
    // 原来: if(growproc(n) < 0) return -1;
    // 改为:
    if(n < 0) {
        if(growproc(n) < 0) return -1;  // 收缩仍然立即处理
    } else {
        myproc()->sz += n;               // 扩展仅更新 sz
    }
    return addr;
}
```

**关键约束**: 缩小堆（`n < 0`）仍然需要立即释放物理页，防止内存泄漏。

### 2. 在页错误处理中实现懒分配 (src/trap.c)

在 `trap()` 的 `T_PGFLT` 分支添加懒分配处理：

```c
case T_PGFLT: {
    uint va = rcr2();
    struct proc *p = myproc();
    // 检查 1: va 在用户地址空间内（< p->sz）
    // 检查 2: va >= 0（不是空指针解引用）
    // 检查 3: 页表中确实无映射（非 COW 页错误）
    if(va < p->sz && va >= 0) {
        char *mem = kalloc();
        if(mem == 0) {
            // 物理内存耗尽，杀死进程
            p->killed = 1;
            break;
        }
        memset(mem, 0, PGSIZE);
        uint aligned_va = PGROUNDDOWN(va);
        if(mappages(p->pgdir, (void*)aligned_va, PGSIZE,
                    V2P(mem), PTE_W|PTE_U) < 0) {
            kfree(mem);
            p->killed = 1;
        }
    } else {
        // 真正的非法访问，杀死进程
        cprintf("pid %d: invalid page fault at va %x\n", p->pid, va);
        p->killed = 1;
    }
    break;
}
```

### 3. 修复 copyuvm：跳过未映射区域 (src/vm.c)

`fork()` 调用 `copyuvm()` 复制地址空间时，会按 `[0, p->sz)` 遍历所有地址。懒分配后 `[0, p->sz)` 中可能有未映射的页，`copyuvm` 中调用 `walkpgdir` 时会遇到无效 PTE：

```c
// 在 copyuvm 中，遇到无效 PTE 时跳过（而不是 panic）
pte = walkpgdir(pgdir, (void*)i, 0);
if(pte == 0 || !(*pte & PTE_P))
    continue;   // 懒分配区域，子进程也懒分配
```

### 4. 修复 freevm：跳过未映射页 (src/vm.c)

`deallocuvm` 遍历地址范围释放物理页时，同样需要跳过未映射的地址：

```c
pte = walkpgdir(pgdir, (char*)a, 0);
if(pte == 0 || !(*pte & PTE_P))
    continue;    // 从未分配，无需释放
```

### 5. 处理系统调用中的未映射地址 (src/vm.c)

`sys_read`/`sys_write` 等系统调用通过 `argptr` 验证用户指针时，会检查 `walkpgdir`——若懒分配区域的地址被传入，验证会失败。

两种处理方案（选其一）：
- 方案 A：在 `argptr` 中，若地址在 `[0, p->sz)` 内，先触发一次分配再验证
- 方案 B：修改验证逻辑，允许 `[0, p->sz)` 内的任意地址通过（延迟到实际访问时触发 #PF）

### 6. 编写测试 (user/lazytest.c)

```
测试 1: sbrk(64*1024) 后立即返回（不 OOM），逐字节写入
测试 2: sbrk(1024*1024) 但只写前 4096 字节，物理页仅分配一页
测试 3: 访问 sbrk 扩展区域内的空洞地址（期望不崩溃）
测试 4: fork 后子进程访问父进程懒分配区域（各自独立分配）
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 按需分页 (Demand Paging) | 物理页在首次访问时才分配，即懒分配的本质 |
| 页错误处理 | #PF 中断是内核介入时机，类比 Linux 的 `handle_mm_fault` |
| 地址空间与物理内存解耦 | `p->sz` 描述虚拟空间大小，不等同于物理内存使用量 |
| 零页 | `memset(mem, 0)` 保证未初始化内存为零（安全隔离） |
| 进程地址空间 | 合法地址范围检查（`[0, p->sz)` 内才允许懒分配） |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| src/sysproc.c | 修改 | `sys_sbrk` 扩展时仅更新 `sz`，不调用 `growproc` |
| src/trap.c | 修改 | `T_PGFLT` 分支：懒分配处理 + 非法访问杀进程 |
| src/vm.c | 修改 | `copyuvm`/`deallocuvm` 跳过未映射页 |
| user/lazytest.c | 新增 | 懒分配行为验证 |
| Makefile | 修改 | 添加 `lazytest` |

## 验证

### 编译和运行

```bash
make clean && make qemu-nox
$ lazytest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| sbrk 不立即分配 | `sbrk(1MB)` 返回快速，物理页不立即增加 | 在 trap.c 添加计数器 |
| 按需触发分配 | 首次访问新地址时触发一次 #PF | trap.c cprintf 调试 |
| 合法访问不崩溃 | `[0, sz)` 内的懒分配区域可正常读写 | lazytest 全 PASS |
| 非法访问正确处理 | 访问 `>= sz` 的地址：进程被 killed | lazytest 预期行为验证 |
| usertests 通过 | 已有功能不受影响 | `usertests` 全部 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 内核 panic 在 copyuvm | 遍历到未映射 PTE | copyuvm 跳过 `!PTE_P` 的项 |
| 系统调用 argptr 失败 | read/write 传入懒分配地址未被验证通过 | 修改 argptr 的验证逻辑 |
| 懒分配后数据不为零 | kalloc 的页有旧数据 | 确认 `memset(mem, 0, PGSIZE)` |
| 非法地址未被正确杀死 | 条件判断有误 | 仅允许 `va < p->sz && va >= 0` 时懒分配 |

## 关键代码路径

- 扩展堆: `sysproc.c:sys_sbrk()` → `p->sz += n`（无物理分配）
- 首次访问: 用户程序写 → MMU 页错误 → `trap.c:trap(T_PGFLT)` → `kalloc` + `mappages`
- fork 中跳过: `vm.c:copyuvm()` → `walkpgdir` → PTE 无效则 `continue`
- 进程退出: `vm.c:freevm()` → `deallocuvm` → 跳过未映射页

## 设计权衡

| 方面 | 立即分配（原始） | 懒分配 |
|------|----------------|-------|
| sbrk 延迟 | 与分配大小成正比 | O(1)，极快 |
| 物理内存使用 | 按请求量立即占用 | 只有实际访问到的页才占用 |
| 页错误开销 | 无 | 每个新页一次性 #PF 开销 |
| 实现复杂度 | 简单 | 需处理 copyuvm/freevm 的兼容 |
| OOM 行为 | sbrk 调用时失败 | 首次访问时才可能 OOM |

## 进阶挑战

- [ ] 结合 **COW fork**（lab-mm-02）：懒分配 + COW 同时生效
- [ ] 实现 **Guard Page**：在用户栈下方设置不可访问页，检测栈溢出
- [ ] 实现 **预取（Prefetching）**：检测顺序访问模式，提前分配后续页面
- [ ] 统计**页错误次数**与**实际分配页数**，对比不同程序的懒分配效率
- [ ] 实现 `mincore(addr, len, vec)`：查询哪些懒分配页已实际分配到物理内存
