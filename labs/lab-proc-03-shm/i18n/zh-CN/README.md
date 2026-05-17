# Lab: Shared Memory IPC (共享内存进程间通信)
[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 进程间通信（IPC）目前只有**管道（pipe）**：单向字节流，内核缓冲，需要系统调用进行读写。对于需要高频、大量数据交换的场景，每次数据传输都要经过内核缓冲区的拷贝，性能受限。

**共享内存（Shared Memory）** 是最快的 IPC 机制：

- 两个进程的虚拟地址空间中，有一段范围映射到**同一块物理页**
- 进程可以直接读写共享内存，**无需系统调用**、无需数据拷贝
- 需要额外的同步机制（锁、信号量）保护并发访问

本实验实现 System V 风格的共享内存接口：`shmget`/`shmat`/`shmdt`。

核心问题：*"两个进程的页表都指向同一物理地址是否安全？谁来管理这块内存的生命周期？"*

## 前置知识

- **页表共享**: 两个进程的不同虚拟地址，在页表中映射到同一物理页帧（物理地址相同），修改物理内存后双方都能看到
- **引用计数**: 共享物理页在所有进程 `shmdt` 前不能被 `kfree`，需要引用计数（类比 `lab-mm-02` COW 引用计数）
- **命名对象**: `shmget(key, size, flags)` 按 key 创建或查找共享内存对象，类比文件通过路径名查找
- **System V IPC**: Unix 的一套传统 IPC 接口（shmget/msgsnd/semop），现代系统逐渐被 POSIX IPC 和 mmap 替代

```
共享内存映射示意:
进程 A 地址空间:         进程 B 地址空间:
  VA 0x6000 ──────┐       VA 0x8000 ──────┐
                  │                       │
                  ▼                       ▼
              物理页帧 PA 0xC000 (共享内存)
              refcnt = 2

A 写 *(char*)0x6000 = 'X' → B 读 *(char*)0x8000 == 'X'
```

## 实验内容

### 1. 定义共享内存对象 (src/shm.c / include/shm.h)

```c
#define NSHM  16        // 最大共享内存段数
#define SHM_MAXSIZE  (4 * PGSIZE)  // 单个段最大 16KB

struct shmobj {
    int     valid;      // 是否在使用
    int     key;        // 标识符（进程间约定的整数 key）
    uint    size;       // 大小（字节，页对齐）
    char   *pages[4];   // 指向物理页（最多 4 页）
    int     npages;     // 实际页数
    int     refcnt;     // 有多少个 shmat 引用
    struct  spinlock lock;
};

struct shmobj shmtable[NSHM];   // 全局共享内存表
struct spinlock shmtable_lock;
```

### 2. 实现 shmget（创建或获取共享内存段）(src/sysproc.c)

```c
int sys_shmget(void) {
    int key, size, flags;
    if(argint(0, &key) < 0 || argint(1, &size) < 0 || argint(2, &flags) < 0)
        return -1;

    acquire(&shmtable_lock);
    // 1. 先查找已有的 key
    for(int i = 0; i < NSHM; i++) {
        if(shmtable[i].valid && shmtable[i].key == key) {
            int id = i;
            release(&shmtable_lock);
            return id;  // 返回已有段的 id
        }
    }
    // 2. 创建新段
    for(int i = 0; i < NSHM; i++) {
        if(!shmtable[i].valid) {
            shmtable[i].valid = 1;
            shmtable[i].key = key;
            shmtable[i].size = PGROUNDUP(size);
            shmtable[i].refcnt = 0;
            shmtable[i].npages = shmtable[i].size / PGSIZE;
            // 分配物理页
            for(int j = 0; j < shmtable[i].npages; j++) {
                shmtable[i].pages[j] = kalloc();
                if(!shmtable[i].pages[j]) {
                    // 回滚并失败
                    for(int k = 0; k < j; k++) kfree(shmtable[i].pages[k]);
                    shmtable[i].valid = 0;
                    release(&shmtable_lock);
                    return -1;
                }
                memset(shmtable[i].pages[j], 0, PGSIZE);
            }
            release(&shmtable_lock);
            return i;  // 返回新段 id
        }
    }
    release(&shmtable_lock);
    return -1;  // 段表已满
}
```

### 3. 定义进程 SHM 附加记录 (include/proc.h)

```c
#define NATTACH  8    // 每个进程最多附加 8 个共享内存段

struct shmattach {
    int   shmid;    // -1 = 空槽
    uint  addr;     // 映射到的虚拟地址
};

struct proc {
    // ... 现有字段 ...
    struct shmattach shm_attach[NATTACH];
};
```

### 4. 实现 shmat（将共享内存附加到进程地址空间）(src/sysproc.c)

```c
void* sys_shmat(void) {
    int shmid, shmflg;
    void *shmaddr;
    if(argint(0, &shmid) < 0 || argptr(1, (char**)&shmaddr, 0) < 0
       || argint(2, &shmflg) < 0) return (void*)-1;
    if(shmid < 0 || shmid >= NSHM || !shmtable[shmid].valid) return (void*)-1;

    struct proc *p = myproc();
    struct shmobj *shm = &shmtable[shmid];

    // 选择映射地址（忽略 shmaddr hint，使用 p->sz 之上）
    uint addr = PGROUNDUP(p->sz);
    p->sz = addr + shm->size;

    // 建立页表映射
    uint perm = PTE_U | PTE_W;
    for(int i = 0; i < shm->npages; i++) {
        if(mappages(p->pgdir, (void*)(addr + i * PGSIZE), PGSIZE,
                    V2P(shm->pages[i]), perm) < 0) {
            // 回滚
            p->sz -= shm->size;
            return (void*)-1;
        }
    }

    // 记录附加信息
    for(int i = 0; i < NATTACH; i++) {
        if(p->shm_attach[i].shmid == -1) {
            p->shm_attach[i].shmid = shmid;
            p->shm_attach[i].addr  = addr;
            break;
        }
    }

    acquire(&shmtable_lock);
    shm->refcnt++;
    release(&shmtable_lock);

    return (void*)addr;
}
```

### 5. 实现 shmdt（解除共享内存附加）(src/sysproc.c)

```c
int sys_shmdt(void) {
    void *shmaddr;
    if(argptr(0, (char**)&shmaddr, 0) < 0) return -1;
    struct proc *p = myproc();

    for(int i = 0; i < NATTACH; i++) {
        if(p->shm_attach[i].shmid != -1 && p->shm_attach[i].addr == (uint)shmaddr) {
            int shmid = p->shm_attach[i].shmid;
            struct shmobj *shm = &shmtable[shmid];
            // 解除页表映射（不释放物理页！）
            for(int j = 0; j < shm->npages; j++) {
                pte_t *pte = walkpgdir(p->pgdir, (void*)((uint)shmaddr + j*PGSIZE), 0);
                if(pte) *pte = 0;
            }
            p->shm_attach[i].shmid = -1;
            p->shm_attach[i].addr  = 0;
            // 减少引用计数，若为 0 则释放共享内存对象
            acquire(&shmtable_lock);
            shm->refcnt--;
            if(shm->refcnt == 0) {
                for(int j = 0; j < shm->npages; j++) kfree(shm->pages[j]);
                shm->valid = 0;
            }
            release(&shmtable_lock);
            return 0;
        }
    }
    return -1;
}
```

### 6. 进程退出时自动 shmdt (src/proc.c)

在 `exit()` 中遍历 `shm_attach[]`，对所有有效附加执行 shmdt 操作。

### 7. 编写测试 (user/shmtest.c)

```
测试 1: 同一个 key，两次 shmget 返回同一 id
测试 2: 父进程 shmget+shmat，fork 后子进程能看到父进程写入的数据
        （fork 需要 copyuvm，验证共享内存页不被复制）
测试 3: 两个无关进程通过相同 key 的 shmget 共享数据（需要先后启动）
测试 4: 最后一个 shmdt 后，shmget 同一 key 返回空段（内存被释放）
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 页表共享 | 两个进程的 PTE 指向同一物理页帧 |
| IPC（进程间通信） | 共享内存是最快的 IPC，无需拷贝 |
| 引用计数 | refcnt 管理共享内存的生命周期 |
| 命名对象 | key 是进程间约定的整数标识符 |
| 地址空间管理 | shmat 在进程的虚拟地址空间中分配新区域 |
| fork 的特殊处理 | 共享内存页在 fork 时不应被复制（区别于私有页） |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| src/shm.c | 新增 | 共享内存表和 `shminit()` |
| include/shm.h | 新增 | `struct shmobj` 定义 |
| include/proc.h | 修改 | 添加 `shm_attach[]` 字段 |
| src/sysproc.c | 修改 | 实现 `sys_shmget`/`sys_shmat`/`sys_shmdt` |
| src/proc.c | 修改 | `allocproc` 初始化 `shm_attach`，`exit` 自动 shmdt，`fork` 处理共享内存继承 |
| src/main.c | 修改 | 调用 `shminit()` |
| include/syscall.h | 修改 | 添加系统调用编号 |
| user/shmtest.c | 新增 | 共享内存 IPC 测试 |

## 验证

```bash
make clean && make qemu-nox
$ shmtest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 数据共享 | 进程 A 写，进程 B 立即读到 | shmtest 验证 |
| 引用计数 | 最后一个 shmdt 后物理页被释放 | 添加 kalloc 计数验证 |
| fork 继承 | 子进程能访问父进程的 shmat 区域 | shmtest fork 测试 |
| usertests 通过 | 已有功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 进程 A 写的数据进程 B 看不到 | TLB 未刷新 | shmat 后调用 `lcr3(V2P(pgdir))` 刷新 TLB |
| 引用计数错误 | fork 后子进程也引用共享页，但 refcnt 未增加 | fork 中遍历 shm_attach，对每个有效附加 refcnt++ |
| shmdt 后页仍可访问 | 页表项未清零，或 TLB 未刷新 | shmdt 中 `*pte = 0` 后调用 `lcr3` |

## 关键代码路径

- 创建共享段: `sys_shmget()` → 查 key → 未找到 → `kalloc` 物理页 → 返回 id
- 附加到地址空间: `sys_shmat()` → 选择 VA → `mappages` → 记录 `shm_attach` → `refcnt++`
- 解除附加: `sys_shmdt()` → 找 addr → 清空 PTE → `refcnt--` → 若 0 则 kfree

## 设计权衡

| 方面 | 管道（pipe） | 共享内存（shm） |
|------|------------|--------------|
| 数据拷贝 | 2 次（写→内核，内核→读） | 0 次（直接访问同一物理页） |
| 同步 | 内置（read 阻塞直到有数据） | 无内置，需要额外同步 |
| 通信方向 | 单向 | 双向 |
| 持久性 | 无（进程退出自动释放） | 显式 shmdt 控制 |
| 适用场景 | 流式数据传输 | 高频、大量数据交换 |

## 进阶挑战

- [ ] 结合 **lab-sync-03-semaphore**：用信号量同步共享内存的并发访问（经典生产者-消费者）
- [ ] 实现 **shmctl(IPC_RMID)**：强制删除共享内存段（即使 refcnt > 0，标记删除，待所有 shmdt 后释放）
- [ ] 实现 **权限控制**：shmget 创建时指定访问权限，shmat 检查权限（结合 lab-userspace）
- [ ] 实现 **POSIX 命名共享内存**（`shm_open`/`mmap`）接口，基于 mmap（结合 lab-mm-04）
- [ ] 统计共享内存 IPC 与管道 IPC 的**吞吐量对比**（传输相同数据量的系统调用次数和耗时）
