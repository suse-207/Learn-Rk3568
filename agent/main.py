"""CLI entry.

  python main.py --task vsomeip-review "分析 vsomeip 绑定与服务发现"
  python main.py --task vsomeip-review -f prompts/vsomeip_review.txt -o report.md
  python main.py --task fix-xxx --allow-write "修复 ... 并验证"   # each side effect requires approval
  python main.py --task vsomeip-review            # interactive; same task id resumes history
"""
import argparse
import json
import sqlite3
import sys

from langchain_core.messages import HumanMessage
from langgraph.checkpoint.sqlite import SqliteSaver
from langgraph.errors import GraphRecursionError
from langgraph.types import Command

from code_agent import config, telemetry
from code_agent.graph import build_graph
from code_agent.spec_workflow import format_design, format_plan, format_spec


def ask_approval(payload: dict) -> str:
    print("\n" + "=" * 70, file=sys.stderr)
    if payload.get("kind") == "spec":
        print("待批准的实现 Spec：\n" + format_spec(payload["spec"]), file=sys.stderr)
        answer = input("输入 a 批准、c 取消，或输入修改意见: ").strip()
        if answer.lower() in ("a", "approve", "y", "yes"):
            return "approve"
        if answer.lower() in ("c", "cancel", "q"):
            return "cancel"
        return "revise: " + answer

    if payload.get("kind") in ("plan", "design"):
        print(
            format_design(payload["design"])
            if payload.get("kind") == "design"
            else format_plan(payload["plan"]),
            file=sys.stderr,
        )
        answer = input("输入 a 批准方案、c 取消，或输入调整意见: ").strip()
        if answer.lower() in ("a", "approve", "y", "yes"):
            return "approve"
        if answer.lower() in ("c", "cancel", "q"):
            return "cancel"
        return "revise: " + answer

    if payload.get("kind") == "commit":
        print("Reviewer 已通过。待提交文件：%s\n" % ", ".join(payload["paths"]), file=sys.stderr)
        print(payload["diff"], file=sys.stderr)
        answer = input(
            "输入 a 提交（默认说明：%s），其他输入不提交: "
            % payload["message"]
        ).strip()
        return "approve" if answer.lower() in ("a", "approve", "y", "yes") else "reject"

    print("Agent 请求执行以下操作：", file=sys.stderr)
    for a in payload["actions"]:
        print("-" * 70 + "\n" + a, file=sys.stderr)
    print("=" * 70, file=sys.stderr)
    ans = input("批准? [y = 执行 / 其他输入 = 拒绝理由]: ").strip()
    return ans or "rejected"


def run(
    graph,
    task_id: str,
    text: str,
    verbose: bool,
    workflow_mode: str = "analysis",
) -> str:
    cfg = {
        "configurable": {"thread_id": task_id},
        "recursion_limit": config.RECURSION_LIMIT,
    }
    stats = telemetry.RunStats()
    cfg["callbacks"] = [stats]

    def finish(report: str, status: str) -> str:
        entry = telemetry.record_run(
            task_id, config.MODEL, workflow_mode, stats, status, len(report)
        )
        if verbose:
            print(
                "  [stats] %s"
                % json.dumps(
                    {k: entry[k] for k in ("duration_s", "steps", "llm_calls", "tool_calls",
                                           "prompt_tokens", "completion_tokens")},
                    ensure_ascii=False,
                ),
                file=sys.stderr,
            )
        return report

    inp = {
        "messages": [HumanMessage(text)],
        "task_id": task_id,
        "verify_attempts": 0,
        "workflow_mode": workflow_mode,
        "new_spec_request": workflow_mode == "implementation",
    }
    final = ""
    while True:
        interrupted = None
        try:
            for step in graph.stream(inp, cfg, stream_mode="updates"):
                stats.steps += 1
                for node, update in step.items():
                    if node == "__interrupt__":
                        interrupted = update[0].value
                        continue
                    if not update or "messages" not in update:
                        continue
                    msg = update["messages"][-1]
                    if node == "agent" and getattr(msg, "tool_calls", None):
                        stats.tool_calls += len(msg.tool_calls)
                        for tc in msg.tool_calls:
                            print("  -> %s(%s)" % (tc["name"], str(tc["args"])[:200]), file=sys.stderr)
                    elif node == "tools" and verbose:
                        print("  <- %s" % str(msg.content)[:300], file=sys.stderr)
                    elif node == "verify":
                        if isinstance(msg, HumanMessage):
                            print("  !! evidence check failed, sent back:\n%s" % msg.content, file=sys.stderr)
                        else:
                            final = msg.content
                    elif node == "agent":
                        final = msg.content
        except GraphRecursionError:
            return finish(
                (final or "(未生成完整报告)")
                + "\n\n> 已达到单次任务步数上限（%d）。以上是已完成的部分结果；"
                "可调大 AGENT_RECURSION_LIMIT，或把问题拆小后重试。" % config.RECURSION_LIMIT,
                "recursion_limit",
            )
        if interrupted is None:
            return finish(final or "(no answer)", "ok")
        inp = Command(resume=ask_approval(interrupted))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("question", nargs="?")
    ap.add_argument("-f", "--file", help="read question from file")
    ap.add_argument("-o", "--output", help="write final report to file")
    ap.add_argument("--task", default="default", help="task id = LangGraph thread id = L3 memory key")
    ap.add_argument(
        "--allow-write",
        action="store_true",
        help="enable Spec/design-first implementation; edits/checks/commit require approval",
    )
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    config.MEMORY_DIR.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(config.CHECKPOINT_DB), check_same_thread=False)
    graph = build_graph(SqliteSaver(conn), allow_write=a.allow_write)
    print("[repo] %s  [model] %s  [task] %s  [write] %s"
          % (config.REPO_ROOT, config.MODEL, a.task, a.allow_write), file=sys.stderr)

    if a.file or a.question:
        text = open(a.file, encoding="utf-8").read() if a.file else a.question
        report = run(
            graph,
            a.task,
            text,
            a.verbose,
            "implementation" if a.allow_write else "analysis",
        )
        print(report)
        if a.output:
            open(a.output, "w", encoding="utf-8").write(report)
        return
    while True:
        try:
            text = input("\n>>> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if text in ("exit", "quit"):
            break
        if text:
            print(
                run(
                    graph,
                    a.task,
                    text,
                    a.verbose,
                    "implementation" if a.allow_write else "analysis",
                )
            )


if __name__ == "__main__":
    main()
