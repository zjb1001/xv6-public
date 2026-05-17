# Lab: mmap System Call (内存映射文件)

[English](../../README.md)

难度: ★★★★★

## 设计初衷

Unix 系统的文件访问有两种范式：通过 `read`/`write` 系统调用（内核缓冲区→用户缓冲区的拷贝），或通过 `mmap` 将文件直接映射到进程虚拟地址空间（通过页错误按需加载，零拷贝访问）。

`mmap` 是现代操作系统中最重要的内存管理接口之一：
- **文件映射**: 将文件的某个区间映射为虚拟地址，直接用指针读写文件内容
- **匿名映射**: 分配私有零页，作为堆分配的底层机制（glibc `malloc` 大块分配即用此实现）
- **共享映射**: 多进程映射同一文件，实现无 IPC 开销的进程间通信

核心问题：*"进程读文件，为什么'不经过系统调用'用指针直接访问可以更快？"*

## 前置知识

- **VMA（虚拟内存区域）**: Linux 中每段连续虚拟地址映射由 `struct vm_area_struct` 描述，xv6 中需要自己实现
- **文件页缓存**: 文件内容在内核 buffer cache（`bio.c`）中缓存，mmap 可将这些缓存页直接映射给用户，避免额外复制
- **`mappages`**: `src/vm.c` 中建立 PTE 的函数，支持任意物理地址和保护标志
- **`munmap` 的写回**: 若映射为 `MAP_SHARED`，修改映射区域后必须在 munmap 时写回文件

```
mmap 的两种映射:
文件映射: va [0x5000, 0x6000) → 文件 foo.txt [offset 0, len 4096]
           访问 va 0x5000 → #PF → 读文件 offset 0 → 物理页 → 映射

匿名映射: va [0x7000, 0x8000) → 零页（不关联文件）
           访问 va 0x7000 → #PF → kalloc 零页 → 映射
```

## 实验内容

### 1. 定义 VMA 结构 (include/proc.h)

```c
#define NVMA  16       // 每个进程最多 16 个 VMA

#define MAP_SHARED   0x1
#define MAP_PRIVATE  0x2
#define MAP_ANON     0x4

#define PROT_READ    0x1
#define PROT_WRITE   0x2

struct vma {
    uint   valid;      // 1=在使用, 0=空槽
    uint   addr;       // 映射起始虚拟地址
    uint   len;        // 映射长度（字节，页对齐）
    int    prot;       // PROT_READ | PROT_WRITE
    int    flags;      // MAP_SHARED | MAP_PRIVATE | MAP_ANON
    struct file *f;    // 关联文件（MAP_ANON 时为 0）
    uint   offset;     // 文件内偏移（字节）
};
```

在 `struct proc` 中添加 `struct vma vmas[NVMA]`。

### 2. 实现 sys_mmap (src/sysfile.c)

```c
void* sys_mmap(void)
```

参数（按 Linux 约定）: `addr hint`, `len`, `prot`, `flags`, `fd`, `offset`

实现步骤：
1. 用 `argint`/`argfd` 解析参数，验证合法性（len > 0，flags 合法，prot 与文件打开模式一致）
2. 选择映射地址：忽略 hint，直接从进程 `p->sz` 之上分配（`addr = PGROUNDUP(p->sz)`），更新 `p->sz`
3. 找一个空闲的 `vmas[]` 槽，填入 VMA 信息
4. 如果是文件映射，调用 `filedup(f)` 增加文件引用计数
5. **不立即建立页表映射**（懒映射），返回 `addr`

### 3. 在页错误中处理 VMA (src/trap.c)

扩展 `T_PGFLT` 处理：

```c
// 在懒分配处理之后，检查是否命中某个 VMA
struct vma *vma = vma_find(p, va);   // 查找 va 所在 VMA
if(vma) {
    char *mem = kalloc();
    memset(mem, 0, PGSIZE);
    if(vma->flags & MAP_ANON) {
        // 匿名映射：零页即可
    } else {
        // 文件映射：从文件读入数据
        uint page_offset = PGROUNDDOWN(va) - vma->addr + vma->offset;
        readi(vma->f->ip, mem, page_offset, PGSIZE);
    }
    uint perm = PTE_U;
    if(vma->prot & PROT_WRITE) perm |= PTE_W;
    mappages(p->pgdir, (void*)PGROUNDDOWN(va), PGSIZE, V2P(mem), perm);
}
```

### 4. 实现 sys_munmap (src/sysfile.c)

```c
int sys_munmap(void)
```

参数: `addr`, `len`

实现步骤：
1. 找到对应的 VMA（addr 和 len 必须完全匹配或为 VMA 的子集）
2. 若是 `MAP_SHARED` 文件映射，将脏页写回文件（遍历地址范围，找已映射的页，调用 `writei`）
3. 解除页表映射（调用 `deallocuvm` 或逐页 unmap）
4. `fileclose(vma->f)` 释放文件引用
5. 清空 VMA 槽

