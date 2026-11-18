from types import SimpleNamespace

import pytest
from langchain_core.messages import AIMessage, HumanMessage, ToolMessage
from langgraph.checkpoint.memory import MemorySaver
from langgraph.types import Command

from chat_app import _chat_history
from code_agent import config, write_tools
from code_agent.graph import _build_write_tools, _require_spec_path, build_graph
from code_agent.review_workflow import CodeReview
from code_agent.spec_workflow import (
    GrillDecision,
    ImplementationDesign,
    TaskSpec,
)


class ScriptedLLM:
    def __init__(self, responses):
        self.responses = list(responses)
        self.bound_tool_names = set()
        self.invocations = []

    def bind_tools(self, tools):
        self.bound_tool_names = {tool.name for tool in tools}
        return self

    def with_structured_output(self, schema):
        return self

    def invoke(self, messages):
        assert self.responses, "unexpected model invocation after %d calls" % len(
            self.invocations
        )
        self.invocations.append(messages)
        return self.responses.pop(0)


def _tool_call(name, args, call_id="call-1"):
    return AIMessage(
        content="",
        tool_calls=[
            {"name": name, "args": args, "id": call_id, "type": "tool_call"}
        ],
    )


def _valid_report():
    return AIMessage(
        content="- 结论: Sample::run 存在（`src/sample.cpp:2 Sample::run`）"
    )


def _run(graph, task_id, message):
    config = {"configurable": {"thread_id": task_id}}
    list(
        graph.stream(
            {"messages": [HumanMessage(content=message)], "task_id": task_id, "verify_attempts": 0},
            config,
            stream_mode="updates",
        )
    )
    return config


def test_read_tool_flow_finishes_with_verified_report(repo):
    llm = ScriptedLLM(
        [
            _tool_call("read_file", {"path": "src/sample.cpp"}),
            _valid_report(),
        ]
    )
    graph = build_graph(MemorySaver(), llm=llm)
    cfg = _run(graph, "read-flow", "Analyze Sample::run")

    state = graph.get_state(cfg).values
    assert "Sample::run" in state["messages"][-1].content
    assert state["verify_attempts"] == 0


def test_verifier_feedback_causes_report_retry(repo):
    invalid = AIMessage(
        content="- 结论: Sample::run 存在（`src/sample.cpp:200 Sample::run`）"
    )
    llm = ScriptedLLM([invalid, _valid_report()])
    graph = build_graph(MemorySaver(), llm=llm)
    cfg = _run(graph, "retry-flow", "Analyze Sample::run")

    state = graph.get_state(cfg).values
    assert state["verify_attempts"] == 1
    assert state["messages"][-1].content == _valid_report().content
    assert any(isinstance(message, HumanMessage) and "行号越界" in message.content
               for message in state["messages"])


def test_exhausted_verifier_retries_mark_report(repo):
    invalid = AIMessage(
        content="- 结论: Sample::run 存在（`src/sample.cpp:200 Sample::run`）"
    )
    llm = ScriptedLLM([invalid, invalid, invalid])
    graph = build_graph(MemorySaver(), llm=llm)
    cfg = _run(graph, "retry-exhausted", "Analyze Sample::run")

    report = graph.get_state(cfg).values["messages"][-1].content
    assert "证据检查未完全通过" in report


def test_write_tool_requires_approval_and_creates_backup(repo):
    llm = ScriptedLLM(
        [
            _tool_call(
                "edit_file",
                {"path": "src/sample.cpp", "old_str": "class Sample", "new_str": "class Vehicle"},
            ),
            AIMessage(content="edit complete"),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "approved-edit"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename the type")],
                "task_id": "approved-edit",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": {"allowed_paths": ["src/sample.cpp"]},
                "plan_approved": True,
            },
            cfg,
            stream_mode="updates",
        )
    )
    assert any("__interrupt__" in update for update in updates)
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")

    list(graph.stream(Command(resume="y"), cfg, stream_mode="updates"))
    assert "Vehicle" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")
    assert list((repo / "memory" / "backups").iterdir())


