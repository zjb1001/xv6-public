# Lab: Multiboot-Compatible Kernel (GRUB Multiboot 启动)

[English](../../README.md)

难度: ★★★★☆

## 设计初衷

xv6 的启动依赖自己写的 bootloader（bootasm.S + bootmain.c）。但真实世界中，几乎没人自己写 bootloader——大家用 GRUB。

GRUB 是 Linux 发行版的标准引导加载器，已经处理了所有复杂的事情：识别文件系统、解析配置文件、显示启动菜单、加载内核。内核只需要遵守 **Multiboot 规范**——一个 bootloader 和内核之间的"合同"。

有趣的是，xv6 的 `entry.S` 中**已经有一个 Multiboot 头**，但 `flags = 0` 不请求任何信息，Makefile 也没有构建 Multiboot 兼容镜像。本实验让这个 header "活"起来，让 xv6 能被 GRUB 直接加载。

核心问题：**内核和 bootloader 之间的"接口"是什么？**

## 前置知识

- **Multiboot 规范**: 内核在 ELF 文件的前 8KB 内嵌入魔数 `0x1BADB002` 的 header，GRUB 识别后直接加载内核并跳转
- **GRUB 的工作**: GRUB 把内核 ELF 从磁盘读入内存，切换到 32 位保护模式，跳到 entry point。此时 EAX = `0x2BADB002`（魔数），EBX = multiboot_info 结构的物理地址，A20 已开启，分页未开启，中断禁用
- **multiboot_info 结构**: GRUB 传递给内核的信息包，包含内存映射、命令行参数、启动设备等

### 当前启动 vs Multiboot 启动

```
当前 (自举):
  BIOS → bootasm.S → bootmain.c → entry.S → main()

Multiboot (GRUB 加载):
  BIOS → GRUB → entry.S（GRUB 已完成保护模式切换和 ELF 加载）
  （bootasm.S 和 bootmain.c 被完全跳过！）
```

## 实验内容

### 1. 激活 Multiboot Header (修改 kernel/entry.S)

当前 entry.S 中的 Multiboot header `flags = 0`，不请求任何信息。

**需要做的事**：将 flags 改为 `0x01 | 0x04`（请求内存信息和引导设备信息），并更新校验和（`-(magic + flags)`）使三个 .long 之和为 0。

### 2. 定义 Multiboot 信息结构 (新增 include/multiboot.h)

**需要定义的结构**：
- `struct multiboot_info` — 包含 flags、mem_lower/mem_upper、mmap_addr/mmap_length 等字段
- `struct multiboot_mmap_entry` — 内存映射条目（size、addr、len、type）
- `MULTIBOOT_MAGIC` 常量（`0x2BADB002`）
- 各 flag 位掩码：`MBI_MEM_INFO`、`MBI_MMAP`、`MBI_LOADER` 等

**参考**：[Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)

### 3. 修改 entry.S 接收 Multiboot 信息 (修改 kernel/entry.S)

**关键设计**：entry.S 必须兼容两种启动方式。

- 比较 EAX 与 `0x2BADB002`：匹配则保存 EBX（multiboot_info 物理地址）到约定变量
- 无论哪种方式，都继续正常的分页设置和跳转 main()
- 用 `.comm` 声明 `multiboot_info_ptr` 变量

**注意**：必须在开分页前保存 EBX（其中的物理地址在分页后需要 P2V 转换）。

### 4. 解析 Multiboot 信息 (修改 kernel/main.c)

**需要实现** `parse_multiboot()`：
- 检查 `multiboot_info_ptr` 是否非零（区分 Multiboot 和自举启动）
- P2V 转换后读取 flags 判断哪些信息可用
- 打印 loader 名称、内存大小、内存映射（如果可用）

在 `main()` 中于 `kvmalloc()` 之后调用（需要虚拟地址映射）。

### 5. 创建 GRUB 启动镜像 (修改 Makefile)

**需要添加的构建目标**：
- `grub-qemu` — 用 GRUB ISO 启动 xv6
- `xv6.iso` — 用 `grub-mkrescue` 创建含 kernel + grub.cfg 的 ISO
- `grub.cfg` — 配置 GRUB 用 `multiboot` 命令加载 xv6 内核