### 5. 修改 fork 和 exit 处理 VMA (src/proc.c)

- **fork**: 复制父进程的 `vmas[]` 数组，对每个有效 VMA 调用 `filedup(vma->f)`，页表按 COW 处理（或简单重新懒映射）
- **exit**: 对每个有效 VMA 执行 munmap 操作（写回 + 解除映射 + fileclose）

### 6. 编写测试 (user/mmaptest.c)

```
测试 1: 匿名 mmap + 读写 + munmap，验证内存独立
测试 2: 文件映射（只读）：mmap 一个文本文件，用指针读出内容，对比 read 的结果
测试 3: 文件映射（读写 MAP_SHARED）：修改映射区，munmap 后用 read 验证文件被更新
测试 4: MAP_PRIVATE：修改映射区，munmap 后验证文件未被修改
测试 5: fork 后子进程继承映射，子进程修改 MAP_PRIVATE 不影响父进程
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| VMA（虚拟内存区域） | `struct vma` 描述每段映射的属性和关联文件 |
| 文件页缓存 | 文件映射的数据从 `readi` 读入，利用 buffer cache |
| 写回（Writeback） | MAP_SHARED munmap 时将脏页写回文件 |
| 按需分页 | mmap 仅记录 VMA，实际物理页在页错误时分配 |
| 引用计数 | `filedup`/`fileclose` 管理映射持有的文件引用 |
| 进程地址空间管理 | VMA 数组描述进程地址空间，是 Linux mm_struct 的简化 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/proc.h | 修改 | 添加 `struct vma` 和 `vmas[NVMA]` 字段 |
| src/sysfile.c | 修改 | 实现 `sys_mmap`、`sys_munmap` |
| src/trap.c | 修改 | `T_PGFLT` 中处理 VMA 的懒加载 |
| src/proc.c | 修改 | fork/exit 中处理 VMA 继承与清理 |
| include/syscall.h | 修改 | 添加 `SYS_mmap`、`SYS_munmap` 编号 |
| include/user.h | 修改 | 添加 `mmap`/`munmap` 用户态声明 |
| user/usys.S | 修改 | 添加系统调用入口 |
| user/mmaptest.c | 新增 | 完整测试套件 |

## 验证

```bash
make clean && make qemu-nox
$ mmaptest
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 匿名映射读写 | 映射区域可正常读写，munmap 后地址无效 | mmaptest 输出 |
| 文件映射读 | 指针读出的内容与 read() 一致 | mmaptest 比较两种读法 |
| MAP_SHARED 写回 | munmap 后用 read 验证文件更新 | mmaptest 验证 |
| MAP_PRIVATE 不写回 | 文件内容不变 | mmaptest 验证 |
| usertests 通过 | 已有功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| mmap 返回地址非法 | 未正确更新 `p->sz` | 确认 `addr = PGROUNDUP(p->sz); p->sz = addr + len` |
| 文件内容读取错误 | offset 计算错误 | `page_offset = PGROUNDDOWN(va) - vma->addr + vma->offset` |
| munmap 写回时 panic | inode 未加锁 | 写回前 `ilock(ip)`，完成后 `iunlock(ip)` |
| fork 后子进程 mmap 数据错误 | VMA 复制后文件引用计数未增加 | fork 中对每个 VMA 调用 `filedup` |

## 关键代码路径

- mmap 注册: `sysfile.c:sys_mmap()` → 找空闲 VMA → 填写 → 不建页表 → 返回 addr
- 懒加载: 用户访问 → `#PF` → `trap.c` → `vma_find()` → `readi`/`kalloc` → `mappages`
- munmap: `sys_munmap()` → 遍历脏页写回 → `deallocuvm` → `fileclose`
- exit 清理: `proc.c:exit()` → 遍历 `vmas[]` → 对每个有效 VMA 执行清理

## 设计权衡

| 方面 | read/write 系统调用 | mmap 文件映射 |
|------|--------------------|-----------|
| 数据拷贝 | 内核→用户 buffer 拷贝 | 零拷贝（直接访问缓存页） |
| 接口复杂度 | 简单（read/write） | 较复杂（需管理 VMA） |
| 随机访问效率 | 需要 lseek + read | 直接指针，O(1) |
| 内核实现复杂度 | 简单 | 较高（VMA + 懒加载 + 写回） |
| 适用场景 | 顺序 I/O | 随机访问、大文件、共享内存 |

## 进阶挑战

- [ ] 实现 **msync(addr, len, MS_SYNC)**：主动刷写脏页而不 munmap
- [ ] 实现 **mprotect(addr, len, prot)**：动态修改已映射区域的保护属性
- [ ] 结合 **COW**（lab-mm-02）：MAP_PRIVATE fork 后使用 COW 延迟复制
- [ ] 支持 **addr hint**：若 hint 地址空闲，优先使用（POSIX 建议行为）
- [ ] 统计 mmap 的**缺页次数**，与等价 read 操作比较系统调用次数