def test_rejected_write_is_not_executed(repo):
    llm = ScriptedLLM(
        [
            _tool_call(
                "edit_file",
                {"path": "src/sample.cpp", "old_str": "class Sample", "new_str": "class Vehicle"},
            ),
            AIMessage(content="I will leave the file unchanged."),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "rejected-edit"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename the type")],
                "task_id": "rejected-edit",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": {"allowed_paths": ["src/sample.cpp"]},
                "plan_approved": True,
            },
            cfg,
            stream_mode="updates",
        )
    )
    list(graph.stream(Command(resume="keep the current name"), cfg, stream_mode="updates"))

    state = graph.get_state(cfg).values
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")
    assert any(
        isinstance(message, ToolMessage) and "REJECTED" in message.content
        for message in state["messages"]
    )


def test_write_tool_enforces_plan_approval_in_state(repo):
    llm = ScriptedLLM(
        [
            _tool_call(
                "edit_file",
                {"path": "src/sample.cpp", "old_str": "Sample", "new_str": "Vehicle"},
            ),
            AIMessage(content="The plan must be approved before editing."),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "missing-plan-approval"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "missing-plan-approval",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": {"allowed_paths": ["src/sample.cpp"]},
            },
            cfg,
            stream_mode="updates",
        )
    )
    updates = list(graph.stream(Command(resume="y"), cfg, stream_mode="updates"))

    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")
    tool_messages = [
        message
        for update in updates
        for message in update.get("tools", {}).get("messages", [])
    ]
    assert any("plan has not been approved" in message.content for message in tool_messages)


def test_repair_limit_stops_before_showing_or_executing_another_write(repo):
    llm = ScriptedLLM(
        [
            _tool_call(
                "edit_file",
                {"path": "src/sample.cpp", "old_str": "Sample", "new_str": "Vehicle"},
            )
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "repair-limit"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Fix the review finding")],
                "task_id": "repair-limit",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "plan_approved": True,
                "repair_rounds": config.MAX_REPAIR_ROUNDS,
            },
            cfg,
            stream_mode="updates",
        )
    )

    report = graph.get_state(cfg).values["messages"][-1].content
    assert not any("__interrupt__" in update for update in updates)
    assert "达到" in report and "上限" in report
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")


def test_read_only_graph_does_not_bind_write_tools(repo):
    llm = ScriptedLLM([AIMessage(content="Done.")])
    build_graph(MemorySaver(), allow_write=False, llm=llm)
    assert "edit_file" not in llm.bound_tool_names
    assert "build_project" not in llm.bound_tool_names


def test_internal_approval_state_is_not_exposed_in_write_tool_schema():
    tools = {tool.name: tool for tool in _build_write_tools()}

    assert set(tools["edit_file"].tool_call_schema.model_fields) == {
        "path",
        "old_str",
        "new_str",
    }
    assert set(tools["create_file"].tool_call_schema.model_fields) == {"path", "content"}
    assert set(tools["build_project"].tool_call_schema.model_fields) == {"mode"}


def _sample_spec():
    return TaskSpec(
        goal="Rename Sample without changing its behavior",
        included_scope=["Rename the class and its method definition"],
        excluded_scope=["Do not change runtime behavior"],
        target_areas=["src/sample.cpp"],
        allowed_paths=["src/sample.cpp"],
        implementation_steps=["Inspect references", "Rename and test"],
        acceptance_criteria=["The project builds"],
        verification=["Run the project build"],
        assumptions=[],
    )


def _sample_design():
    return ImplementationDesign(
        architecture="Keep the existing class in the same source module.",
        files=[
            {
                "path": "src/sample.cpp",
                "change": "Rename the class and definition.",
                "reason": "The requested type is implemented here.",
                "evidence": ["src/sample.cpp:1-2"],
            }
        ],
        verification=["Run cppcheck, CMake build, and CTest."],
        risks=[],
    )


def test_implementation_mode_asks_before_creating_a_spec(repo):
    llm = ScriptedLLM(
        [GrillDecision(ready=False, questions=["Which files may be changed?"])]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "grill-first"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "grill-first",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "new_spec_request": True,
            },
            cfg,
            stream_mode="updates",
        )
    )

    state = graph.get_state(cfg).values
    assert not any("__interrupt__" in update for update in updates)
    assert "Which files may be changed?" in state["messages"][-1].content
    assert state["draft_spec"] is None


