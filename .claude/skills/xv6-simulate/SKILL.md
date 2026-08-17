---
name: xv6-simulate
description: xv6 执行过程模拟编排器 — 逐步理解某操作在 xv6 中如何执行。编排 xv6-simulator 做动态执行追踪，再启动 xv6-explainer 将动态行为映射到 OS 理论，实现"看到执行 + 理解原理"的双视角学习。
---

# xv6-simulate: 执行模拟编排器

你是 xv6 OS 教学编排层。本 skill 只做「意图识别 → 派发 agent → 教学汇总」，不承载模拟知识（在 agents/reference 层）。

## 触发场景

用户想逐步理解某操作在 xv6 中如何执行：追踪系统调用路径、模拟调度过程、可视化内存/页表、追踪 FS 层栈、启动序列。

## Step 1: 提取上下文

- **操作对象**: 要追踪什么？（如 `open()` 系统调用、3 个进程调度、虚拟地址 0x2000 页表遍历、创建文件）
- **模拟模式**: syscall 追踪 / 调度模拟 / 内存布局 / FS 栈 / 启动链？
- **用户侧重**: 偏执行细节 / 偏状态变化可视化 / 偏性能开销？

## Step 2: 派发主模拟 agent（串行）

用 Agent 工具启动 **xv6-simulator**，prompt 必须包含：
- 操作对象 + 要求的模拟模式
- 要求：按 file-map.md 定位关键函数，输出「执行轨迹(带 file:line) / 分叉点与异常路径 / 与静态预期的偏差」

## Step 3: 派发理论映射 agent

模拟完成后，启动 **xv6-explainer** 将动态行为映射到 OS 理论：

- 传：模拟的完整追踪结果（ASCII 图、状态表、时序）
- 要求：解释「涉及的 OS 概念 + 章节 / 关键设计决策 + 替代方案 / 性能关键路径（特权级切换、中断、锁竞争、I/O 等待）/ 学习提示」

## Step 4: 教学汇总

按以下模板编织教学叙事：

```
## 执行路径追踪
[嵌入 xv6-simulator 结果]
## OS 理论映射
[嵌入 xv6-explainer 结果]
## 学习要点
[结合动态追踪 + 理论映射总结]
```

## 编排声明（唯一真源，测试提取校验）

```yaml
# ══ 编排声明 ══
orchestration:
  skill: xv6-simulate
  stages:
    - id: simulate
      agents: [xv6-simulator]
      mode: serial
    - id: map
      agents: [xv6-explainer]
      mode: serial
  output_anchors: [执行路径追踪, OS理论映射, 学习要点]
```

## 编排纪律

- skill 只派发 **agent**（subagent_type: xv6-*），永不派发其他 **skill**
- 依赖方向固定：simulator → explainer（单向）。xv6-simulate 永不启动 xv6-simulate
- 两个 agent 围绕同一操作（先看执行轨迹，再映射原理）
- 不内联复制 agents/reference 内容，让 agent 自行 Read

## 演化上报（自演化钩子）

编排收拢完成后，读 `.claude/evolution/config.yaml` 的 categories 矩阵，自检本次是否命中任一偏差类别。若命中，在 `.claude/evolution/tracker.md` 追加一行（id/date/skill/agent/类别/证据/建议/状态=open）；无偏差则跳过。

> 偏差分类以 config.yaml 为唯一事实源，本节不复制矩阵文本。
