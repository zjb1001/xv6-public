#!/usr/bin/env python3
"""xv6 skill 工程静态测试套件（零第三方依赖，零 LLM 调用）。

检查项：
  1. 结构不变式（防环）：agent tools 词元不含 Agent/Skill；编排块 agents 合法；prose 派发词元不越界
  2. 引用完整性 + 锚点一致性：agent 必读 reference 存在；被引 agent 存在；输出契约节锚点 >= 2
  3. 单一事实源：CLAUDE.md 不含已下沉的 OS 知识块标题
  4. settings 护栏：本套件不写 settings.json
  5. 演化健康（软信号，不 gate）：tracker 缺失跳过

退出码：任一 hard 检查失败 → 1；全绿 → 0。
用法：python3 .claude/tests/run.py  （或 make skill-check）
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent  # .claude/tests/ -> repo root
AGENTS_DIR = ROOT / ".claude" / "agents"
SKILLS_DIR = ROOT / ".claude" / "skills"
REF_DIR = ROOT / ".claude" / "reference"
EVO_DIR = ROOT / ".claude" / "evolution"
CLAUDE_MD = ROOT / "CLAUDE.md"

failures = []
warnings = []


def fail(msg):
    failures.append(msg)


def warn(msg):
    warnings.append(msg)


def read(p):
    return p.read_text(encoding="utf-8")


# ────────────────────────────────────────────────────────────
# 数据准备
# ────────────────────────────────────────────────────────────
agents = {p.stem for p in AGENTS_DIR.glob("*.md")}
skills = {p.parent.name for p in SKILLS_DIR.glob("*/SKILL.md")}
skill_files = {p.parent.name: p for p in SKILLS_DIR.glob("*/SKILL.md")}

# 解析每个 agent 的 frontmatter tools 词元
agent_tools = {}
for p in AGENTS_DIR.glob("*.md"):
    text = read(p)
    m = re.search(r"^tools:\s*(.+)$", text, re.MULTILINE)
    agent_tools[p.stem] = [t.strip() for t in m.group(1).split(",")] if m else []

# 解析每个 skill 的编排块（fenced yaml 块内）
skill_orch = {}  # skill -> {agents:set, output_anchors:list}
for name, p in skill_files.items():
    text = read(p)
    m = re.search(r"```yaml\n(.*?)```", text, re.DOTALL)
    if not m:
        fail(f"[编排] {name} 缺 fenced yaml 编排块")
        continue
    block = m.group(1)
    agents_in = set(re.findall(r"agents:\s*\[([^\]]*)\]", block))
    # 展平
    flat = set()
    for a in agents_in:
        flat |= {x.strip() for x in a.split(",") if x.strip()}
    anchors = re.findall(r"output_anchors:\s*\[([^\]]*)\]", block)
    anchors_flat = [x.strip() for a in anchors for x in a.split(",") if x.strip()]
    skill_orch[name] = {"agents": flat, "anchors": anchors_flat, "text": text, "block": block}


# ────────────────────────────────────────────────────────────
# 1. 结构不变式（防环）
# ────────────────────────────────────────────────────────────
def check_no_cycle():
    # 1a. agent tools 词元不含 Agent/Skill
    for name, tools in agent_tools.items():
        if "Agent" in tools or "Skill" in tools:
            fail(f"[防环] agent {name} 工具面含 Agent/Skill: {tools}")
    # 1b. 编排块 agents ⊆ agents/ 且无 skill 名、per-skill 唯一
    for name, orch in skill_orch.items():
        if not orch["agents"]:
            fail(f"[防环] {name} 编排块无 agents")
            continue
        for a in orch["agents"]:
            if a not in agents:
                fail(f"[防环] {name} 编排块引用不存在的 agent: {a}")
            if a in skills:
                fail(f"[防环] {name} 编排块引用 skill 名（应只派 agent）: {a}")
        # per-skill 唯一（同流不重入）：agent 在一个 skill 的编排块内至多出现一次
        seen = set()
        # 重新逐 stage 检查重复（简化：整体集合唯一即不重入，因为每个 stage agents 已去重）
        # 精确：按 stage 提取 agents 列表判断重复
        stage_agents = re.findall(r"agents:\s*\[([^\]]*)\]", orch["block"])
        all_flat = [x.strip() for s in stage_agents for x in s.split(",") if x.strip()]
        if len(all_flat) != len(set(all_flat)):
            fail(f"[防环] {name} 同一 agent 跨 stage 重复出现（违反律3同流不重入）: {all_flat}")


def check_reverse_orchestration():
    """反向校验：prose 中派发动作词元 ⊆ {skill 自身名} ∪ {编排块 agents}。"""
    for name, orch in skill_orch.items():
        text = orch["text"]
        # 去掉 fenced yaml 块，得到 prose
        prose = re.sub(r"```yaml\n.*?```", "", text, flags=re.DOTALL)
        # 提取 xv6-xxx 词元
        for token in re.findall(r"xv6-[a-z]+", prose):
            # 排除 skill 自身名、路由提示（前面有 /）、通配
            before = prose[max(0, prose.rfind(token) - 6):prose.rfind(token)]
            if token == name:
                continue
            if "/" in before or "subagent_type:" in before:
                continue
            if token not in orch["agents"]:
                fail(f"[反向] {name} prose 出现块外 agent/skill 词元: {token}")


# ────────────────────────────────────────────────────────────
# 2. 引用完整性 + 锚点一致性
# ────────────────────────────────────────────────────────────
def check_references():
    # 2a. agent 必读 reference 存在
    for p in AGENTS_DIR.glob("*.md"):
        text = read(p)
        for ref in re.findall(r"\.claude/reference/([a-z-]+)\.md", text):
            if not (REF_DIR / f"{ref}.md").exists():
                fail(f"[引用] {p.stem} 引用了不存在的 reference: {ref}.md")
    # 2b. 每个被引 agent 有输出契约节且锚点 >= 2
    for name, orch in skill_orch.items():
        for a in orch["agents"]:
            ap = AGENTS_DIR / f"{a}.md"
            if not ap.exists():
                continue  # 已被 1b 捕获
            atext = read(ap)
            m = re.search(r"^#+\s*输出契约\s*$(.*?)(?=^#+\s|\Z)", atext, re.MULTILINE | re.DOTALL)
            if not m:
                fail(f"[锚点] {a} 缺「输出契约」节")
                continue
            body = m.group(1)
            # 锚点 = 编号项或加粗项
            anchor_count = len(re.findall(r"^\s*\d+\.\s*\*\*", body, re.MULTILINE)) + \
                           len(re.findall(r"^\s*\*\*", body, re.MULTILINE))
            if anchor_count < 2:
                fail(f"[锚点] {a} 输出契约锚点 < 2（当前 {anchor_count}）")
    # 2c. 上报节不含 config.yaml 的类别名（防矩阵漂移）
    cfg = EVO_DIR / "config.yaml"
    if cfg.exists():
        cfg_text = read(cfg)
        cat_names = re.findall(r"name:\s*([^,}]+)", cfg_text)
        for name, orch in skill_orch.items():
            report_sec = re.search(r"## 演化上报.*", orch["text"], re.DOTALL)
            if not report_sec:
                warn(f"[上报] {name} 缺「演化上报」节")
                continue
            for cn in cat_names:
                cn = cn.strip()
                if cn and cn in report_sec.group(0):
                    fail(f"[漂移] {name} 上报节复制了矩阵类别名: {cn}（应以 config.yaml 为唯一真源）")


# ────────────────────────────────────────────────────────────
# 3. 单一事实源：CLAUDE.md 不含已下沉知识块标题
# ────────────────────────────────────────────────────────────
SUNK_HEADINGS = ["架构概览", "关键常量", "内存布局", "执行流程", "OS 核心术语对照"]


def check_single_source():
    claude = read(CLAUDE_MD)
    for h in SUNK_HEADINGS:
        if re.search(rf"^##\s+.*{h}", claude, re.MULTILINE):
            fail(f"[漂移] CLAUDE.md 仍含已下沉的块标题: {h}")


# ────────────────────────────────────────────────────────────
# 4. settings 护栏
# ────────────────────────────────────────────────────────────
# 本套件只读：仅 read_text，不写任何仓库文件（含 settings.json）。
# 「不触碰 settings」由「测试套件无写路径」这一事实保证，而非运行时断言。
# ────────────────────────────────────────────────────────────
# 5. 演化健康（软信号，不 gate）
# ────────────────────────────────────────────────────────────
def check_evolution_health():
    tracker = EVO_DIR / "tracker.md"
    if not tracker.exists():
        warn("[演化] tracker.md 缺失（阶段3 前正常），跳过演化健康检查")
        return
    t = read(tracker)
    open_count = t.count("| open |") + t.count("| pending |")
    warn(f"[演化] tracker open/pending 条目 ≈ {open_count}")


def main():
    check_no_cycle()
    check_reverse_orchestration()
    check_references()
    check_single_source()
    check_evolution_health()

    # 汇总
    print("=" * 60)
    print(f"xv6 skill 工程健康检查")
    print(f"  skills:  {len(skills)}")
    print(f"  agents:  {len(agents)}")
    print(f"  reference: {len(list(REF_DIR.glob('*.md')))}")
    print("=" * 60)

    if warnings:
        for w in warnings:
            print(f"  ⚠ {w}")
    if failures:
        print(f"\n❌ 失败 {len(failures)} 项：")
        for f in failures:
            print(f"  ✗ {f}")
        sys.exit(1)
    else:
        print("\n✅ 全部 hard 检查通过")
        sys.exit(0)


if __name__ == "__main__":
    main()