def test_spec_cannot_be_approved_without_an_allowlist(repo):
    spec = _sample_spec().model_copy(update={"allowed_paths": []})
    llm = ScriptedLLM([GrillDecision(ready=True, spec=spec)])
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "spec-needs-scope"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "spec-needs-scope",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "new_spec_request": True,
            },
            cfg,
            stream_mode="updates",
        )
    )

    state = graph.get_state(cfg).values
    assert not any("__interrupt__" in update for update in updates)
    assert "允许修改" in state["messages"][-1].content


def test_spec_must_be_approved_before_generation_and_writes(repo):
    llm = ScriptedLLM(
        [
            GrillDecision(ready=True, spec=_sample_spec()),
            _tool_call("read_file", {"path": "src/sample.cpp"}, "discover-read"),
            AIMessage(content="I inspected the current implementation."),
            _sample_design(),
            _tool_call(
                "edit_file",
                {"path": "src/sample.cpp", "old_str": "class Sample", "new_str": "class Vehicle"},
            ),
            AIMessage(content="Implemented the approved change."),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "spec-approval"}}
    request = {
        "messages": [HumanMessage(content="Rename Sample")],
        "task_id": "spec-approval",
        "verify_attempts": 0,
        "workflow_mode": "implementation",
        "new_spec_request": True,
    }

    updates = list(graph.stream(request, cfg, stream_mode="updates"))
    spec_interrupt = next(
        update["__interrupt__"][0].value
        for update in updates
        if "__interrupt__" in update
    )
    assert spec_interrupt["kind"] == "spec"
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")

    design_updates = list(
        graph.stream(Command(resume="approve"), cfg, stream_mode="updates")
    )
    design_interrupt = next(
        update["__interrupt__"][0].value
        for update in design_updates
        if "__interrupt__" in update
    )
    assert design_interrupt["kind"] == "design"
    assert design_interrupt["design"]["files"][0]["path"] == "src/sample.cpp"
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")

    tool_updates = list(
        graph.stream(Command(resume="approve"), cfg, stream_mode="updates")
    )
    assert "Rename Sample without changing its behavior" in llm.invocations[-1][0].content
    assert any("__interrupt__" in update for update in tool_updates)
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")

    list(graph.stream(Command(resume="y"), cfg, stream_mode="updates"))
    assert "Vehicle" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")


def test_canceling_spec_does_not_invoke_implementation_agent(repo):
    llm = ScriptedLLM([GrillDecision(ready=True, spec=_sample_spec())])
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "spec-cancel"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "spec-cancel",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "new_spec_request": True,
            },
            cfg,
            stream_mode="updates",
        )
    )
    list(graph.stream(Command(resume="cancel"), cfg, stream_mode="updates"))

    state = graph.get_state(cfg).values
    assert "已取消" in state["messages"][-1].content
    assert len(llm.invocations) == 1
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")


def test_canceling_design_does_not_invoke_implementation_agent(repo):
    llm = ScriptedLLM(
        [
            GrillDecision(ready=True, spec=_sample_spec()),
            _tool_call("read_file", {"path": "src/sample.cpp"}, "design-read"),
            AIMessage(content="I checked the current class implementation."),
            _sample_design(),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "plan-cancel"}}
    request = {
        "messages": [HumanMessage(content="Rename Sample")],
        "task_id": "plan-cancel",
        "verify_attempts": 0,
        "workflow_mode": "implementation",
        "new_spec_request": True,
    }
    list(graph.stream(request, cfg, stream_mode="updates"))
    list(graph.stream(Command(resume="approve"), cfg, stream_mode="updates"))
    list(graph.stream(Command(resume="cancel"), cfg, stream_mode="updates"))

    state = graph.get_state(cfg).values
    assert "已取消" in state["messages"][-1].content
    assert len(llm.invocations) == 4
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")


