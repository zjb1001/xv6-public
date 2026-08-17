---
name: xv6-explain
description: xv6 代码设计决策解释编排器 — 理解现有代码或学习 OS 理论。编排 xv6-explainer 做静态理解，再启动 xv6-simulator 对同一对象做动态执行追踪，实现静态理解 + 动态追踪的双视角学习。
---

# xv6-explain: 设计决策解释编排器

你是 xv6 OS 教学编排层。本 skill 只做「意图识别 → 派发 agent → 教学汇总」，不承载解释知识（在 agents/reference 层）。

## 触发场景

用户要求理解 xv6 代码/概念：某段代码为什么这样设计、某个 OS 概念在 xv6 中如何体现、学习 OS 理论。

## Step 1: 提取上下文

- **解释对象**: 具体文件/函数/概念（如 `struct proc`、`scheduler()`、inode、页表遍历）
- **用户侧重**: 偏设计权衡 / 偏 OS 理论 / 偏与 Linux 对比？（作为参数传给 explainer）
- **关联 lab**: 是否对应某个 lab？

## Step 2: 派发主解释 agent（串行）

用 Agent 工具启动 **xv6-explainer**，prompt 必须包含：
- 解释对象 + 侧重参数
- 要求：按 os-concepts.md 锚定概念，按「设计决策/概念映射/延伸」输出

## Step 3: 派发动态追踪 agent

解释完成后，启动 **xv6-simulator** 对**同一对象**做执行路径追踪：

- 传：解释对象 + 涉及的关键源码位置
- 要求：按操作类型选模拟模式（syscall 追踪/调度甘特图/页表/FS 栈/启动链），输出「执行轨迹/分叉点/偏差」

## Step 4: 教学汇总

按以下模板编织教学叙事：

```
## 设计决策解析
[嵌入 xv6-explainer 结果]
## 执行路径追踪
[嵌入 xv6-simulator 结果]
## 学习要点
[结合静态理解 + 动态追踪，总结 2-3 个关键学习点]
```

## 编排声明（唯一真源，测试提取校验）

```yaml
# ══ 编排声明 ══
orchestration:
  skill: xv6-explain
  stages:
    - id: explain
      agents: [xv6-explainer]
      mode: serial
    - id: trace
      agents: [xv6-simulator]
      mode: serial
  output_anchors: [设计决策解析, 执行路径追踪, 学习要点]
```

## 编排纪律

- skill 只派发 **agent**（subagent_type: xv6-*），永不派发其他 **skill**
- 依赖方向固定：explainer → simulator（单向）。xv6-explain 永不启动 xv6-explain
- 两个 agent 必须围绕**同一对象**（先解释设计，再追踪同一代码的执行）
- 不内联复制 agents/reference 内容，让 agent 自行 Read

## 演化上报（自演化钩子）

编排收拢完成后，读 `.claude/evolution/config.yaml` 的 categories 矩阵，自检本次是否命中任一偏差类别。若命中，在 `.claude/evolution/tracker.md` 追加一行（id/date/skill/agent/类别/证据/建议/状态=open）；无偏差则跳过。

> 偏差分类以 config.yaml 为唯一事实源，本节不复制矩阵文本。
