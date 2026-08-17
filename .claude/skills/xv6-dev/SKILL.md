---
name: xv6-dev
description: xv6 功能开发编排器 — 在 xv6 中添加系统调用、调度策略、内存管理等功能。编排 xv6-developer 实现，再并行启动 explainer/simulator/reviewer 三视角，汇总完整的 OS 学习体验。
---

# xv6-dev: 功能开发编排器

你是 xv6 OS 教学编排层。本 skill 只做「意图识别 → 派发 agent → 教学汇总」，不承载实现知识（在 agents/reference 层）。

## 触发场景

用户要求实现 xv6 功能：添加系统调用、调度策略、内存管理、内核数据结构、设备支持等。

## Step 1: 提取上下文

- **功能目标**: 要实现什么？输入输出是什么？
- **子系统**: 进程/内存/文件系统/陷阱/系统调用/同步/设备？
- **关联 lab**: 是否对应某个 `labs/` 或 `lab-Tests/`（遵守 file-map.md 目录规范）？
- **x86 前提**: 这是 x86 版 xv6（非 RISC-V）

## Step 2: 派发主实现 agent（串行）

用 Agent 工具启动 **xv6-developer**，prompt 必须包含：
- 功能描述 + 子系统 + 关联 lab
- 要求：按 file-map.md 修改清单落地，输出「实现总结 / 代码变更 / 设计决策记录」

## Step 3: 并行派发三视角 agent

主实现完成后，**单条消息并行**启动三个视角 agent（用各自的输出契约）：

1. **xv6-explainer**（静态理解）— 传：变更文件列表 + 关键代码，要求按「设计决策/概念映射/延伸」解释
2. **xv6-simulator**（动态追踪）— 传：关键代码/新链路，要求模拟完整执行路径（syscall 追踪/调度甘特图/页表/FS 栈）
3. **xv6-reviewer**（评审验证）— 传：变更范围 + diff，要求五维评分 + critical/warning 清单

## Step 4: 教学汇总

按以下模板编织教学叙事：

```
## 实现总结
[一段话：实现了什么，改了什么]
## 代码变更
[文件 + 关键改动，来自 xv6-developer]
## 设计决策解析
[嵌入 xv6-explainer 结果]
## 执行路径追踪
[嵌入 xv6-simulator 结果]
## 评审结果
[嵌入 xv6-reviewer 结果 + 待修复项]
## 学习要点
[从 OS 教材角度总结 2-3 个关键学习点]
```

## 编排声明（唯一真源，测试提取校验）

```yaml
# ══ 编排声明 ══
orchestration:
  skill: xv6-dev
  stages:
    - id: implement
      agents: [xv6-developer]
      mode: serial
    - id: tri-view
      agents: [xv6-explainer, xv6-simulator, xv6-reviewer]
      mode: parallel
  output_anchors: [实现总结, 代码变更, 设计决策解析, 执行路径追踪, 评审结果, 学习要点]
```

## 编排纪律

- skill 只派发 **agent**（subagent_type: xv6-*），永不派发其他 **skill**
- agent 是叶子，不会回头派发任何东西；同一 agent 一次编排最多调用一次
- 三视角可并行；主实现必须先于视角完成（依赖它的产出）
- 不内联复制 agents/reference 内容，需要时让 agent 自行 Read

## 演化上报（自演化钩子）

编排收拢完成后，读 `.claude/evolution/config.yaml` 的 categories 矩阵，自检本次是否命中任一偏差类别。若命中，在 `.claude/evolution/tracker.md` 追加一行（id/date/skill/agent/类别/证据/建议/状态=open）；无偏差则跳过。

> 偏差分类以 config.yaml 为唯一事实源，本节不复制矩阵文本。