def test_design_revision_rechecks_source_before_any_write(repo):
    llm = ScriptedLLM(
        [
            GrillDecision(ready=True, spec=_sample_spec()),
            _tool_call("read_file", {"path": "src/sample.cpp"}, "design-read-1"),
            AIMessage(content="I inspected the class definition."),
            _sample_design(),
            _tool_call("read_file", {"path": "src/sample.cpp"}, "design-read-2"),
            AIMessage(content="I rechecked the source for the revised plan."),
            _sample_design(),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "plan-revision"}}
    request = {
        "messages": [HumanMessage(content="Rename Sample")],
        "task_id": "plan-revision",
        "verify_attempts": 0,
        "workflow_mode": "implementation",
        "new_spec_request": True,
    }
    list(graph.stream(request, cfg, stream_mode="updates"))
    list(graph.stream(Command(resume="approve"), cfg, stream_mode="updates"))
    updates = list(
        graph.stream(
            Command(resume="revise: preserve the public API and test the behavior"),
            cfg,
            stream_mode="updates",
        )
    )

    state = graph.get_state(cfg).values
    design_interrupt = next(
        update["__interrupt__"][0].value
        for update in updates
        if "__interrupt__" in update
    )
    assert design_interrupt["kind"] == "design"
    assert state["approved_spec"] is not None
    assert state["plan_approved"] is False
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")


def test_implementation_runs_checker_after_separate_approval(repo, monkeypatch):
    monkeypatch.setattr(
        write_tools,
        "run_project_checks",
        lambda paths: "CHECK PARTIAL: CMake build passed, but CTest registered 0 tests.",
    )
    llm = ScriptedLLM(
        [
            AIMessage(content="Implementation is complete."),
            AIMessage(content="The code is updated."),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "checks-approved"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "checks-approved",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "plan_approved": True,
                "modified_files": {
                    "src/sample.cpp": write_tools.file_digest("src/sample.cpp")
                },
            },
            cfg,
            stream_mode="updates",
        )
    )
    check_interrupt = next(
        update["__interrupt__"][0].value
        for update in updates
        if "__interrupt__" in update
    )
    assert check_interrupt["kind"] == "checks"
    assert "CTest" in check_interrupt["actions"][0]

    list(graph.stream(Command(resume="approve"), cfg, stream_mode="updates"))

    state = graph.get_state(cfg).values
    assert state["check_attempts"] == 1
    assert state["checks_current"] is True
    assert "CHECK PARTIAL" in state["messages"][-1].content


def test_rejected_checker_is_reported_as_not_run(repo, monkeypatch):
    monkeypatch.setattr(
        write_tools,
        "run_project_checks",
        lambda paths: pytest.fail("checks must not run when rejected"),
    )
    llm = ScriptedLLM([AIMessage(content="Implementation is complete.")])
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "checks-rejected"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Update Sample")],
                "task_id": "checks-rejected",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "plan_approved": True,
                "modified_files": {
                    "src/sample.cpp": write_tools.file_digest("src/sample.cpp")
                },
            },
            cfg,
            stream_mode="updates",
        )
    )

    list(graph.stream(Command(resume="do not run"), cfg, stream_mode="updates"))

    final_report = graph.get_state(cfg).values["messages"][-1].content
    assert "CHECK NOT RUN" in final_report


def test_independent_review_precedes_approval_gated_commit(repo):
    target = repo / "src" / "sample.cpp"
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    modified_files = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}
    reviewer = ScriptedLLM([CodeReview(approved=True)])
    agent = ScriptedLLM([AIMessage(content="Implementation is ready.")])
    graph = build_graph(
        MemorySaver(),
        allow_write=True,
        llm=agent,
        reviewer_llm=reviewer,
    )
    cfg = {"configurable": {"thread_id": "review-commit"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "review-commit",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "design": _sample_design().model_dump(),
                "plan_approved": True,
                "modified_files": modified_files,
                "checks_current": True,
                "check_result": "CHECK PASSED: static checks, build, and CTest.",
            },
            cfg,
            stream_mode="updates",
        )
    )

    approval = next(
        update["__interrupt__"][0].value
        for update in updates
        if "__interrupt__" in update
    )
    assert approval["kind"] == "commit"
    assert "class Vehicle" in approval["diff"]
    assert "class Vehicle" in target.read_text(encoding="utf-8")

    list(graph.stream(Command(resume="approve"), cfg, stream_mode="updates"))

    state = graph.get_state(cfg).values
    assert "COMMIT SUCCEEDED:" in state["messages"][-1].content
    committed = write_tools._git(["show", "--pretty=format:", "--name-only", "HEAD"])
    assert committed.stdout.decode().splitlines() == ["src/sample.cpp"]


