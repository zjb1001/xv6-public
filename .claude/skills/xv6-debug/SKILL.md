---
name: xv6-debug
description: xv6 内核问题诊断编排器 — 启动失败、内核 panic、死锁、竞态条件、内存损坏。编排 xv6-debugger 定位根因，再并行启动 explainer（OS 原理）+ reviewer（修复验证），汇总根因+原理+方案验证。
---

# xv6-debug: 内核问题诊断编排器

你是 xv6 OS 教学编排层。本 skill 只做「意图识别 → 派发 agent → 教学汇总」，不承载诊断知识（在 agents/reference 层）。

## 触发场景

用户报告 xv6 内核问题：启动失败、panic、挂起/死锁、竞态、内存损坏、输出异常。

## Step 1: 提取上下文

- **症状类型**: 崩溃 / 挂起 / 输出错误 / 行为异常
- **错误信息**: panic 内容、QEMU 输出、GDB 信息
- **复现条件**: 哪些操作触发？是否只在多 CPU 时出现？
- **最近的代码修改**: 与哪个 lab/功能相关？

## Step 2: 派发诊断 agent（串行）

用 Agent 工具启动 **xv6-debugger**，prompt 必须包含：
- 症状 + 错误信息 + 复现条件 + 相关代码修改
- 要求：按 panic-table.md 定位，输出「复现步骤 / 根因假设(置信度) / 验证证据 / 修复草案」

## Step 3: 并行派发双视角 agent

诊断完成后，**单条消息并行**启动两个视角 agent：

1. **xv6-explainer**（根因 OS 原理）— 传：根因 + 有问题的代码 + 修复草案，要求解释「被违反的 OS 不变量、不修复的后果、Linux 如何防范、修复为什么有效」
2. **xv6-reviewer**（修复验证）— 传：原始问题 + 修复草案，要求验证「是否解决根因、是否引入新问题、错误路径是否正确、是否需同步改其他文件」

## Step 4: 教学汇总

按以下模板编织教学叙事：

```
## 诊断结果
[症状 + 根因 + 文件定位，来自 xv6-debugger]
## 修复方案
[修复草案 + 为什么这样修复]
## 根因深度分析
[嵌入 xv6-explainer 的 OS 不变量分析]
## 修复验证
[嵌入 xv6-reviewer 的验证结果 + 遗留问题]
## 学习要点
[从 OS 不变量角度总结]
```

## 编排声明（唯一真源，测试提取校验）

```yaml
# ══ 编排声明 ══
orchestration:
  skill: xv6-debug
  stages:
    - id: diagnose
      agents: [xv6-debugger]
      mode: serial
    - id: dual-view
      agents: [xv6-explainer, xv6-reviewer]
      mode: parallel
  output_anchors: [诊断结果, 修复方案, 根因深度分析, 修复验证, 学习要点]
```

## 编排纪律

- skill 只派发 **agent**（subagent_type: xv6-*），永不派发其他 **skill**
- agent 是叶子；同一 agent 一次编排最多调用一次
- 修复落地与否由用户决定：diagnose 出的修复草案先呈现，不要未经确认直接改源码
- 不内联复制 agents/reference 内容，让 agent 自行 Read

## 演化上报（自演化钩子）

编排收拢完成后，读 `.claude/evolution/config.yaml` 的 categories 矩阵，自检本次是否命中任一偏差类别。若命中，在 `.claude/evolution/tracker.md` 追加一行（id/date/skill/agent/类别/证据/建议/状态=open）；无偏差则跳过。

> 偏差分类以 config.yaml 为唯一事实源，本节不复制矩阵文本。
