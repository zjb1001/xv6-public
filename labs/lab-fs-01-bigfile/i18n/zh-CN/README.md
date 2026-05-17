# Lab: Large File Support (大文件双重间接块)

[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

xv6 的 inode 结构（`include/fs.h`）支持 12 个直接块 + 1 个间接块（128 个块指针）：

```
最大文件大小 = (12 + 128) × 512 = 71,680 字节 ≈ 70KB
```

这意味着 xv6 无法存储超过 70KB 的文件——连一张普通图片都放不下。真实的 Unix 文件系统（UFS、ext2）使用**双重间接块（doubly indirect block）**：一个块存储间接块的指针，每个间接块再存储数据块的指针，级联两层。

本实验将 xv6 的最大文件大小扩展到：
```
12（直接）+ 128（单级间接）+ 128×128（双级间接）= 16,396 个块 ≈ 8MB
```

核心问题：*"inode 的大小是固定的，磁盘块地址是怎么通过有限的指针支持大文件的？"*

## 前置知识

- **xv6 inode 结构**: `include/fs.h` 中 `struct dinode`，`addrs[NDIRECT+1]` 存储块号，最后一个是间接块指针
- **间接寻址**: 间接块本身不存数据，它的内容是数据块的块号数组（`uint addrs[NINDIRECT]`，NINDIRECT=128）
- **`bmap(ip, bn)` 函数**: `src/fs.c` 中，将文件的逻辑块号 `bn` 转换为磁盘物理块号，是文件系统寻址的核心
- **`itrunc(ip)` 函数**: 截断文件时释放所有数据块，需要递归释放间接块

```
原始 inode 地址结构:
addrs[0..11]  → 直接块（12个）
addrs[12]     → 单级间接块 → [addr0, addr1, ..., addr127]

扩展后:
addrs[0..11]  → 直接块（12个）
addrs[12]     → 单级间接块 → [addr0..addr127]
addrs[13]     → 双级间接块 → [iblk0, iblk1, ..., iblk127]
                              每个 iblk → [addr0..addr127]
```

## 实验内容

### 1. 修改 inode 常量 (include/fs.h)

```c
#define NDIRECT   12
#define NINDIRECT (BSIZE / sizeof(uint))           // = 128
#define NDINDIRECT (NINDIRECT * NINDIRECT)          // = 16384 (新增)
#define MAXFILE   (NDIRECT + NINDIRECT + NDINDIRECT) // = 16524

// dinode 的 addrs 从 NDIRECT+1 扩展到 NDIRECT+2
struct dinode {
    // ...
    uint addrs[NDIRECT + 2];   // 原来是 NDIRECT+1
};
```

**同步修改** `include/file.h` 中内存 inode `struct inode` 的 `addrs` 字段大小。

### 2. 修改 bmap：处理双重间接 (src/fs.c)

`bmap(ip, bn)` 当前处理直接块和单级间接块。需要在末尾添加双重间接的处理：

```c
// 双重间接块处理
bn -= NINDIRECT;
if(bn < NDINDIRECT) {
    // 第一级：从 addrs[NDIRECT+1] 找到双重间接块
    // 第二级：在双重间接块中找到对应的间接块
    // 第三级：在间接块中找到数据块
    uint idx1 = bn / NINDIRECT;    // 双级间接块内的间接块索引
    uint idx2 = bn % NINDIRECT;    // 间接块内的数据块索引
    // 如果中间块不存在，调用 balloc 分配并清零
    // ...
    return addr;
}
panic("bmap: out of range");
```

**提示**: 需要两次 `bread`/`brelse` 操作——第一次读双重间接块，第二次读间接块。修改时要用 `log_write` 标记 dirty。

### 3. 修改 itrunc：递归释放双重间接块 (src/fs.c)

`itrunc` 目前释放直接块和单级间接块。需要在末尾添加双重间接块的释放逻辑：

```c
// 释放双重间接块
if(ip->addrs[NDIRECT+1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    for(j = 0; j < NINDIRECT; j++) {
        if(((uint*)bp->data)[j]) {
            // 释放每个间接块中的所有数据块
            struct buf *bp2 = bread(ip->dev, ((uint*)bp->data)[j]);
            for(k = 0; k < NINDIRECT; k++) {
                if(((uint*)bp2->data)[k])
                    bfree(ip->dev, ((uint*)bp2->data)[k]);
            }
            brelse(bp2);
            bfree(ip->dev, ((uint*)bp->data)[j]); // 释放间接块
        }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);         // 释放双重间接块
    ip->addrs[NDIRECT+1] = 0;
}
```

### 4. 更新 mkfs 和磁盘大小 (tools/mkfs.c / Makefile)

- `mkfs.c` 中的 `MAXFILE` 和 inode 结构定义需要同步更新
- 磁盘镜像大小可能需要增加（在 Makefile 中调整 `QEMUOPTS` 的磁盘大小）

### 5. 编写测试程序 (user/bigfiletest.c)

```c
// 测试 1: 写入恰好超过原始最大大小的文件
// 测试 2: 写入接近新最大大小的文件（8MB）
// 测试 3: seek + 读取验证数据完整性
// 测试 4: 大文件 truncate（itrunc）后磁盘块正确释放
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 间接寻址 | inode 的 `addrs` 存指针的指针，两级级联寻址 |
| 磁盘块号 | 所有地址都是逻辑块号，`balloc`/`bfree` 管理空闲位图 |
| 缓冲区缓存 | `bread` 读块，`bwrite`/`log_write` 写块，`brelse` 释放 |
| 日志（WAL） | 修改元数据块必须通过 `log_write`，crash-safe |
| 文件系统一致性 | itrunc 需完整释放所有层级的块，防止磁盘块泄漏 |
| mkfs | 磁盘格式化工具，需与内核的 inode 格式保持一致 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/fs.h | 修改 | 添加 `NDINDIRECT`，修改 `MAXFILE`，`addrs[NDIRECT+2]` |
| include/file.h | 修改 | 同步修改内存 inode 的 `addrs` 大小 |
| src/fs.c | 修改 | `bmap` 添加双重间接处理，`itrunc` 添加双重间接释放 |
| tools/mkfs.c | 修改 | 同步 `MAXFILE` 和 inode 结构 |
| user/bigfiletest.c | 新增 | 大文件读写测试 |
| Makefile | 修改 | 添加 `bigfiletest` |

## 验证

```bash
make clean && make qemu-nox
$ bigfiletest
$ usertests     # 确认不破坏已有文件系统功能
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 超过 70KB 写入成功 | 写 80KB 文件不返回错误 | bigfiletest 输出 |
| 读写一致 | seek + read 内容与 write 一致 | bigfiletest 数据校验 |
| itrunc 正确 | 大文件删除后磁盘空间释放 | 删除后 df 或手动检查 bitmap |
| usertests 通过 | 已有功能不受影响 | usertests 全 PASS |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| bmap panic "out of range" | MAXFILE 或 bmap 的边界计算错误 | 仔细检查 `bn -= NINDIRECT` 的位置 |
| 数据读写不一致 | bmap 返回错误物理块号 | 在 bmap 中 cprintf 调试 idx1/idx2 |
| itrunc 后磁盘块泄漏 | 双重间接块未完整释放 | 检查三层释放逻辑是否完整 |
| mkfs 编译错误 | mkfs.c 与 fs.h 不同步 | 确认 mkfs.c 中的 NDIRECT/addrs 与 fs.h 一致 |

## 关键代码路径

- 写大文件: `sysfile.c:sys_write()` → `filewrite()` → `writei()` → `bmap(bn)` → 分配块
- 双重寻址: `fs.c:bmap()` → 计算 idx1/idx2 → `bread` 双重间接块 → `bread` 间接块 → 返回数据块号
- 释放大文件: `sys_unlink()` → `iput()` → `itrunc()` → 三层循环 bfree

## 设计权衡

| 方面 | 原始（12+1级间接） | 扩展（12+1+2级间接） |
|------|--------------------|---------------------|
| 最大文件大小 | ~70KB | ~8MB |
| 普通文件访问 | 最多 1 次 bread（间接块） | 不变（直接块路径未改） |
| 大文件访问 | 不支持 | 额外 1 次 bread（双重间接路径） |
| inode 磁盘结构 | addrs[13] | addrs[14]（多 4 字节） |
| 实现复杂度 | 简单 | 中等（两层循环） |

## 进阶挑战

- [ ] 添加**三重间接块**，将最大文件扩展到 1GB
- [ ] 实现 **`stat` 输出块数**（`struct stat` 的 `blocks` 字段）
- [ ] 测量大文件的随机读**访问延迟**（直接块 vs 单级间接 vs 双级间接的 I/O 次数对比）
- [ ] 研究 ext2 如何使用相同原理支持 2TB 文件，对比 inode 结构差异