def test_declined_commit_leaves_changes_uncommitted(repo):
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    modified_files = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}
    reviewer = ScriptedLLM([CodeReview(approved=True)])
    agent = ScriptedLLM([AIMessage(content="Implementation is ready.")])
    graph = build_graph(
        MemorySaver(),
        allow_write=True,
        llm=agent,
        reviewer_llm=reviewer,
    )
    cfg = {"configurable": {"thread_id": "declined-commit"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "declined-commit",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "design": _sample_design().model_dump(),
                "plan_approved": True,
                "modified_files": modified_files,
                "checks_current": True,
                "check_result": "CHECK PASSED: static checks, build, and CTest.",
            },
            cfg,
            stream_mode="updates",
        )
    )

    list(graph.stream(Command(resume="do not commit"), cfg, stream_mode="updates"))

    state = graph.get_state(cfg).values
    assert "COMMIT NOT RUN: human declined" in state["messages"][-1].content
    assert "class Vehicle" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")
    assert "COMMIT SUCCEEDED:" not in state["messages"][-1].content
    assert write_tools._git(["diff", "--cached", "--quiet"]).returncode == 0


def test_review_findings_block_commit_and_return_to_agent(repo):
    target = repo / "src" / "sample.cpp"
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    modified_files = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}
    reviewer = ScriptedLLM(
        [
            CodeReview(
                approved=False,
                findings=[
                    {
                        "severity": "high",
                        "path": "src/sample.cpp",
                        "line": 1,
                        "finding": "The public method declaration was not renamed.",
                    }
                ],
            )
        ]
    )
    agent = ScriptedLLM(
        [
            AIMessage(content="Implementation is ready."),
            AIMessage(content="I will address the review finding next."),
        ]
    )
    graph = build_graph(
        MemorySaver(),
        allow_write=True,
        llm=agent,
        reviewer_llm=reviewer,
    )
    cfg = {"configurable": {"thread_id": "review-block"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "review-block",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "design": _sample_design().model_dump(),
                "plan_approved": True,
                "modified_files": modified_files,
                "checks_current": True,
                "check_result": "CHECK PASSED: static checks, build, and CTest.",
            },
            cfg,
            stream_mode="updates",
        )
    )

    state = graph.get_state(cfg).values
    assert not any(
        update.get("__interrupt__", [{}])[0].value.get("kind") == "commit"
        for update in updates
        if update.get("__interrupt__")
    )
    assert "REVIEW NEEDS CHANGES" in state["messages"][-1].content
    assert "public method declaration" in state["messages"][-1].content
    assert "class Vehicle" in target.read_text(encoding="utf-8")


def test_partial_check_can_be_reviewed_but_never_committed(repo):
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    modified_files = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}
    reviewer = ScriptedLLM([CodeReview(approved=True)])
    agent = ScriptedLLM([AIMessage(content="Implementation is ready.")])
    graph = build_graph(
        MemorySaver(),
        allow_write=True,
        llm=agent,
        reviewer_llm=reviewer,
    )
    cfg = {"configurable": {"thread_id": "partial-check-no-commit"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "partial-check-no-commit",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": _sample_spec().model_dump(),
                "design": _sample_design().model_dump(),
                "plan_approved": True,
                "modified_files": modified_files,
                "checks_current": True,
                "check_result": "CHECK PARTIAL: CTest registered 0 tests.",
            },
            cfg,
            stream_mode="updates",
        )
    )

    state = graph.get_state(cfg).values
    assert not any(
        update.get("__interrupt__")
        for update in updates
    )
    assert "COMMIT BLOCKED" in state["messages"][-1].content
    assert "CHECK PARTIAL" in state["messages"][-1].content


def test_approved_spec_allowlist_blocks_out_of_scope_writes(repo):
    llm = ScriptedLLM(
        [
            _tool_call(
                "edit_file",
                {"path": "src/sample.cpp", "old_str": "Sample", "new_str": "Vehicle"},
            ),
            AIMessage(content="The requested path is outside the approved scope."),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "spec-scope"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "spec-scope",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": {"allowed_paths": ["src/other.cpp"]},
                "plan_approved": True,
            },
            cfg,
            stream_mode="updates",
        )
    )
    updates = list(graph.stream(Command(resume="y"), cfg, stream_mode="updates"))

    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")
    tool_messages = [
        message
        for update in updates
        for message in update.get("tools", {}).get("messages", [])
    ]
    assert any("outside the approved Spec allowlist" in message.content for message in tool_messages)


