"""LangGraph workflows

Analysis: START -> agent <-> read tools -> citation verify -> END
Implementation: grill -> Spec approval -> read-only discovery -> design approval
                         -> per-write approval -> static/build/test -> independent review
                         -> final commit approval -> END
"""
import fnmatch
from pathlib import PurePosixPath
from typing import Annotated, List, TypedDict

from langchain_core.messages import AIMessage, AnyMessage, HumanMessage, SystemMessage, ToolMessage
from langchain_core.tools import StructuredTool
from langchain_openai import ChatOpenAI
from langgraph.graph import END, START, StateGraph
from langgraph.graph.message import add_messages
from langgraph.prebuilt import InjectedState, ToolNode
from langgraph.types import Command, interrupt

from . import code_tools, config, memory, verifier, write_tools
from .prompts import (
    CONTEXT_SUMMARY_PROMPT,
    READ_ONLY_RULES,
    SYSTEM_PROMPT,
    WRITE_RULES,
)
from .review_workflow import REVIEW_SYSTEM_PROMPT, CodeReview
from .spec_workflow import (
    DESIGN_SYSTEM_PROMPT,
    GRILL_SYSTEM_PROMPT,
    GrillDecision,
    ImplementationDesign,
    format_design,
    format_spec,
    spec_as_json,
)


class AgentState(TypedDict):
    messages: Annotated[List[AnyMessage], add_messages]
    task_id: str
    verify_attempts: int
    workflow_mode: str
    new_spec_request: bool
    draft_spec: dict
    approved_spec: dict
    spec_ready: bool
    plan_approved: bool
    design: dict
    design_ready: bool
    design_attempts: int
    source_read_count: int
    design_start_index: int
    modified_files: dict
    checks_current: bool
    check_result: str
    check_attempts: int
    review_result: str
    review_attempts: int
    repair_rounds: int
    check_failures: int
    commit_result: str
    context_summary: str
    folded_count: int


READ_TOOLS = [
    StructuredTool.from_function(f)
    for f in (
        code_tools.list_files,
        code_tools.read_file,
        code_tools.search_code,
        code_tools.search_symbol,
        memory.save_knowledge,
        memory.recall_knowledge,
        memory.update_task,
    )
]
DISCOVERY_TOOLS = [
    StructuredTool.from_function(f)
    for f in (
        code_tools.list_files,
        code_tools.read_file,
        code_tools.search_code,
        code_tools.search_symbol,
    )
]
WRITE_TOOL_NAMES = {"edit_file", "create_file", "build_project"}


def _require_spec_path(path: str, spec: dict):
    allowed_paths = spec.get("allowed_paths", [])
    candidate = PurePosixPath(path.replace("\\", "/"))
    if candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError("write path must be relative to the repository root")

    relative_path = code_tools._rel(code_tools._safe_path(path))
    safe_patterns = []
    for pattern in allowed_paths:
        safe_pattern = PurePosixPath(pattern.replace("\\", "/"))
        normalized_pattern = safe_pattern.as_posix()
        first_component = normalized_pattern.split("/", 1)[0]
        if (
            not safe_pattern.is_absolute()
            and ".." not in safe_pattern.parts
            and normalized_pattern not in ("*", "**", "**/*", ".")
            and not any(character in first_component for character in "*?[")
        ):
            safe_patterns.append(normalized_pattern)
    if not any(fnmatch.fnmatchcase(relative_path, pattern) for pattern in safe_patterns):
        raise ValueError(
            "write path is outside the approved Spec allowlist: %s" % relative_path
        )


