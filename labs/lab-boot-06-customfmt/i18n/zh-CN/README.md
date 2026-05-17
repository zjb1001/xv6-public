# Lab: Custom Kernel Image Format (自定义内核镜像格式)

[English](../../README.md)

难度: ★★★★★

## 设计初衷

xv6 的 `bootmain.c` 用标准 ELF 格式加载内核。ELF 是 Unix 系统的可执行文件标准——灵活、通用，但对于一个教学内核来说过于复杂。ELF header 有 52 字节，每个 program header 有 32 字节，支持动态链接、节区表、符号表等 xv6 根本不用的功能。

本实验设计一个**极简的内核镜像格式**来替代 ELF，让学生从零理解"可执行文件格式"的本质：**它是 bootloader 和链接器之间的契约**。

做完这个实验，你会明白 ELF 的每个字段为什么存在（因为你需要自己设计替代品），链接脚本如何控制输出格式，以及"加载内核"其实只是"把文件中的数据搬到内存中的指定位置"。

## 前置知识

- **ELF 格式结构**: xv6 内核 ELF 包含 ELF header (52B) + 2 个 program header (各 32B) + 2 个 LOAD 段 (.text 和 .data/.bss)
- **链接脚本 (kernel.ld)**: 控制段的虚拟地址、加载地址和对齐
- **bootmain.c 的 ELF 加载逻辑**: 读 ELF header → 遍历 program header → readseg + stosb 清零 BSS
- **构建工具链**: `gcc` 编译 → `ld` 链接 → `objcopy` 转格式 → 自定义工具生成镜像

### 目标构建流程

```
kernel/*.c → gcc -c → *.o → ld -T kernel.ld → kernel.elf (ELF)
                                                      │
                                               mkkernel (自定义工具)
                                                      │
                                                kernel.bin (自定义格式)
                                                      │
                                               dd → xv6.img
```

## 实验内容

### 1. 分析当前内核 ELF 结构

用 `readelf -l build/kernel` 或 `objdump -l build/kernel` 查看内核 ELF 实际有哪些 LOAD 段。

典型结果只有 **2 个 LOAD 段**：.text（代码+只读数据）和 .data（数据+BSS）。xv6 不需要动态链接、不需要符号表加载——ELF 的大部分字段都是浪费。

### 2. 设计自定义格式 XKIF (xv6 Kernel Image Format)

**需要设计的结构**：
- `xkif_header` — 魔数、版本、入口地址、段数量、校验和等
- `xkif_segment` — 每段的物理地址、文件大小、内存大小、文件偏移

**设计决策点**：
- header 多大？需要哪些字段？ELF 的哪些字段是 xv6 真正需要的？
- 段描述符多大？与 ELF program header (32B) 相比能精简多少？
- 是否需要校验和？用什么算法（累加和、CRC32）？
- 数据对齐策略：是否按 512 字节扇区对齐？

### 3. 实现 mkkernel 转换工具 (新增 tools/mkkernel.c)

**功能**：读取 ELF 内核 → 提取 LOAD 段信息 → 生成 XKIF 格式文件。

**关键步骤**：
- 验证 ELF magic
- 遍历 program header，只提取 `type == LOAD` 的段
- 将 ELF 的 entry point 转换为物理地址（注意：xv6 中 `_start = V2P_WO(entry)`，所以 ELF entry 已经是物理地址）
- 重新计算段数据在 XKIF 文件中的偏移
- 写入 header + 段描述符 + 段数据

**entry_point 物理地址问题**：ELF entry 字段存的是 `_start`，被链接脚本设为 `V2P_WO(entry)` 即物理地址。在 XKIF 中要**显式**记录物理地址，消除这个隐含的"巧合"。

### 4. 修改 bootmain.c 解析 XKIF 格式 (修改 boot/bootmain.c)

**需要实现**：
- 定义 XKIF 格式结构（与 mkkernel.c 中一致）
- 替换 ELF 解析逻辑：读 XKIF header → 验证魔数 → 读段描述符 → 加载每个段
- 保留 waitdisk、readsect、readseg 不变

