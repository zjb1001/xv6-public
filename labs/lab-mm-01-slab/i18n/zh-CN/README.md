# Lab: Slab Allocator (Slab 内核对象分配器)

[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 的 `kalloc()` 实现极其简单：维护一个物理页帧（4KB）的空闲链表，每次分配或释放整整一页。当内核需要分配一个只有几十字节的小对象（如 `struct proc`、`struct file`、`struct buf`）时，kalloc 的粗粒度导致了严重的**内部碎片**——每个小对象都占用一整页，绝大部分空间被浪费。

Linux 内核使用 **Slab 分配器** 解决这一问题：

- 为每种固定大小的对象维护一个**缓存（kmem_cache）**
- 每个缓存由若干 **Slab**（一组连续物理页）组成，每个 Slab 预先切割为等大小的 slot
- 分配时从 Slab 的空闲 slot 链表取出一个，释放时归还链表——无需内存初始化

核心问题：*"为什么 Linux 不直接用 malloc 分配内核对象？Slab 有什么本质优势？"*

## 前置知识

- **物理内存分配**: xv6 `kalloc()`/`kfree()` 在 `src/kalloc.c`，管理 `[end, PHYSTOP)` 的空闲页链表
- **内部碎片**: 分配单元（页）远大于请求大小，浪费的空间称为内部碎片
- **Cache Coloring**: Slab 为不同 Slab 设置不同的起始偏移，错开 CPU 缓存行，减少 false sharing
- **对象构造/析构**: Slab 可以在分配/释放时调用构造/析构函数（本实验简化，不实现）

```
kmem_cache 结构示意:
kmem_cache["proc"] (obj_size=sizeof(struct proc))
  │
  ├── Slab 1 (4KB 物理页)
  │   ├── slot 0 [已分配]
  │   ├── slot 1 [空闲] ─── free_list
  │   ├── slot 2 [空闲] ─── ...
  │   └── ... (共 4096 / sizeof(struct proc) 个 slot)
  │
  └── Slab 2 (4KB 物理页)
      └── ...
```

## 实验内容

### 1. 定义 Slab 数据结构 (src/kalloc.c / include/defs.h)

```c
struct slab_obj {
    struct slab_obj *next;   // 空闲 slot 链表
};

struct slab {
    struct slab     *next;   // Slab 链表
    void            *mem;    // 指向分配的物理页
    uint             nfree;  // 本 Slab 中空闲 slot 数
    struct slab_obj *freelist; // 空闲 slot 链表头
};

struct kmem_cache {
    char            name[16]; // 调试用名称
    uint            obj_size; // 每个对象大小（字节，对齐到 8）
    uint            objs_per_slab; // 每个 Slab 能容纳的对象数
    struct slab    *slabs;    // Slab 链表头
    struct spinlock lock;
};
```

**关键约束**: `obj_size` 必须 >= `sizeof(struct slab_obj*)`（空闲时 slot 本身存放 next 指针）

### 2. 实现 kmem_cache 初始化

```c
struct kmem_cache* kmem_cache_create(char *name, uint obj_size)
```

- 从静态数组中分配一个 `kmem_cache` 结构（避免鸡生蛋问题）
- 计算 `objs_per_slab = PGSIZE / ALIGN8(obj_size)`
- 初始化自旋锁，`slabs = 0`

### 3. 实现 Slab 增长（从 kalloc 获取新页）

```c
static struct slab* slab_grow(struct kmem_cache *c)
```

- 调用 `kalloc()` 申请一页物理内存作为新 Slab 的 `mem`
- 用 `kalloc()` 再申请一小页存放 `struct slab` 元数据（或将元数据嵌入 mem 首部）
- 将 mem 按 `obj_size` 切割，构建 slot 的空闲链表：
  ```c
  for(i = 0; i < objs_per_slab; i++) {
      obj = (struct slab_obj*)((char*)mem + i * obj_size);
      obj->next = freelist;
      freelist = obj;
  }
  ```
- 将新 Slab 插入 `c->slabs` 链表头

### 4. 实现分配和释放

```c
void* kmem_cache_alloc(struct kmem_cache *c)
```
- 加锁，遍历 Slab 链表找有空闲 slot 的 Slab（`nfree > 0`）
- 从 `freelist` 摘出一个 slot，`nfree--`
- 若所有 Slab 均满，调用 `slab_grow` 增加新 Slab
- 返回 slot 指针

```c
void  kmem_cache_free(struct kmem_cache *c, void *obj)
```
- 加锁，找到 `obj` 所属的 Slab（地址范围判断）
- 将 obj 作为 `slab_obj` 插入 `freelist`，`nfree++`
- 若 Slab 全部空闲，可选择将整个 Slab 归还给 kalloc

### 5. 集成到内核 — 替换部分 kalloc 调用

创建以下全局缓存并替换内核中对应的分配：

| 缓存名 | 替换对象 | 改动文件 |
|--------|---------|---------|
| `kmem_proc` | `struct proc` 分配 | src/proc.c |
| `kmem_file` | `struct file` 分配 | src/file.c |

### 6. 编写测试（内核 panic 输出统计）

在内核启动时输出各缓存统计信息：
```
slab: proc cache: obj_size=144 objs_per_slab=28
slab: file cache: obj_size=36 objs_per_slab=113
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 内部碎片 | kalloc 每次分配整页，小对象浪费严重 |
| Slab 分配器 | 固定大小对象池，O(1) 分配/释放 |
| 对象缓存 | kmem_cache 按对象类型维护独立空闲链表 |
| 自旋锁保护 | 每个缓存独立加锁，减少锁竞争 |
| 物理页管理 | Slab 从 kalloc 批量获取页，再细分给小对象 |
| Cache Coloring | Slab 起始偏移错开，利用 CPU cache 局部性 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| src/kalloc.c | 修改 | 添加 Slab 实现：`kmem_cache_*` 系列函数 |
| include/defs.h | 修改 | 导出 `kmem_cache_create`、`kmem_cache_alloc`、`kmem_cache_free` |
| src/proc.c | 修改 | 用 `kmem_cache_alloc(kmem_proc)` 替换 `kalloc()` |
| src/file.c | 修改 | 用 `kmem_cache_alloc(kmem_file)` 替换 `kalloc()` |
| src/main.c | 修改 | 在 `kinit` 后初始化全局 Slab 缓存 |

## 验证

### 编译和运行

```bash
make clean && make qemu-nox
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 内核正常启动 | xv6 启动成功，shell 可用 | 观察启动输出 |
| Slab 统计 | 启动时打印缓存信息 | 串口输出 |
| 进程/文件正常 | `ls`、`cat`、`fork` 等正常工作 | 运行 usertests |
| 内存节省 | 相同进程数时物理页使用量减少 | 添加 `kfree_count` 统计 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 内核 panic: kalloc failed | Slab 增长时 kalloc 返回 0 | 检查物理内存是否耗尽，减小测试规模 |
| 对象数据被覆盖 | slot 被用作链表节点后未清零，分配时未清零 | 分配前 memset(0) |
| double free | free 后 freelist 链接出错 | 断言 slot 不在 freelist 中 |
| Slab 查找 obj 失败 | 地址范围判断错误 | 用 `PGROUNDDOWN(obj) == slab->mem` 定位 |

## 关键代码路径

- 初始化: `main.c:main()` → `kmem_cache_create("proc", sizeof(struct proc))`
- 分配: `proc.c:allocproc()` → `kmem_cache_alloc(kmem_proc)` → 取空闲 slot
- 扩容: `kmem_cache_alloc()` → `slab_grow()` → `kalloc()` → 切割 slot 链表
- 释放: `proc.c:freeproc()` → `kmem_cache_free(kmem_proc, p)` → 归还 freelist

## 设计权衡

| 方面 | kalloc（按页分配） | Slab 分配器 |
|------|-------------------|-----------|
| 最小分配粒度 | 4096 字节（一页） | `obj_size` 字节 |
| 分配速度 | O(1) | O(1)（有 Slab 时） |
| 内部碎片 | 极高（小对象） | 极低（对象紧密排列） |
| 实现复杂度 | 极简 | 中等 |
| 元数据开销 | 无 | Slab 头部（可嵌入页内） |

## 进阶挑战

- [ ] 实现**全部空闲 Slab 回收**：当 Slab 全部 slot 空闲时，归还物理页给 kalloc
- [ ] 实现**通用大小 Slab**（类似 Linux 的 kmalloc）：预设 8/16/32/.../4096 字节的缓存
- [ ] 统计**缓存命中率**（分配时不需要 slab_grow 的比例），输出性能报告
- [ ] 为 `struct buf`（buffer cache）也引入 Slab 缓存
- [ ] 研究 **Cache Coloring**：为不同 Slab 设置不同起始偏移，测量其对 cache miss 的影响
