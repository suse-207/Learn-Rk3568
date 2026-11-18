"""Local browser chat UI for the RK3568 code analysis agent."""
import os
import sqlite3
import uuid
from pathlib import Path

import streamlit as st
from langchain_core.messages import AIMessage, HumanMessage
from langgraph.checkpoint.sqlite import SqliteSaver
from langgraph.types import Command

from code_agent import config, telemetry
from code_agent.graph import build_graph
from code_agent.spec_workflow import format_design, format_plan, format_spec


@st.cache_resource
def _load_graph(allow_write: bool):
    config.MEMORY_DIR.mkdir(parents=True, exist_ok=True)
    checkpoint_db = Path(os.getenv("AGENT_CHECKPOINT_DB", str(config.CHECKPOINT_DB)))
    checkpoint_db.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(str(checkpoint_db), check_same_thread=False)
    checkpointer = SqliteSaver(connection)
    checkpointer.setup()
    return build_graph(checkpointer, allow_write=allow_write)


def _chat_history(messages):
    history = []
    for message in messages:
        if isinstance(message, HumanMessage):
            if not message.content.startswith(
                (
                    "[evidence-checker]",
                    "[spec-review]",
                    "[design-review]",
                    "[design-checker]",
                    "[project-checker]",
                    "[independent-reviewer]",
                )
            ):
                history.append({"role": "user", "content": message.content})
        elif isinstance(message, AIMessage) and not message.tool_calls and message.content:
            history.append({"role": "assistant", "content": message.content})
    return history


def _pending_approval(snapshot):
    for task in getattr(snapshot, "tasks", ()):
        for interruption in getattr(task, "interrupts", ()):
            value = getattr(interruption, "value", None)
            if (
                isinstance(value, dict)
                and (
                    value.get("actions")
                    or value.get("kind") in ("spec", "plan", "design", "commit")
                )
            ):
                return value
    return None


def _thread_config(task_id):
    return {
        "configurable": {"thread_id": task_id},
        "recursion_limit": config.RECURSION_LIMIT,
    }


def _run_turn(
    graph, thread_config, task_id, text=None, approval=None, workflow_mode="analysis"
):
    input_state = Command(resume=approval) if approval is not None else {
        "messages": [HumanMessage(content=text)],
        "task_id": task_id,
        "verify_attempts": 0,
        "workflow_mode": workflow_mode,
        "new_spec_request": workflow_mode == "implementation",
    }
    interruption = None
    stats = telemetry.RunStats()
    thread_config = dict(thread_config)
    thread_config["callbacks"] = [stats]
    try:
        for update in graph.stream(input_state, thread_config, stream_mode="updates"):
            stats.steps += 1
            if "__interrupt__" in update:
                interruption = update["__interrupt__"][0].value
    finally:
        telemetry.record_run(task_id, config.MODEL, workflow_mode, stats, "ok")
    snapshot = graph.get_state(thread_config)
    return _chat_history(snapshot.values.get("messages", [])), interruption


def _new_task_id():
    return "chat-" + uuid.uuid4().hex