def _build_write_tools():
    def require_approved_plan(
        plan_approved: bool, repair_rounds: int, check_failures: int
    ):
        if not plan_approved:
            raise ValueError("the implementation plan has not been approved")
        if repair_rounds >= config.MAX_REPAIR_ROUNDS:
            raise ValueError(
                "automatic review-repair limit reached; revise the Spec or handle the remaining issue manually"
            )
        if check_failures >= config.MAX_CHECK_FAILURES:
            raise ValueError(
                "static-check/build failure limit reached; fix the build before asking for more edits"
            )

    def scoped_edit_file(
        path: str,
        old_str: str,
        new_str: str,
        approved_spec: Annotated[dict, InjectedState("approved_spec")],
        plan_approved: Annotated[bool | None, InjectedState("plan_approved")] = None,
        modified_files: Annotated[dict | None, InjectedState("modified_files")] = None,
        repair_rounds: Annotated[int | None, InjectedState("repair_rounds")] = None,
        check_failures: Annotated[int | None, InjectedState("check_failures")] = None,
    ) -> str:
        """Replace one unique text occurrence under the approved Spec paths."""
        require_approved_plan(plan_approved, repair_rounds or 0, check_failures or 0)
        _require_spec_path(path, approved_spec)
        write_tools.ensure_safe_write_target(path, modified_files or {})
        return write_tools.edit_file(path, old_str, new_str)

    def scoped_create_file(
        path: str,
        content: str,
        approved_spec: Annotated[dict, InjectedState("approved_spec")],
        plan_approved: Annotated[bool | None, InjectedState("plan_approved")] = None,
        modified_files: Annotated[dict | None, InjectedState("modified_files")] = None,
        repair_rounds: Annotated[int | None, InjectedState("repair_rounds")] = None,
        check_failures: Annotated[int | None, InjectedState("check_failures")] = None,
    ) -> str:
        """Create a new source file under the approved Spec paths."""
        require_approved_plan(plan_approved, repair_rounds or 0, check_failures or 0)
        _require_spec_path(path, approved_spec)
        write_tools.ensure_safe_write_target(
            path, modified_files or {}, creating=True
        )
        return write_tools.create_file(path, content)

    # build_project is intentionally NOT bound: the fixed `checks` step already configures,
    # builds and runs CTest once. Letting the model build first doubled the work and cost an
    # extra approval round for the same information.
    return [
        StructuredTool.from_function(scoped_edit_file, name="edit_file"),
        StructuredTool.from_function(scoped_create_file, name="create_file"),
    ]


def _context_chars(messages) -> int:
    return sum(len(str(m.content)) for m in messages)


def _fold_context(state, model):
    """Summarise the oldest turns once the prompt budget is exceeded.

    Truncating old tool output (the first attempt at this) made the model forget what it had
    already read and re-read the same file up to 8 times; an LLM summary keeps the facts without
    the bulk. Returns ``(summary, folded_count, window)`` where ``window`` is what should be sent
    to the model next. The window never starts with a ToolMessage, otherwise the
    AIMessage(tool_calls)/ToolMessage pairing sent to the provider would be invalid.
    """
    messages = state["messages"]
    folded = max(0, int(state.get("folded_count") or 0))
    summary = (state.get("context_summary") or "").strip()
    window = messages[folded:]

    if len(window) <= config.CONTEXT_KEEP_MESSAGES:
        return summary, folded, window
    if _context_chars(window) <= config.CONTEXT_BUDGET_CHARS:
        return summary, folded, window

    prefix = [0]
    for message in window:
        prefix.append(prefix[-1] + len(str(message.content)))

    cut = max(1, len(window) - config.CONTEXT_KEEP_MESSAGES)
    while cut > 1 and prefix[cut] > config.CONTEXT_FOLD_CHARS:
        cut -= 1
    # Fold a little more if the kept window would otherwise start with a tool result.
    while cut < len(window) - 1 and isinstance(window[cut], ToolMessage):
        cut += 1

    if cut >= len(window) or prefix[cut] < config.CONTEXT_MIN_FOLD_CHARS:
        return summary, folded, window

    lines = []
    for message in window[:cut]:
        body = str(message.content)
        if getattr(message, "tool_calls", None):
            body += " [tool_calls: %s]" % ", ".join(
                call["name"] for call in message.tool_calls
            )
        lines.append(
            "【%s】%s" % (type(message).__name__.replace("Message", ""), body[:4000])
        )
    prompt = "已有摘要：\n%s\n\n新增对话：\n%s" % (summary or "（无）", "\n\n".join(lines))
    summarized = model.invoke(
        [
            SystemMessage(content=CONTEXT_SUMMARY_PROMPT),
            HumanMessage(content=prompt[: config.CONTEXT_FOLD_CHARS * 2]),
        ]
    )
    text = (
        summarized.content
        if isinstance(summarized.content, str)
        else str(summarized.content)
    )
    new_summary = text.strip()[: config.CONTEXT_SUMMARY_MAX_CHARS]
    return new_summary, folded + cut, messages[folded + cut:]


