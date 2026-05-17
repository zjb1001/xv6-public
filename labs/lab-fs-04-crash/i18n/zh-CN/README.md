# Lab: Crash Consistency Experiment (崩溃一致性实验)

[English](../../README.md)

难度: ★★★★★

## 设计初衷

文件系统必须保证：即使电源在任意时刻断掉，重启后文件系统处于一致状态——要么操作全部完成，要么全部没有发生，绝对不能处于中间状态。

xv6 通过**预写日志（Write-Ahead Log, WAL）**实现崩溃一致性：`begin_op`/`end_op` 包裹所有文件系统修改，`commit()` 先将修改写入日志区，再写入实际磁盘位置，重启时检查日志并重放。

本实验是一个**破坏性实验**：

1. **分析**日志机制（`src/log.c`）在哪个时间点崩溃是安全的，哪个时间点崩溃会导致不一致
2. **注入**人为崩溃（在 `commit()` 的关键位置调用 `panic()`）
3. **观察**并分析不一致的磁盘状态
4. **验证**日志恢复能正确将磁盘恢复到一致状态

核心问题：*"为什么写两次（写日志 + 写数据）反而比只写一次更安全？"*

## 前置知识

- **xv6 日志结构**: 磁盘布局中，日志区紧跟 superblock，包含日志头（logheader）+ 日志块（log blocks）。`include/fs.h` 中 `struct logheader`，`src/log.c` 完整实现
- **`commit()` 的四步**: 1. `write_log()`（将修改块写入磁盘日志区）→ 2. `write_head()`（将日志头写入磁盘，标志提交点）→ 3. `install_trans()`（从日志区复制到实际位置）→ 4. 清空日志头
- **提交点（Commit Point）**: `write_head()` 完成瞬间，是唯一的"原子性保证点"。此前崩溃：重启后无日志，不重放；此后崩溃：重启后发现日志，重放完成
- **`recover_from_log()`**: 系统启动时由 `initlog()` 调用，检查日志头，若有未完成的提交则重放

```
commit() 四步时序与崩溃安全性:
Step 1: write_log()   ← 崩溃: 日志头未写 → 无提交 → 重启不重放 → 安全
Step 2: write_head()  ← 崩溃: 原子写头 → 提交点
Step 3: install_trans ← 崩溃: 日志头存在 → 重启重放 → 安全
Step 4: 清空日志头    ← 崩溃: 数据已写 → 重放幂等 → 安全
```

## 实验内容

### 1. 阅读并注释日志代码 (src/log.c)

在开始实验之前，逐行阅读 `log.c` 并回答：

- `log.lh.n` 记录的是什么？它的值何时被修改？
- 为什么 `write_head()` 只需要一次 `bwrite` 就能保证原子性？
- `install_trans` 的 `recovering` 参数有什么用？

### 2. 场景分析：崩溃点的安全性

对以下四个崩溃点，分析磁盘会处于什么状态，重启后能否恢复：

| 崩溃点 | 已完成 | 未完成 | 重启行为 | 磁盘状态 |
|--------|-------|-------|---------|---------|
| A: write_log 之后，write_head 之前 | 日志数据已写 | 日志头未写 | 无提交，不重放 | ? |
| B: write_head 之后，install_trans 之前 | 日志头已写 | 数据未安装 | 发现提交，重放 | ? |
| C: install_trans 中间（部分写） | 部分数据已写 | 部分未写 | 发现提交，重放全部 | ? |
| D: install_trans 之后，清空头之前 | 数据全部已写 | 头未清空 | 发现提交，重放（幂等） | ? |

**问题**：哪个场景是"不一致窗口"？日志如何消除这个窗口？

### 3. 在崩溃点 B 注入 panic，观察磁盘

在 `src/log.c:commit()` 中，`write_head()` 完成后立即插入：

```c
static void commit() {
    if(log.lh.n > 0) {
        write_log();
        write_head();    // 提交点
        // === 注入崩溃 ===
        // 取消注释以下行，触发 B 点崩溃:
        // panic("crash-experiment: B point");
        install_trans(0);
        log.lh.n = 0;
        write_head();
    }
}
```

**观察步骤**:

```bash
# 1. 启动 xv6，执行一个文件操作（如 echo hello > test.txt），崩溃发生
make qemu-nox
# 触发崩溃后，QEMU 退出
# 2. 使用 qemu-img 或自制工具检查磁盘镜像
# 3. 重新启动 xv6（不注入崩溃），观察 recover_from_log 的输出
```

### 4. 去掉日志：直接写，观察不一致

修改 `end_op()` 中的 `commit()` 调用，改为**直接**调用 `install_trans` 而不写日志头（即跳过 `write_head()`，绕过原子性保证）：

```c
// 危险实验：绕过日志，直接写数据（仅用于研究，不可用于生产）
static void dangerous_commit() {
    if(log.lh.n > 0) {
        write_log();
        // 故意跳过 write_head()！
        install_trans(0);   // 直接将日志数据写入实际位置
        log.lh.n = 0;
        // 在任意中间位置 panic
    }
}
```

