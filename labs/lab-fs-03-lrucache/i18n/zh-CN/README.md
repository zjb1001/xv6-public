# Lab: LRU Buffer Cache (LRU 缓冲区缓存)

[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 的缓冲区缓存（`src/bio.c`）使用固定大小的 `NBUF=30` 个缓冲块。当所有缓冲块都被占用时，`bget` 函数需要驱逐（evict）一个"最近最少使用"的块。但原始实现仅使用**线性扫描**找到任意一个 `refcnt=0` 的块，没有真正实现 LRU 策略——驱逐哪个块取决于数组位置而非使用时间。

本实验将缓冲区链表改造为**严格 LRU 双向链表**：

- **最近使用**的块靠近链表头部（MRU 端）
- **最久未使用**的块在链表尾部（LRU 端）
- 每次 `bget` 命中后，将块移到链表头部
- 驱逐时直接从链表尾部取

核心问题：*"为什么 LRU 是缓存替换的好策略？OS 中还有哪些地方用 LRU 替换？"*

## 前置知识

- **`bio.c` 的工作原理**: `bread(dev, blockno)` 查找缓存（命中直接返回），未命中则驱逐一个块并从磁盘读入。`brelse(b)` 释放对块的引用（`refcnt--`），使其可被驱逐
- **`bcache.head`**: 原始代码中，缓冲块组成一个双向循环链表（`buf.next`/`buf.prev`），head 是哨兵节点
- **LRU 不变性**: 任何时候，链表中靠近 head 的块是最近访问的，靠近 tail（`head.prev`）的是最久未访问的
- **`bget` 的两个阶段**: 先在链表中查找（命中），若未命中则从 tail 端驱逐可用块

```
LRU 链表示意（MRU → LRU）：
head ↔ [块A(最新)] ↔ [块B] ↔ [块C] ↔ [块D(最旧)] ↔ head

命中块B: 将 B 移到 head 之后
head ↔ [块B] ↔ [块A] ↔ [块C] ↔ [块D] ↔ head

驱逐（未命中）: 从 tail（D 的前驱）方向找 refcnt==0 的块
```

## 实验内容

### 1. 理解并绘制原始链表结构 (src/bio.c)

在动手修改前，阅读 `bio.c` 的 `binit`、`bget`、`brelse`，画出：
- `bcache.head` 双向循环链表的初始状态
- `bget` 的两个循环（命中查找、驱逐查找）各自在链表中的遍历方向
- `brelse` 如何将块重新插入链表

**问题**：原始代码是否实现了严格 LRU？驱逐时选择的是"最久未使用"还是"第一个 refcnt=0"？

### 2. 在 bget 命中时将块移到 MRU 端 (src/bio.c)

在 `bget` 的命中路径（找到匹配的 dev/blockno）中，添加"移到链表头"操作：

```c
// 找到命中块 b 后，将其移到 head 之后（MRU 位置）
b->prev->next = b->next;
b->next->prev = b->prev;
// 插入 head 之后
b->next = bcache.head.next;
b->prev = &bcache.head;
bcache.head.next->prev = b;
bcache.head.next = b;
```

**注意**: 移动操作必须在 `bcache.lock` 保护下进行（已在 `bget` 中加锁）。

### 3. 在 bget 驱逐时从 LRU 端查找 (src/bio.c)

原始 `bget` 的驱逐循环**正向**遍历链表（从 head 往下），改为**反向**遍历（从 tail 往回），找到最久未使用且 `refcnt==0` 的块：

```c
// 驱逐：从 LRU 端（tail）向 MRU 端方向查找
for(b = bcache.head.prev; b != &bcache.head; b = b->prev) {
    if(b->refcnt == 0) {
        // 驱逐此块
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // 移到 MRU 端
        b->prev->next = b->next;
        b->next->prev = b->prev;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
    }
}
```

### 4. 在 brelse 时将块移到 MRU 端 (src/bio.c)

`brelse` 将 `refcnt` 减为 0 时，块重新变为可驱逐。为了让最近释放的块排在 LRU 链表的 MRU 侧（表示它"最近被用过"），在 `brelse` 的 `refcnt==0` 分支中也执行"移到 head 之后"操作：

```c
if(b->refcnt == 0) {
    // 移到 MRU 端，表示最近刚用过
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
}
```

### 5. 添加命中率统计（可选，用于验证）

在 `bcache` 结构中添加 `hit` 和 `miss` 计数器，在 `bget` 命中/未命中时分别累加，在内核输出中打印统计信息。

### 6. 编写测试验证 LRU 效果 (user/lrucachetest.c)

设计一个访问模式，使 LRU 的命中率明显优于随机替换：
- 顺序访问 20 个文件（热点），然后访问一个冷块，再访问热点
- 统计 LRU 下的磁盘 I/O 次数（通过 miss 计数），对比随机替换

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 缓冲区缓存 | bio.c：内核维护的磁盘块内存缓存，减少磁盘 I/O |
| LRU 替换策略 | 最近最少使用的块被优先驱逐，基于时间局部性原理 |
| 双向链表维护 | 每次命中/释放都调整链表顺序，O(1) 操作 |
| 引用计数 | `refcnt > 0` 的块正在使用，不可驱逐 |
| 时间局部性 | 最近访问过的块大概率很快再次被访问（LRU 的理论基础） |
| 锁的粒度 | `bcache.lock` 保护整个缓存，高并发时可考虑分片 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| src/bio.c | 修改 | `bget` 命中移到 MRU，驱逐从 LRU 端；`brelse` 移到 MRU 端 |
| include/buf.h | 修改（可选） | 在 `bcache` 结构中添加 `hit`/`miss` 计数器 |
| user/lrucachetest.c | 新增 | 验证 LRU 命中率的测试程序 |
| Makefile | 修改 | 添加 `lrucachetest` |

## 验证

```bash
make clean && make qemu-nox
$ lrucachetest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| LRU 顺序正确 | 驱逐最久未使用块（链表尾端） | 在 bget 中打印被驱逐的 blockno |
| 命中率提升 | LRU 比原始实现命中率高 | hit/miss 计数器对比 |
| 并发安全 | CPUS=2 下无崩溃 | `make qemu-nox CPUS=2` + usertests |
| usertests 通过 | 文件系统功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| bget panic "no buffers" | 驱逐循环方向错误，跳过了 refcnt=0 的块 | 确认反向遍历：`b = bcache.head.prev` |
| 链表损坏导致死循环 | 移动操作时前后指针修改顺序错误 | 严格按：先断开旧链接，再插入新位置 |
| 命中率未提升 | bget 命中时忘记将块移到 MRU 端 | 检查命中分支是否有移动操作 |
| 死锁 | 在 acquiresleep 之前未 release(bcache.lock) | 检查 bget 中的锁操作顺序 |

## 关键代码路径

- 缓存命中: `bio.c:bget()` → 找到 dev/blockno → 移到 MRU 端 → 返回
- 缓存未命中: `bget()` → 从 LRU 端向前找 refcnt=0 → 驱逐 → 移到 MRU 端 → 返回
- 释放缓冲: `bio.c:brelse()` → `refcnt--` → 若为 0 → 移到 MRU 端

## 设计权衡

| 方面 | 原始（无序链表） | LRU 双向链表 |
|------|----------------|-------------|
| 命中时操作 | 无（不调整顺序） | 移到 MRU 端（O(1) 链表操作） |
| 驱逐策略 | 任意 refcnt=0 | 最久未使用（严格 LRU） |
| 命中率 | 取决于初始化顺序 | 显著高于随机替换（局部性原理） |
| 实现复杂度 | 简单 | 稍复杂（需正确维护双向链表顺序） |
| 并发扩展 | 单全局锁 | 单全局锁（可进一步拆分为哈希 + 桶锁） |

## 进阶挑战

- [ ] 实现 **哈希桶 + 桶级锁**：将 bcache 按 `blockno % NBUCKETS` 分片，减少锁争用
- [ ] 实现 **Clock 算法**（近似 LRU）：每块维护一个 referenced 位，顺时针扫描替换
- [ ] 统计不同访问模式（顺序、随机、Zipf 分布）下 LRU 与 Clock 的命中率对比
- [ ] 实现 **Write-Back 缓存**：`brelse` 时不立即写磁盘，在驱逐时才写（注意崩溃一致性）
- [ ] 研究 **2Q 算法**（Linux page cache 使用）：区分"第一次访问"和"多次访问"，防止大文件顺序读把热数据挤出