def build_graph(
    checkpointer=None,
    allow_write: bool = False,
    llm=None,
    reviewer_llm=None,
):
    tools = READ_TOOLS + (_build_write_tools() if allow_write else [])
    model = llm if llm is not None else ChatOpenAI(model=config.MODEL, temperature=0)
    bound_llm = model.bind_tools(tools)
    discovery_llm = model.bind_tools(DISCOVERY_TOOLS)
    reviewer = reviewer_llm

    def agent_node(state: AgentState):
        # Rebuild the system prompt every turn so freshly saved memory is visible.
        approved_spec = state.get("approved_spec")
        summary, folded, window = _fold_context(state, model)
        sys = SystemMessage(
            content=SYSTEM_PROMPT.format(
                project_context=memory.project_context(),
                knowledge_index=memory.knowledge_index(),
                task_id=state["task_id"],
                task_state=memory.task_summary(state["task_id"]),
                implementation_spec=(
                    spec_as_json(approved_spec)
                    if state.get("workflow_mode") == "implementation" and approved_spec
                    else "(无已批准的实现 Spec；按普通只读问答处理)"
                ),
                implementation_design=(
                    state.get("design") or "(尚无已批准的架构方案)"
                ),
                context_summary=summary or "(暂无；较早的对话尚未压缩)",
                mode_rules=WRITE_RULES if allow_write else READ_ONLY_RULES,
            )
        )
        return {
            "messages": [bound_llm.invoke([sys] + window)],
            "context_summary": summary,
            "folded_count": folded,
        }

    def discovery_agent_node(state: AgentState):
        system = SystemMessage(
            content=DESIGN_SYSTEM_PROMPT.format(
                approved_spec=spec_as_json(state["approved_spec"])
            )
        )
        return {"messages": [discovery_llm.invoke([system] + state["messages"])]}

    def route_after_discovery_agent(state: AgentState):
        calls = getattr(state["messages"][-1], "tool_calls", None) or []
        return "discovery_tools" if calls else "design"

    def discovery_tools_node(state: AgentState):
        messages = state["messages"][state.get("design_start_index", 0):]
        read_count = sum(
            1
            for message in messages
            if (
                isinstance(message, ToolMessage)
                and message.name == "read_file"
                and not message.content.startswith(("ERROR:", "Error:"))
            )
        )
        return {
            "source_read_count": max(state.get("source_read_count", 0), read_count)
        }

    def design_node(state: AgentState):
        design = model.with_structured_output(
            ImplementationDesign, method=config.STRUCTURED_METHOD
        ).invoke(
            [
                SystemMessage(
                    content=(
                        DESIGN_SYSTEM_PROMPT.format(
                            approved_spec=spec_as_json(state["approved_spec"])
                        )
                        + "\n请根据前面只读工具返回的源码，输出 ImplementationDesign。"
                    )
                )
            ]
            + state["messages"]
        )
        problems = []
        if not state.get("source_read_count"):
            problems.append("尚未读取实际源码文件；请先调用 read_file 检查目标实现。")
        for item in design.files:
            try:
                _require_spec_path(item.path, state["approved_spec"])
            except (ValueError, OSError) as exc:
                problems.append("%s：%s" % (item.path, exc))
            for citation in item.evidence:
                if not verifier.CITE_RX.search(citation):
                    problems.append(
                        "%s：源码依据必须包含 file:line 格式，实际为 %s"
                        % (item.path, citation)
                    )
                citation_problems = verifier.check_citations(citation)
                problems.extend(
                    "%s：%s" % (item.path, problem)
                    for problem in citation_problems
                )

        attempts = state.get("design_attempts", 0) + 1
        if problems:
            if attempts >= config.MAX_DESIGN_RETRIES:
                return {
                    "messages": [
                        AIMessage(
                            content="架构方案未通过前置检查，尚未开始实现：\n- "
                            + "\n- ".join(problems)
                        )
                    ],
                    "design_ready": False,
                    "design_attempts": attempts,
                }
            return {
                "messages": [
                    HumanMessage(
                        content="[design-checker] 请补充源码依据或修正越界文件计划：\n- "
                        + "\n- ".join(problems)
                    )
                ],
                "design_ready": False,
                "design_attempts": attempts,
            }

        plan = design.model_dump()
        return {
            "messages": [
                AIMessage(
                    content=(
                        "我已检查相关源码，下面是架构与文件计划。批准后才开始生成和修改代码。\n\n"
                        + format_spec(state["approved_spec"])
                        + "\n\n"
                        + format_design(plan)
                    )
                )
            ],
            "design": plan,
            "design_ready": True,
            "design_attempts": attempts,
        }

    def route_start(state: AgentState):
        if (
            state.get("workflow_mode") == "implementation"
            and state.get("new_spec_request")
        ):
            return "grill"
        return "agent"

    def grill_node(state: AgentState):
        system = SystemMessage(
            content=GRILL_SYSTEM_PROMPT.format(project_context=memory.project_context())
        )
        decision = model.with_structured_output(
            GrillDecision, method=config.STRUCTURED_METHOD
        ).invoke(
            [system] + state["messages"]
        )
        missing_spec_items = []
        if decision.spec is not None:
            required_items = (
                (decision.spec.goal.strip(), "请明确这次修改的目标。"),
                (decision.spec.included_scope, "请明确这次修改包含哪些内容。"),
                (decision.spec.allowed_paths, "请确认允许修改的仓库相对路径或目录范围（例如 src/com/**）。"),
                (decision.spec.implementation_steps, "请补充可执行的实施步骤。"),
                (decision.spec.acceptance_criteria, "请补充可检查的验收标准。"),
                (decision.spec.verification, "请说明如何验证改动。"),
            )
            missing_spec_items = [
                question for value, question in required_items if not value
            ]
        spec_ready = (
            decision.ready
            and decision.spec is not None
            and not missing_spec_items
        )
        if not spec_ready:
            questions = list(decision.questions)
            questions.extend(
                question for question in missing_spec_items if question not in questions
            )
            if not questions:
                questions = [
                    "请补充目标、修改范围、允许写入路径，以及可检查的验收与验证方式。"
                ]
            questions = questions[:5]
            response = (
                "为了避免我按自己的假设动手，我还需要确认：\n"
                + "\n".join("%d. %s" % (index, question) for index, question in enumerate(questions, 1))
            )
            return {
                "messages": [AIMessage(content=response)],
                "draft_spec": None,
                "approved_spec": None,
                "spec_ready": False,
                "new_spec_request": False,
                "design": None,
                "design_ready": False,
                "design_attempts": 0,
                "source_read_count": 0,
                "plan_approved": False,
                "modified_files": {},
                "checks_current": False,
                "check_result": "",
                "check_attempts": 0,
                "review_result": "",
                "review_attempts": 0,
                "repair_rounds": 0,
                "check_failures": 0,
                "commit_result": "",
            }

        spec = decision.spec.model_dump()
        return {
            "messages": [
                AIMessage(
                    content=(
                        format_spec(spec)
                        + "\n\n请检查这份 Spec。批准后才会进入源码检查和实现；"
                        "你也可以要求修改或取消。"
                    )
                )
            ],
            "draft_spec": spec,
            "approved_spec": None,
            "spec_ready": True,
            "new_spec_request": False,
            "design": None,
            "design_ready": False,
            "design_attempts": 0,
            "source_read_count": 0,
            "plan_approved": False,
            "modified_files": {},
            "checks_current": False,
            "check_result": "",
            "check_attempts": 0,
            "review_result": "",
            "review_attempts": 0,
            "repair_rounds": 0,
            "check_failures": 0,
            "commit_result": "",
        }

    def route_after_grill(state: AgentState):
        if state.get("spec_ready") and state.get("draft_spec"):
            return "spec_approval"
        return END

    def spec_approval_node(state: AgentState):
        answer = interrupt(
            {"kind": "spec", "spec": state["draft_spec"]}
        )
        normalized = answer.strip().lower() if isinstance(answer, str) else ""
        if normalized in ("approve", "approved", "y", "yes"):
            return Command(
                goto="discovery_agent",
                update={
                    "approved_spec": state["draft_spec"],
                    "draft_spec": None,
                    "spec_ready": False,
                    "plan_approved": False,
                    "design": None,
                    "design_ready": False,
                    "design_attempts": 0,
                    "source_read_count": 0,
                    "design_start_index": len(state["messages"]),
                    "modified_files": {},
                    "checks_current": False,
                    "check_result": "",
                    "check_attempts": 0,
                    "review_result": "",
                    "review_attempts": 0,
                    "repair_rounds": 0,
                    "check_failures": 0,
                    "commit_result": "",
                },
            )
        if normalized in ("cancel", "取消"):
            return Command(
                goto=END,
                update={
                    "messages": [AIMessage(content="已取消这次实现任务，没有执行代码修改。")],
                    "draft_spec": None,
                    "spec_ready": False,
                    "approved_spec": None,
                },
            )

        reason = answer.strip() if isinstance(answer, str) and answer.strip() else "请根据对话重新检查 Spec"
        return Command(
            goto="grill",
            update={
                "messages": [
                    HumanMessage(content="[spec-review] 用户要求修改 Spec：" + reason)
                ],
                "draft_spec": None,
                "approved_spec": None,
                "spec_ready": False,
            },
        )

    def route_after_design(state: AgentState):
        if state.get("design_ready") and state.get("design"):
            return "design_approval"
        if state.get("design_attempts", 0) >= config.MAX_DESIGN_RETRIES:
            return END
        return "discovery_agent"

    def design_approval_node(state: AgentState):
        answer = interrupt(
            {
                "kind": "design",
                "design": state["design"],
            }
        )
        normalized = answer.strip().lower() if isinstance(answer, str) else ""
        if normalized in ("approve", "approved", "y", "yes"):
            return Command(
                goto="agent",
                update={
                    "plan_approved": True,
                    "review_result": "",
                    # Without this nudge the implementation agent sees its own "please approve"
                    # message as the last turn and just asks for approval again.
                    "messages": [
                        HumanMessage(
                            content=(
                                "[plan-approved] 用户已批准上面的架构与文件计划，现在开始实施。"
                                "先读取目标文件的精确原文，再用 edit_file/create_file 提交第一个改动；"
                                "每次写入前都会显示 Diff 等待人工批准。不要再次征求计划批准。"
                            )
                        )
                    ],
                },
            )
        if normalized in ("cancel", "取消"):
            return Command(
                goto=END,
                update={
                    "messages": [
                        AIMessage(content="已取消这次实现任务，没有执行代码修改。")
                    ],
                    "approved_spec": None,
                    "design": None,
                    "plan_approved": False,
                },
            )

        reason = (
            answer.strip()
            if isinstance(answer, str) and answer.strip()
            else "请根据对话重新检查架构和文件计划"
        )
        return Command(
            goto="discovery_agent",
            update={
                "messages": [
                    HumanMessage(content="[design-review] 用户要求修改架构/文件计划：" + reason)
                ],
                "design": None,
                "design_ready": False,
                "design_attempts": 0,
                "source_read_count": 0,
                "design_start_index": len(state["messages"]),
                "plan_approved": False,
            },
        )

    def route_after_agent(state: AgentState):
        calls = getattr(state["messages"][-1], "tool_calls", None) or []
        if not calls:
            if state.get("commit_result", "").startswith("COMMIT SUCCEEDED:"):
                return "verify"
            if (
                state.get("workflow_mode") == "implementation"
                and state.get("plan_approved")
                and state.get("modified_files")
            ):
                if not state.get("checks_current"):
                    return "check_approval"
                check_result = state.get("check_result", "")
                if check_result.startswith("CHECK FAILED:"):
                    return "verify"
                if state.get("review_result", "").startswith("REVIEW PASSED:"):
                    return "commit_approval"
                if state.get("review_result", "").startswith("REVIEW NEEDS CHANGES:"):
                    return "verify"
                return "review"
            return "verify"
        if any(c["name"] in WRITE_TOOL_NAMES for c in calls):
            if (
                state.get("repair_rounds", 0) >= config.MAX_REPAIR_ROUNDS
                or state.get("check_failures", 0) >= config.MAX_CHECK_FAILURES
            ):
                return "repair_limit"
            return "approve"
        return "tools"

    def after_tools_node(state: AgentState):
        modified_files = dict(state.get("modified_files") or {})
        for message in reversed(state["messages"]):
            if not isinstance(message, ToolMessage):
                break
            if (
                message.name in ("edit_file", "create_file")
                and message.content.startswith(("edited ", "created "))
            ):
                path = message.content.split(" ", 2)[1]
                modified_files[path] = write_tools.file_digest(path)
        if modified_files != (state.get("modified_files") or {}):
            return {
                "modified_files": modified_files,
                "checks_current": False,
                "review_result": "",
                "commit_result": "",
            }
        return {}

    def approve_node(state: AgentState):
        calls = state["messages"][-1].tool_calls
        pending = [c for c in calls if c["name"] in WRITE_TOOL_NAMES]
        # Pauses the graph; the CLI or chat UI shows the preview before resuming.
        answer = interrupt({"actions": [write_tools.describe(c) for c in pending]})
        if isinstance(answer, str) and answer.strip().lower() in ("y", "yes", "approve"):
            return Command(goto="tools")
        reason = answer if isinstance(answer, str) and answer.strip() else "no reason given"
        # Every tool_call must get a ToolMessage, otherwise the next LLM call is invalid.
        rejected = [
            ToolMessage(
                content="REJECTED by human reviewer: %s" % reason
                if c["name"] in WRITE_TOOL_NAMES
                else "NOT EXECUTED: batch was rejected; re-issue if still needed.",
                tool_call_id=c["id"],
                name=c["name"],
            )
            for c in calls
        ]
        return Command(goto="agent", update={"messages": rejected})

    def repair_limit_node(state: AgentState):
        if state.get("check_failures", 0) >= config.MAX_CHECK_FAILURES:
            reason = "静态检查/构建已连续失败 %d 次" % config.MAX_CHECK_FAILURES
        else:
            reason = "自动修复已达到 %d 轮上限" % config.MAX_REPAIR_ROUNDS
        return Command(
            goto="verify",
            update={
                "messages": [
                    AIMessage(
                        content=(
                            reason + "，未执行新的写入。请调整 Spec 或人工处理剩余问题。"
                        )
                    )
                ]
            },
        )

    def check_approval_node(state: AgentState):
        answer = interrupt(
            {
                "kind": "checks",
                "actions": [
                    write_tools.describe_project_checks(
                        list(state.get("modified_files") or {})
                    )
                ],
            }
        )
        if isinstance(answer, str) and answer.strip().lower() in (
            "y",
            "yes",
            "approve",
        ):
            return Command(goto="checks")
        reason = answer if isinstance(answer, str) and answer.strip() else "rejected"
        return Command(
            goto="verify",
            update={
                "check_result": "CHECK NOT RUN: static/build/test verification was rejected: %s"
                % reason,
                "checks_current": True,
            },
        )

    def checks_node(state: AgentState):
        result = write_tools.run_project_checks(
            list(state.get("modified_files") or {})
        )
        passed_or_partial = result.startswith(("CHECK PASSED:", "CHECK PARTIAL:"))
        check_failures = state.get("check_failures", 0) + (0 if passed_or_partial else 1)
        if passed_or_partial:
            goto = "review"
        elif check_failures >= config.MAX_CHECK_FAILURES:
            goto = "verify"
        else:
            goto = "agent"
        return Command(
            goto=goto,
            update={
                "check_result": result,
                "checks_current": True,
                "check_attempts": state.get("check_attempts", 0) + 1,
                # A partial check (e.g. zero registered CTest cases) is not a failure and must not
                # consume the repair budget; only real failures do, and they have their own counter.
                "check_failures": check_failures,
                "messages": [HumanMessage(content="[project-checker]\n" + result)],
            },
        )

    def review_node(state: AgentState):
        paths = list(state.get("modified_files") or {})
        try:
            diff = write_tools.collect_changed_diff(
                paths, state.get("modified_files") or {}
            )
        except (OSError, RuntimeError, ValueError) as exc:
            return Command(
                goto="verify",
                update={
                    "review_result": "REVIEW BLOCKED: could not safely collect diff: %s"
                    % exc,
                },
            )
        if not diff:
            return Command(
                goto="verify",
                update={"review_result": "REVIEW SKIPPED: no diff was available."},
            )
        prompt = (
            "Approved Spec:\n%s\n\nApproved architecture/file plan:\n%s\n\n"
            "Current validation result:\n%s\n\nImplementation diff:\n```diff\n%s\n```"
            % (
                spec_as_json(state["approved_spec"]),
                format_design(state["design"]),
                state.get("check_result", ""),
                diff,
            )
        )
        reviewer_model = reviewer
        if reviewer_model is None:
            reviewer_model = ChatOpenAI(model=config.REVIEW_MODEL, temperature=0)
        decision = reviewer_model.with_structured_output(
            CodeReview, method=config.STRUCTURED_METHOD
        ).invoke(
            [
                SystemMessage(content=REVIEW_SYSTEM_PROMPT),
                HumanMessage(content=prompt),
            ]
        )
        attempts = state.get("review_attempts", 0) + 1
        if decision.approved and not decision.findings:
            return Command(
                goto="commit_approval",
                update={
                    "review_result": "REVIEW PASSED: independent diff review found no actionable issues.",
                    "review_attempts": attempts,
                },
            )

        findings = "\n".join(
            "- [%s] %s%s: %s"
            % (
                item.severity,
                item.path,
                (":" + str(item.line)) if item.line else "",
                item.finding,
            )
            for item in decision.findings
        ) or "- Reviewer did not approve; it must provide actionable findings."
        result = "REVIEW NEEDS CHANGES:\n" + findings
        if attempts <= config.MAX_REVIEW_RETRIES:
            return Command(
                goto="agent",
                update={
                    "review_result": result,
                    "review_attempts": attempts,
                    "repair_rounds": state.get("repair_rounds", 0) + 1,
                    "messages": [
                        HumanMessage(
                            content="[independent-reviewer] 修复以下审查问题；"
                            "修改前先核对源码。每项修改仍需人工审批：\n" + findings
                        )
                    ],
                },
            )
        return Command(
            goto="verify",
            update={"review_result": result, "review_attempts": attempts},
        )

    def commit_approval_node(state: AgentState):
        paths = sorted(state.get("modified_files") or {})
        if state.get("commit_result", "").startswith("COMMIT SUCCEEDED:"):
            # Re-entering this node after a successful commit used to attempt a second commit,
            # find a clean working tree and overwrite the success with "COMMIT BLOCKED".
            return Command(goto="verify")
        if not state.get("plan_approved"):
            return Command(
                goto="verify",
                update={"commit_result": "COMMIT BLOCKED: design was not approved."},
            )
        if not state.get("checks_current"):
            return Command(
                goto="verify",
                update={"commit_result": "COMMIT BLOCKED: current code has not been checked."},
            )
        if not state.get("review_result", "").startswith("REVIEW PASSED:"):
            return Command(
                goto="verify",
                update={"commit_result": "COMMIT BLOCKED: independent review did not pass."},
            )
        try:
            for path in paths:
                _require_spec_path(path, state["approved_spec"])
        except (KeyError, OSError, ValueError) as exc:
            return Command(
                goto="verify",
                update={"commit_result": "COMMIT BLOCKED: invalid reviewed path: %s" % exc},
            )
        try:
            diff = write_tools.collect_changed_diff(
                paths, state.get("modified_files") or {}
            )
        except (OSError, RuntimeError, ValueError) as exc:
            return Command(
                goto="verify",
                update={"commit_result": "COMMIT BLOCKED: %s" % exc},
            )
        if not state.get("check_result", "").startswith("CHECK PASSED:"):
            return Command(
                goto="verify",
                update={
                    "commit_result": (
                        "COMMIT BLOCKED: static checks, build, and tests must all pass."
                    )
                },
            )
        answer = interrupt(
            {
                "kind": "commit",
                "paths": paths,
                "diff": diff,
                "message": state["approved_spec"]["goal"],
            }
        )
        normalized = answer.strip() if isinstance(answer, str) else ""
        normalized_lower = normalized.lower()
        if normalized_lower in ("y", "yes", "approve") or normalized_lower.startswith(
            "approve:"
        ):
            message = (
                normalized.split(":", 1)[1].strip()
                if ":" in normalized
                else state["approved_spec"]["goal"]
            )
            if not message:
                return Command(
                    goto="verify",
                    update={"commit_result": "COMMIT NOT RUN: commit message is empty."},
                )
            result = write_tools.commit_agent_changes(
                paths,
                state.get("modified_files") or {},
                message,
            )
            return Command(
                goto="verify",
                update={"commit_result": result},
            )
        reason = answer if isinstance(answer, str) and answer.strip() else "rejected"
        return Command(
            goto="verify",
            update={"commit_result": "COMMIT NOT RUN: human declined (%s)." % reason},
        )

    def verify_node(state: AgentState):
        report = state["messages"][-1].content or ""
        check_result = state.get("check_result", "")
        if state.get("workflow_mode") == "implementation":
            report += "\n\n### 自动校验\n" + (
                check_result or "未执行（没有获批的代码变更）。"
            )
            if state.get("modified_files"):
                report += "\n\n### 独立代码审查\n" + (
                    state.get("review_result") or "未完成。"
                )
                report += "\n\n### Git 提交\n" + (
                    state.get("commit_result") or "未提交。"
                )
            if check_result and not state.get("checks_current"):
                report += "\n注意：该校验结果早于最近一次成功文件修改，当前代码未重新验证。"
            return Command(
                goto=END,
                update={
                    "messages": [
                        state["messages"][-1].model_copy(update={"content": report})
                    ]
                },
            )
        problems = verifier.verify_report(report)
        attempts = state.get("verify_attempts", 0)
        if not problems:
            return Command(
                goto=END,
                update={
                    "messages": [
                        state["messages"][-1].model_copy(update={"content": report})
                    ]
                },
            )
        if attempts >= config.MAX_VERIFY_RETRIES:
            note = "\n\n> ⚠️ 证据检查未完全通过（已重试 %d 次）：\n" % attempts + "\n".join("> - " + p for p in problems)
            return Command(goto=END, update={"messages": [state["messages"][-1].model_copy(update={"content": report + note})]})
        feedback = (
            "[evidence-checker] 报告中的以下问题必须修正（用工具重新核对源码，修正 file:line / 符号，"
            "或为结论补充源码依据；无法证实的结论请删除或标注“未在源码中找到”），然后输出完整的修正版报告：\n"
            + "\n".join("- " + p for p in problems)
        )
        return Command(goto="agent", update={"messages": [HumanMessage(feedback)], "verify_attempts": attempts + 1})

    g = StateGraph(AgentState)
    g.add_node("agent", agent_node)
    g.add_node("grill", grill_node)
    g.add_node(
        "spec_approval",
        spec_approval_node,
        destinations=("discovery_agent", "grill", END),
    )
    g.add_node("discovery_agent", discovery_agent_node)
    g.add_node("discovery_tools", ToolNode(DISCOVERY_TOOLS, handle_tool_errors=True))
    g.add_node("after_discovery_tools", discovery_tools_node)
    g.add_node("design", design_node)
    g.add_node(
        "design_approval",
        design_approval_node,
        destinations=("agent", "discovery_agent", END),
    )
    g.add_node("tools", ToolNode(tools, handle_tool_errors=True))
    g.add_node("after_tools", after_tools_node)
    g.add_node("approve", approve_node, destinations=("tools", "agent"))
    g.add_node("repair_limit", repair_limit_node, destinations=("verify",))
    g.add_node("check_approval", check_approval_node, destinations=("checks", "verify"))
    g.add_node("checks", checks_node, destinations=("review", "agent"))
    g.add_node(
        "review",
        review_node,
        destinations=("commit_approval", "agent", "verify"),
    )
    g.add_node("commit_approval", commit_approval_node, destinations=("verify",))
    g.add_node("verify", verify_node, destinations=("agent", END))
    g.add_conditional_edges(START, route_start, ["grill", "agent"])
    g.add_conditional_edges("grill", route_after_grill, ["spec_approval", END])
    g.add_conditional_edges(
        "discovery_agent",
        route_after_discovery_agent,
        ["discovery_tools", "design"],
    )
    g.add_edge("discovery_tools", "after_discovery_tools")
    g.add_edge("after_discovery_tools", "discovery_agent")
    g.add_conditional_edges(
        "design",
        route_after_design,
        ["discovery_agent", "design_approval", END],
    )
    g.add_conditional_edges(
        "agent",
        route_after_agent,
        [
            "tools",
            "approve",
            "repair_limit",
            "check_approval",
            "review",
            "commit_approval",
            "verify",
        ],
    )
    g.add_edge("tools", "after_tools")
    g.add_edge("after_tools", "agent")
    g.add_edge("checks", "agent")
    return g.compile(checkpointer=checkpointer)