def main():
    st.set_page_config(page_title="RK3568 代码助手", page_icon="🚗", layout="centered")
    st.title("RK3568 代码助手")
    st.caption("用自然语言聊 vsomeip、SOME/IP、OTA 和项目代码。")

    if not os.getenv("OPENAI_API_KEY"):
        st.error("还没有配置模型密钥。请在 agent/.env 中设置 OPENAI_API_KEY 后重启此应用。")
        st.stop()

    query_task = st.query_params.get("task")
    if "task_id" not in st.session_state:
        st.session_state.task_id = query_task or _new_task_id()
        st.query_params["task"] = st.session_state.task_id
    if "implementation_mode" not in st.session_state:
        st.session_state.implementation_mode = (
            st.query_params.get("mode") == "implementation"
        )

    with st.sidebar:
        st.subheader("会话")
        st.caption("同一会话会保存对话检查点。")
        if st.button("新建会话", use_container_width=True):
            st.session_state.task_id = _new_task_id()
            st.session_state.pop("chat_history", None)
            st.session_state.pop("pending_approval", None)
            st.session_state.pop("approval_rejection_reason", None)
            st.session_state.pop("design_revision", None)
            st.session_state.pop("commit_message", None)
            st.query_params["task"] = st.session_state.task_id
            st.rerun()

        implementation_mode = st.toggle(
            "实现/修改代码（先走 Spec）",
            key="implementation_mode",
            help="开启后先批准 Spec、源码依据上的架构/文件方案；每次改动、检查及最终提交分别审批。",
        )
        selected_mode = "implementation" if implementation_mode else "analysis"
        if st.query_params.get("mode") != selected_mode:
            st.query_params["mode"] = selected_mode
        if implementation_mode:
            st.warning(
                "将先批准 Spec 与源码依据上的架构/文件计划；每个 Diff、静态检查/构建/测试和最终提交都需确认。"
            )
        st.caption("项目目录")
        st.code(str(config.REPO_ROOT), language=None)
        st.caption("模型")
        st.code(config.MODEL, language=None)
        if implementation_mode:
            st.caption("Reviewer 模型")
            st.code(config.REVIEW_MODEL, language=None)

    graph = _load_graph(implementation_mode)
    task_id = st.session_state.task_id
    thread_config = _thread_config(task_id)
    snapshot = graph.get_state(thread_config)
    if "chat_history" not in st.session_state:
        st.session_state.chat_history = _chat_history(
            snapshot.values.get("messages", [])
        )
    if "pending_approval" not in st.session_state:
        st.session_state.pending_approval = _pending_approval(snapshot)

    for message in st.session_state.chat_history:
        with st.chat_message(message["role"]):
            st.markdown(message["content"])

    pending = st.session_state.pending_approval
    if pending:
        if pending.get("kind") == "spec":
            st.warning("请先审阅需求 Spec。批准后 Agent 才会只读检查源码并提出架构/文件方案。")
            st.markdown(format_spec(pending["spec"]))
            revision = st.text_area(
                "可选：告诉 Agent 要如何修改这份 Spec",
                key="spec_revision",
                placeholder="例如：限制只改 src/com，不修改 OTA 模块",
            )
            approve_col, revise_col, cancel_col = st.columns(3)
            with approve_col:
                approve = st.button("批准 Spec", type="primary", use_container_width=True)
            with revise_col:
                revise = st.button(
                    "要求修改", use_container_width=True, disabled=not revision.strip()
                )
            with cancel_col:
                cancel = st.button("取消任务", use_container_width=True)
            if approve or revise or cancel:
                answer = (
                    "approve" if approve
                    else "cancel" if cancel
                    else "revise: " + revision.strip()
                )
                try:
                    history, interruption = _run_turn(
                        graph, thread_config, task_id, approval=answer
                    )
                    st.session_state.chat_history = history
                    st.session_state.pending_approval = interruption
                    st.session_state.pop("spec_revision", None)
                except Exception as exc:
                    st.exception(exc)
                else:
                    st.rerun()
        elif pending.get("kind") in ("plan", "design"):
            st.warning("请审阅架构和文件计划。批准后 Agent 才会开始生成/修改代码。")
            if pending.get("kind") == "design":
                st.markdown(format_design(pending["design"]))
            else:
                st.markdown(format_plan(pending["plan"]))
            revision = st.text_area(
                "可选：告诉 Agent 要如何调整方案",
                key="design_revision",
                placeholder="例如：复用现有 Proxy 接口，先补充失败路径测试",
            )
            approve_col, revise_col, cancel_col = st.columns(3)
            with approve_col:
                approve = st.button("批准方案", type="primary", use_container_width=True)
            with revise_col:
                revise = st.button(
                    "要求调整", use_container_width=True, disabled=not revision.strip()
                )
            with cancel_col:
                cancel = st.button("取消任务", use_container_width=True)
            if approve or revise or cancel:
                answer = (
                    "approve" if approve
                    else "cancel" if cancel
                    else "revise: " + revision.strip()
                )
                try:
                    history, interruption = _run_turn(
                        graph, thread_config, task_id, approval=answer
                    )
                    st.session_state.chat_history = history
                    st.session_state.pending_approval = interruption
                    st.session_state.pop("design_revision", None)
                except Exception as exc:
                    st.exception(exc)
                else:
                    st.rerun()
        elif pending.get("kind") == "commit":
            st.warning("独立 Reviewer 已通过。请审阅最终 Diff，并单独批准 Git 提交。")
            st.caption("提交文件：" + ", ".join(pending["paths"]))
            st.code(pending["diff"] or "(空 Diff)", language="diff")
            commit_message = st.text_input(
                "提交说明",
                value=pending["message"],
                key="commit_message",
            )
            approve_col, reject_col = st.columns(2)
            with approve_col:
                approve = st.button("批准提交", type="primary", use_container_width=True)
            with reject_col:
                reject = st.button("不提交", use_container_width=True)
            if approve or reject:
                answer = (
                    "approve: " + commit_message.strip()
                    if approve
                    else "reject"
                )
                try:
                    history, interruption = _run_turn(
                        graph, thread_config, task_id, approval=answer
                    )
                    st.session_state.chat_history = history
                    st.session_state.pending_approval = interruption
                except Exception as exc:
                    st.exception(exc)
                else:
                    st.rerun()
        else:
            st.warning("Agent 请求执行以下操作。请检查预览后选择是否批准：")
            for index, action in enumerate(pending["actions"], 1):
                with st.expander("操作 %d" % index, expanded=True):
                    st.code(action, language="diff" if action.startswith("edit_file") else None)
            rejection_reason = st.text_input(
                "拒绝时可填写原因",
                key="approval_rejection_reason",
                placeholder="例如：先不要修改这个文件",
            )
            approve_col, reject_col = st.columns(2)
            with approve_col:
                approve = st.button("批准执行", type="primary", use_container_width=True)
            with reject_col:
                reject = st.button("拒绝", use_container_width=True)

            if approve or reject:
                answer = "y" if approve else (rejection_reason.strip() or "用户拒绝")
                try:
                    history, interruption = _run_turn(
                        graph, thread_config, task_id, approval=answer
                    )
                    st.session_state.chat_history = history
                    st.session_state.pending_approval = interruption
                    st.session_state.pop("approval_rejection_reason", None)
                except Exception as exc:
                    st.exception(exc)
                else:
                    st.rerun()

    prompt = st.chat_input(
        "输入问题，例如：分析 SOME/IP 服务发现流程",
        disabled=bool(st.session_state.pending_approval),
    )
    if prompt:
        st.session_state.chat_history.append({"role": "user", "content": prompt})
        with st.chat_message("user"):
            st.markdown(prompt)
        succeeded = False
        with st.chat_message("assistant"):
            try:
                with st.spinner("我在查看项目并整理答案…"):
                    history, interruption = _run_turn(
                        graph,
                        thread_config,
                        task_id,
                        text=prompt,
                        workflow_mode=(
                            "implementation" if implementation_mode else "analysis"
                        ),
                    )
                st.session_state.chat_history = history
                st.session_state.pending_approval = interruption
                if history and history[-1]["role"] == "assistant":
                    st.markdown(history[-1]["content"])
                elif interruption:
                    st.info("需要你确认上面的操作预览。")
                succeeded = True
            except Exception as exc:
                st.exception(exc)
        if succeeded:
            st.rerun()


if __name__ == "__main__":
    main()