- 在 `install_trans` 中间注入 `panic`
- 重启 xv6，观察文件系统是否出现不一致（例如：inode 分配但目录项未写 → 孤儿 inode）

### 5. 编写磁盘镜像检查工具 (tools/fscheck.c)

在宿主机上运行的简单 fsck 工具，读取 `build/fs.img`：

```c
// 检查内容：
// 1. superblock 的 inode 数量与 bitmap 是否一致
// 2. 每个 inode 的 nlink 与目录中引用该 inode 的次数是否匹配
// 3. 日志头（logheader）中是否有未完成的提交
```

**提示**: 参考 `tools/mkfs.c` 中对磁盘格式的读取方式。

### 6. 验证日志重放的正确性

对场景 B 的崩溃：

1. 触发崩溃 → QEMU 退出
2. 不修改磁盘镜像，直接重启 xv6
3. 在 `recover_from_log` 中添加 `cprintf` 输出，确认它被调用且成功重放
4. 验证崩溃前的文件操作已完整完成（或完整未发生）

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 预写日志（WAL） | 先写日志区，再写数据区；提交点原子性 |
| 崩溃一致性 | 任意时刻掉电，重启后 FS 处于一致状态 |
| 幂等性（Idempotency） | 日志重放可以安全执行多次，结果相同 |
| 原子性 | `write_head()` 是唯一原子操作（单块写） |
| 事务（Transaction） | `begin_op`/`end_op` 包裹的操作是一个事务 |
| fsck | 文件系统一致性检查器，日志使其大多数检查变得不必要 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| src/log.c | 修改（实验性） | 在 `commit()` 的各关键点插入 panic，实验后恢复 |
| tools/fscheck.c | 新增 | 宿主机端磁盘一致性检查工具 |
| Makefile | 修改（可选） | 添加 `fscheck` 构建目标 |

## 验证

### 崩溃恢复验证流程

```bash
# 1. 在 commit() 中注入崩溃点 B
# 2. 启动 xv6，执行：
$ echo "hello" > test.txt     # 触发文件系统操作 → panic
# QEMU 退出
# 3. 重启（不修改镜像）
make qemu-nox
# 观察启动日志：应看到 recover_from_log 成功重放
# 4. 验证：
$ cat test.txt                # 期望：hello（日志重放成功）
```

### 不一致验证流程（危险实验）

```bash
# 1. 绕过日志（dangerous_commit），在 install_trans 中间 panic
# 2. 重启 xv6
# 3. ls 或 cat → 可能看到：孤儿 inode、损坏的目录、不一致的 bitmap
# 4. 运行 fscheck 验证不一致
```

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| panic 注入后 QEMU 直接重启而非退出 | QEMU 配置了重启 | 在 Makefile QEMU 参数中添加 `-no-reboot` |
| recover_from_log 未被调用 | 崩溃时日志头未写入（崩溃点 A）| 这是预期行为，改用崩溃点 B |
| fscheck 读取磁盘格式错误 | 字节序或结构体对齐与宿主机不同 | 用 `__attribute__((packed))` |

## 关键代码路径

- 事务开始: `src/log.c:begin_op()` → 等待日志空间 → 增加 `log.outstanding`
- 记录修改: `src/log.c:log_write(b)` → 将 buf 加入日志块集合
- 提交事务: `src/log.c:end_op()` → outstanding-- → 若为 0 调用 `commit()`
- 提交流程: `commit()` → `write_log` → `write_head`（**提交点**）→ `install_trans` → 清空头
- 崩溃恢复: `src/log.c:recover_from_log()` → 读日志头 → `install_trans(1)` → 清空头

## 设计权衡

| 方面 | 无日志（直接写） | WAL 日志 |
|------|----------------|---------|
| 崩溃一致性 | 无保证（任意不一致） | 保证（写两次实现原子性） |
| 写放大 | 1x（只写数据） | 2x（先写日志，再写数据） |
| 恢复时间 | 需要 fsck（可能很慢） | O(日志大小)（固定快速） |
| 实现复杂度 | 简单 | 中等（begin_op/commit/recover） |
| 写吞吐量 | 高 | 较低（日志成为瓶颈） |

## 进阶挑战

- [ ] 实现**组提交（Group Commit）**：积累多个事务后一次性提交，减少写放大
- [ ] 实现 **Checksum 校验**：在日志头中记录每个日志块的 checksum，防止损坏的日志被重放
- [ ] 阅读并对比 **ext4 日志模式**：data=writeback、data=ordered、data=journal 的差异
- [ ] 实现 **日志性能统计**：统计每次 `commit()` 写入的块数，观察写放大系数
- [ ] 设计**崩溃测试框架**：在 QEMU 中随机注入掉电，验证文件系统自动恢复