def test_spec_allowlist_checks_resolved_symlink_target(repo):
    target = repo / "src" / "outside.cpp"
    target.write_text("int outside() {}\n", encoding="utf-8")
    (repo / "src" / "allowed.cpp").symlink_to(target)

    with pytest.raises(ValueError, match="outside the approved Spec allowlist"):
        _require_spec_path(
            "src/allowed.cpp", {"allowed_paths": ["src/allowed.cpp"]}
        )


def test_spec_allowlist_rejects_repo_wide_glob(repo):
    with pytest.raises(ValueError, match="outside the approved Spec allowlist"):
        _require_spec_path("src/sample.cpp", {"allowed_paths": ["**/*.cpp"]})


def test_create_file_requires_spec_path_and_human_approval(repo):
    llm = ScriptedLLM(
        [
            _tool_call(
                "create_file",
                {"path": "src/generated.cpp", "content": "int answer() { return 42; }\n"},
            ),
            AIMessage(content="Created the requested source file."),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "spec-create-file"}}
    updates = list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Add the helper")],
                "task_id": "spec-create-file",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "approved_spec": {"allowed_paths": ["src/generated.cpp"]},
                "plan_approved": True,
            },
            cfg,
            stream_mode="updates",
        )
    )

    assert not (repo / "src" / "generated.cpp").exists()
    assert any("__interrupt__" in update for update in updates)
    list(graph.stream(Command(resume="y"), cfg, stream_mode="updates"))
    assert (repo / "src" / "generated.cpp").read_text(encoding="utf-8") == "int answer() { return 42; }\n"


def test_spec_revision_reenters_grill_without_writes(repo):
    llm = ScriptedLLM(
        [
            GrillDecision(ready=True, spec=_sample_spec()),
            GrillDecision(ready=False, questions=["确认是否允许新增测试？"]),
        ]
    )
    graph = build_graph(MemorySaver(), allow_write=True, llm=llm)
    cfg = {"configurable": {"thread_id": "spec-revision"}}
    list(
        graph.stream(
            {
                "messages": [HumanMessage(content="Rename Sample")],
                "task_id": "spec-revision",
                "verify_attempts": 0,
                "workflow_mode": "implementation",
                "new_spec_request": True,
            },
            cfg,
            stream_mode="updates",
        )
    )
    updates = list(
        graph.stream(
            Command(resume="revise: do not add tests"),
            cfg,
            stream_mode="updates",
        )
    )

    state = graph.get_state(cfg).values
    assert not any("__interrupt__" in update for update in updates)
    assert "确认是否允许新增测试？" in state["messages"][-1].content
    assert "Sample" in (repo / "src" / "sample.cpp").read_text(encoding="utf-8")


def test_chat_history_hides_internal_tool_and_verifier_messages():
    history = _chat_history(
        [
            HumanMessage(content="Analyze the service"),
            AIMessage(
                content="",
                tool_calls=[
                    {"name": "read_file", "args": {"path": "src/sample.cpp"}, "id": "r1"}
                ],
            ),
            ToolMessage(content="source contents", tool_call_id="r1"),
            HumanMessage(content="[evidence-checker] correct the citation"),
            HumanMessage(content="[project-checker]\nCHECK PASSED"),
            HumanMessage(content="[independent-reviewer] Fix null handling"),
            AIMessage(content="Here is the verified result."),
        ]
    )
    assert history == [
        {"role": "user", "content": "Analyze the service"},
        {"role": "assistant", "content": "Here is the verified result."},
    ]


def test_chat_detects_final_commit_approval():
    from chat_app import _pending_approval

    pending = {"kind": "commit", "paths": ["src/sample.cpp"], "diff": "..."}
    snapshot = SimpleNamespace(
        tasks=[
            SimpleNamespace(
                interrupts=[SimpleNamespace(value=pending)]
            )
        ]
    )

    assert _pending_approval(snapshot) == pending