**好处**：bootmain.c 变得更小——XKIF 解析比 ELF 更直接，为其他功能腾出空间。

### 5. 修改构建流程 (修改 Makefile)

**需要调整的规则**：
- 链接阶段生成 `kernel.elf`（中间产物，不再直接使用）
- 添加 mkkernel 编译规则
- kernel 构建改为：`kernel.elf → mkkernel → kernel (XKIF)`
- 磁盘镜像 dd 规则不变

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 可执行文件格式 | 格式是 loader 和 linker 之间的契约 |
| ELF 格式 | 通过设计替代品理解每个字段为什么存在 |
| 链接脚本 | kernel.ld 控制段的虚拟地址、加载地址和对齐 |
| 构建工具链 | gcc → ld → 自定义工具 → dd 的完整管道 |
| 魔数 | 格式识别，与 ELF 的 `0x7F454C46` 作用相同 |
| 校验和 | 检测磁盘数据损坏 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| tools/mkkernel.c | **新增** | ELF → XKIF 转换工具 |
| boot/bootmain.c | 修改 | 从 ELF loader 改为 XKIF loader |
| Makefile | 修改 | 添加 mkkernel 构建规则 |
| kernel.ld | 可能修改 | 如需调整段布局 |

## 验证

### 编译

```bash
make clean && make
```

### 验证 mkkernel 输出

```bash
xxd -l 4 build/kernel     # 检查 XKIF 魔数
hexdump -C build/kernel | head -20   # 查看 header 结构
```

### 运行

```bash
make qemu-nox
```

应正常启动，与原始 ELF 版本行为完全相同。

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| XKIF 魔数正确 | 文件头为自定义魔数 | `xxd -l 4 build/kernel` |
| 段数量正确 | 通常 nsegments = 2 | hexdump 查看 header |
| 内核正常加载 | bootmain 解析后跳转成功 | `make qemu-nox` 正常启动 |
| BSS 清零正确 | memsz > filesz 的部分被清零 | 内核数据段无垃圾数据 |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| bootmain 解析失败 | 段偏移计算不正确 | 检查 mkkernel 的偏移计算 |
| 内核跳转后崩溃 | entry_point 是虚拟地址非物理地址 | 确认 ELF entry 的物理地址语义 |
| 段数据错位 | 文件对齐与读取偏移不匹配 | 确保两端用同样对齐规则 |
| 某些段没加载 | 统计了非 LOAD 段 | 只统计 `type == LOAD` |

## 关键代码路径

- ELF 分析: tools/mkkernel.c → 读取 ELF header，提取 LOAD 段
- 格式转换: tools/mkkernel.c → 构建 XKIF header + 段描述符，重写偏移
- 魔数校验: boot/bootmain.c → 比较自定义魔数
- 段加载: boot/bootmain.c → 遍历段描述符，readseg + stosb
- 构建管道: Makefile → kernel.elf → mkkernel → kernel (XKIF) → dd → xv6.img

## 设计权衡

| 方面 | ELF (原始) | XKIF (本实验) |
|------|-----------|-------------|
| 通用性 | 行业标准 | xv6 专用 |
| 复杂度 | header 52B + phdr 32B/段 | 自行设计 |
| 调试工具 | readelf, objdump 完整支持 | 需要自己写 |
| 理解深度 | "用" ELF | "设计" 格式 → 深层理解 |

## 进阶挑战

- [ ] 添加压缩支持：在 bootmain 中解压（结合多阶段引导实验获得更多空间）
- [ ] 实现多内核支持：header 中包含内核名称，bootmain 显示启动菜单
- [ ] 支持 ELF 和 XKIF 双格式：bootmain 检测魔数自动选择
- [ ] 研究 Linux 的 bzImage 格式：内核镜像中嵌入解压代码和 setup 数据
- [ ] 研究 UEFI 的 PE/COFF 格式：为什么 UEFI 要求 PE 格式而非 ELF

## 扩展阅读

- [ELF 格式规范](https://refspecs.linuxbase.org/elf/elf.pdf)
- [OSDev Wiki: Executable Formats](https://wiki.osdev.org/Executable_Formats)
