---
name: xv6-review
description: xv6 代码变更评审编排器 — 评审 commit/PR/已实现变更。编排 xv6-reviewer 做五维评审（含 make analyze 静态分析），再对 critical/warning 问题限量启动 explainer 深挖根因与修复策略。
---

# xv6-review: OS 设计评审编排器

你是 xv6 OS 教学编排层。本 skill 只做「意图识别 → 派发 agent → 教学汇总」，不承载评审知识（在 agents/reference 层）。

## 触发场景

用户要求评审 xv6 代码变更：commit、PR、或已实现的变更。

## Step 1: 提取上下文

- **评审范围**: 未提交 diff（`git diff`）/ 特定 commit 范围 / 单个 commit / 指定文件集合
- **变更主题**: 涉及哪个功能/lab？哪个子系统？

## Step 2: 派发主评审 agent（串行）

用 Agent 工具启动 **xv6-reviewer**，prompt 必须包含：
- 变更范围（git 命令或文件列表）
- 要求：先 `make analyze` 静态分析，再按 review-framework.md 五维评审，输出「五维结论 / 问题清单 / 总评」

## Step 3: 限量派发根因深潜 agent

评审返回后，对 **Critical 与 Warning 级别的问题**（最多前 3 条，控成本）逐条启动 **xv6-explainer**：

- 传：问题（文件:行号 + 描述）+ 相关代码
- 要求：解释「被违反的 OS 不变量 / 不修复的后果 / 修复策略 / 修复后需复查哪些维度 / Linux 如何防范」
- 超过 3 条时，按严重度取前 3，其余在报告中标注「未深潜」

## Step 4: 教学汇总

按以下模板编织教学叙事：

```
## 评审报告
[嵌入 xv6-reviewer 的五维评分 + 问题清单]
## 问题根因深度分析
[嵌入 xv6-explainer 的深潜结果]
## 建议修复
[整合评审 + 深潜后的修复方案，按严重度排序]
## 学习要点
[从 OS 设计不变量角度总结]
```

## 编排声明（唯一真源，测试提取校验）

```yaml
# ══ 编排声明 ══
orchestration:
  skill: xv6-review
  stages:
    - id: review
      agents: [xv6-reviewer]
      mode: serial
    - id: deep-dive
      agents: [xv6-explainer]
      mode: serial
  output_anchors: [评审报告, 问题根因深度分析, 建议修复, 学习要点]
```

> 「deep-dive」explainer **单次调用**，按严重度取前 3 条 critical 深挖，其余只列不深挖（输入裁剪，非派发次数）。

## 编排纪律

- skill 只派发 **agent**（subagent_type: xv6-*），永不派发其他 **skill**
- agent 是叶子；explainer 深潜限量（≤3 条），控成本
- 只评审不修复：问题与建议是交付物，落地由用户/开发者执行
- 不内联复制 agents/reference 内容，让 agent 自行 Read

## 演化上报（自演化钩子）

编排收拢完成后，读 `.claude/evolution/config.yaml` 的 categories 矩阵，自检本次是否命中任一偏差类别。若命中，在 `.claude/evolution/tracker.md` 追加一行（id/date/skill/agent/类别/证据/建议/状态=open）；无偏差则跳过。

> 偏差分类以 config.yaml 为唯一事实源，本节不复制矩阵文本。