**依赖**：需要系统安装 `grub-common` 和 `xorriso`。

### 6. 兼容两种启动方式

核心设计：`multiboot_info_ptr` 为 0 表示自举启动（原始行为），非零表示 GRUB 启动。entry.S 和 main.c 都通过这个变量分支。

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| Multiboot 规范 | bootloader-内核的标准接口协议 |
| ELF 加载 | GRUB 解析 ELF 并加载各段到指定物理地址 |
| 双重启动路径 | 内核检测启动来源，兼容多种 bootloader |
| 引导信息传递 | GRUB 通过寄存器 (EAX/EBX) 和内存结构传递信息 |
| 内存映射 | GRUB 提供 E820 等价的内存映射 |
| ISO 镜像 | grub-mkrescue 创建 El Torito 可启动 ISO |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| kernel/entry.S | 修改 | 激活 Multiboot header，保存 EBX |
| include/multiboot.h | **新增** | Multiboot 信息结构定义 |
| kernel/main.c | 修改 | 添加 `parse_multiboot()` |
| Makefile | 修改 | 添加 GRUB ISO 构建目标 |

## 验证

### 方式 1: 回归测试（原有 bootloader）

```bash
make clean && make qemu-nox
```

应与原始行为完全相同。`multiboot_info_ptr` 应为 0。

### 方式 2: GRUB ISO 启动

```bash
make grub-qemu
```

### 方式 3: QEMU -kernel 直接加载

```bash
qemu-system-i386 -kernel build/kernel -drive file=build/fs.img,index=1,media=disk,format=raw -m 128 -nographic
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 双路径兼容 | 原有 boot 和 GRUB 都能启动 | 分别运行两种方式 |
| Multiboot 检测 | GRUB 启动时 EAX=0x2BADB002 | parse_multiboot 打印 loader 信息 |
| 内存映射获取 | GRUB 提供 E820 等价数据 | 打印 memory map 条目 |
| 信息正确性 | mem_upper 约等于 QEMU 内存大小 | 128MB → mem_upper ≈ 130048KB |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| GRUB 报 "not multiboot compliant" | header 不在 ELF 前 8KB | 检查 entry.S 中 header 位置 |
| 启动后三重故障 | entry.S 地址引用在 GRUB 加载时不对 | 检查 CR3 设置 |
| multiboot_info_ptr 是零 | EBX 保存位置被覆盖 | 确保在开分页前保存 |
| GRUB ISO 创建失败 | 缺少 grub-mkrescue 或 xorriso | `sudo apt install grub-common xorriso` |
| 原有 boot 启动失败 | entry.S 改动破坏了 bootmain 路径 | 确保非 Multiboot 跳转路径正确 |

## 关键代码路径

- GRUB 检测: kernel/entry.S → 比较 EAX 与 0x2BADB002
- 信息保存: kernel/entry.S → EBX 存入 multiboot_info_ptr
- 信息解析: kernel/main.c → P2V 转换后读取 flags/内存映射
- 双路径入口: kernel/entry.S → 两条路径汇合于分页设置

## 设计权衡

| 方面 | 自举 bootloader (原始) | GRUB Multiboot (本实验) |
|------|---------------------|----------------------|
| 复杂度 | 需要自己写 512 字节引导 | 只需加一个 header |
| 文件系统 | 不支持，直接读扇区 | GRUB 支持 ext2/fat/iso9660 |
| 启动菜单 | 没有 | GRUB 提供完整菜单 |
| 内存信息 | 需要自己调 E820 | GRUB 自动提供 |
| 依赖 | 无外部依赖 | 需要 GRUB 安装 |

## 进阶挑战

- [ ] 通过 GRUB 命令行传递参数给内核（如 `xv6 maxproc=128`），在 main.c 中解析
- [ ] 利用 GRUB module 机制将 fs.img 作为模块传给内核
- [ ] 实现 Multiboot 2 规范（支持 EFI）
- [ ] 研究 Linux 的 `startup_32`：Linux 如何检测 Multiboot 并解析信息

## 扩展阅读

- [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)
- [OSDev Wiki: Multiboot](https://wiki.osdev.org/Multiboot)
