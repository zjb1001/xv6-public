# Lab: Two-Stage Bootloader (多阶段引导加载)

[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

xv6 的 bootloader（bootasm.S + bootmain.c）必须在单个 512 字节扇区内完成所有工作：切换保护模式、读磁盘、解析 ELF、加载内核段。这个空间极其紧张——多加几行代码就可能溢出。

真实 PC 的 bootloader（GRUB、syslinux）几乎都是多阶段的：第一阶段极小（< 512 字节），只负责加载更大的第二阶段；第二阶段再完成复杂工作（文件系统驱动、ELF 解析、用户界面）。

本实验拆分 xv6 的引导为两阶段，让 Stage1 只做"加载 Stage2"这一件事，Stage2 再自由完成 ELF 加载。

## 前置知识

- **512 字节魔咒**: BIOS 只读第一个扇区（512 字节）到 `0x7C00`，最后 2 字节必须是 `0x55AA`
- **磁盘寻址**: xv6 用 LBA 方式通过 IDE 端口 `0x1F0-0x1F7` 读磁盘
- **ELF 格式**: 内核编译为 ELF 格式，bootmain.c 解析 ELF header 和 program header 来确定加载地址
- **内存布局**: bootasm.S 在 `0x7C00`，内核 ELF header 读到 `0x10000`，内核代码加载到 `0x100000`

### 当前 vs 目标架构

```
当前 (单阶段):
  BIOS → [bootasm + bootmain = 510 字节] → kernel

目标 (两阶段):
  BIOS → [stage1: bootasm + mini-bootmain = 510 字节]
                    ↓ 读取磁盘扇区 2~N
              [stage2: bootstage2.c = 无大小限制]
                    ↓ 解析 ELF, 加载内核段
              kernel
```

## 实验内容

### 1. 精简 bootmain.c 为 Stage1 (修改 boot/bootmain.c)

Stage1 的唯一职责：从磁盘加载 Stage2 到内存，然后跳转。

**需要做的事**：
- 保留 `waitdisk`、`readsect`、`readseg` 三个磁盘 I/O 函数不变
- 将 `bootmain()` 替换为极简版本：调用 `readseg` 加载 Stage2，然后跳转

**关键参数**：
- Stage2 加载地址：`0x10000`（或选择其他不冲突的地址）
- Stage2 占用扇区数：需要与 Makefile 中的 `dd seek` 参数协调
- 磁盘扇区偏移：Stage2 从扇区 1 开始（扇区 0 是 Stage1 自身）

### 2. 创建 Stage2 完整 ELF 加载器 (新增 boot/bootstage2.c)

Stage2 无大小限制，完成原始 bootmain.c 的全部工作：解析 ELF、加载段、跳转内核。

**需要做的事**：
- 复制原始 bootmain.c 中的磁盘 I/O 函数（waitdisk、readsect、readseg）
- 实现 `bootstage2()` 函数：读 ELF header → 遍历 program header → 加载每个 LOAD 段 → 跳转 entry

**注意**：Stage2 的 ELF 加载偏移与原始不同——内核在磁盘上的起始位置后移了 Stage2 占用的扇区数。需要定义 `KERNEL_DISK_OFFSET` 常量表示内核的起始扇区号。

**内存冲突**：Stage2 自身加载在 `0x10000`，而读 ELF header 也要放到某个地址。需要选择不冲突的地址（如 `0x90000`）作为 ELF header 的暂存区。

### 3. 修改构建流程 (修改 Makefile)

**需要添加的规则**：
- 编译 bootstage2.c 为独立二进制（链接地址与加载地址一致）
- 用 `dd` 组合磁盘镜像：Stage1（扇区 0）+ Stage2（扇区 1~N）+ 内核（扇区 N+1~）

**磁盘布局**：

```
扇区 0        : stage1 (bootblock, 512 bytes, 0x55AA signed)
扇区 1 ~ N    : stage2 (N * 512 bytes, N 通过编译时确定)
扇区 N+1 ~    : kernel (ELF format)
```

**Stage1 到 Stage2 的参数传递方式**（选做）：
- 编译时确定：Makefile 中用 `-D` 宏传入偏移
- 运行时传递：Stage1 跳转前将偏移写入约定内存地址（如 `0x7000`）

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 多阶段引导 | Stage1 加载 Stage2，Stage2 加载内核，类似 GRUB |
| 512 字节限制 | BIOS 硬约束，Stage1 必须 fit 进一个扇区 |
| 磁盘寻址 (LBA) | IDE 端口 0x1F0-0x1F7，扇区号直接寻址 |
| ELF 加载 | 解析 ELF header → 遍历 program header → 加载段 |
| 内存冲突 | Stage1 在 0x7C00，Stage2 在 0x10000，内核在 0x100000 |
| 构建系统 | dd 组合多个二进制为磁盘镜像，seek 控制偏移 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| boot/bootmain.c | 修改 | 精简为仅加载 Stage2 |
| boot/bootstage2.c | **新增** | Stage2 完整 ELF 加载器 |
| Makefile | 修改 | 添加 Stage2 构建规则，修改磁盘镜像布局 |

## 验证

### 编译和运行

```bash
make clean && make
ls -l build/bootblock   # 必须 ≤ 510 字节
ls -l build/stage2       # 无大小限制，但应 < 16KB
make qemu-nox
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| Stage1 大小合规 | bootblock ≤ 510 字节 | `ls -l build/bootblock` |
| Stage2 正确加载 | 从磁盘正确读取并执行 | GDB: `break *0x10000` |
| 内核正确加载 | 内核从正确的磁盘偏移读取 | `hexdump` 检查 xv6.img |
| 系统正常启动 | shell 可用 | 在 xv6 中输入命令 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 黑屏无输出 | Stage1 未正确加载 Stage2 | 检查磁盘偏移和 dd seek 参数 |
| 三重启循环 | Stage2 跳转地址错误 | 检查链接脚本 `-Ttext` 地址 |
| kernel 加载失败 | KERNEL_DISK_OFFSET 不正确 | 确保与 dd 的 seek 参数一致 |
| Stage2 入口崩溃 | Stage2 地址与 ELF header 冲突 | 使用不同地址暂存 ELF header |

## 关键代码路径

- Stage1 加载: boot/bootmain.c → readseg 加载 Stage2 → 跳转 Stage2 入口
- Stage2 ELF 解析: boot/bootstage2.c → 读 ELF header → 遍历 program header
- 段加载: boot/bootstage2.c → readseg + stosb 清零 BSS
- 控制转移: boot/bootstage2.c → 跳转 ELF entry point

## 设计权衡

| 方面 | 单阶段 (原始) | 两阶段 (本实验) |
|------|-------------|---------------|
| 代码空间 | 极紧张 (510 字节) | Stage2 无限制 |
| 复杂度 | 简单 | 增加 Stage2 协调 |
| 启动速度 | 快 (直接加载) | 略慢 (多读磁盘) |
| 可扩展性 | 差 | 好 (Stage2 可加菜单等) |

## 进阶挑战

- [ ] 在 Stage2 中添加启动菜单：选择加载哪个内核
- [ ] 实现 Stage1 到 Stage2 的参数传递（磁盘几何信息）
- [ ] 让 Stage2 支持从文件系统（而非裸扇区）加载内核
- [ ] 研究 GRUB Stage1.5 的设计：为什么需要 "1.5" 这个阶段
