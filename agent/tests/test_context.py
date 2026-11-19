from langchain_core.messages import AIMessage, HumanMessage, ToolMessage

from code_agent import config
from code_agent.graph import _fold_context


class StubModel:
    def __init__(self, reply="摘要：已读 src/sample.cpp:1-20"):
        self.reply = reply
        self.calls = []

    def invoke(self, messages):
        self.calls.append(messages)
        return AIMessage(content=self.reply)


def _messages(count, size=3000):
    out = []
    for index in range(count):
        out.append(HumanMessage(content=("请求 %d " % index) + "x" * size))
        out.append(AIMessage(content="ok %d" % index))
    return out


def test_fold_context_summarises_when_over_budget(monkeypatch):
    monkeypatch.setattr(config, "CONTEXT_BUDGET_CHARS", 5000)
    monkeypatch.setattr(config, "CONTEXT_KEEP_MESSAGES", 4)
    monkeypatch.setattr(config, "CONTEXT_FOLD_CHARS", 20000)
    monkeypatch.setattr(config, "CONTEXT_MIN_FOLD_CHARS", 100)

    model = StubModel()
    state = {"messages": _messages(10), "context_summary": "", "folded_count": 0}

    summary, folded, window = _fold_context(state, model)

    assert model.calls, "the summariser should have been invoked"
    assert summary.startswith("摘要")
    assert folded > 0
    assert len(window) == len(state["messages"]) - folded


def test_fold_context_never_starts_window_with_tool_message(monkeypatch):
    monkeypatch.setattr(config, "CONTEXT_BUDGET_CHARS", 1000)
    monkeypatch.setattr(config, "CONTEXT_KEEP_MESSAGES", 3)
    monkeypatch.setattr(config, "CONTEXT_FOLD_CHARS", 20000)
    monkeypatch.setattr(config, "CONTEXT_MIN_FOLD_CHARS", 10)

    messages = [HumanMessage(content="q")]
    for index in range(6):
        messages.append(
            AIMessage(
                content="",
                tool_calls=[
                    {
                        "name": "read_file",
                        "args": {"path": "src/a.cpp"},
                        "id": "call-%d" % index,
                        "type": "tool_call",
                    }
                ],
            )
        )
        messages.append(
            ToolMessage(content="y" * 2000, tool_call_id="call-%d" % index, name="read_file")
        )

    _summary, folded, window = _fold_context(
        {"messages": messages, "context_summary": "", "folded_count": 0}, StubModel()
    )

    assert folded > 0
    assert not isinstance(window[0], ToolMessage)


def test_fold_context_is_a_noop_below_budget(monkeypatch):
    monkeypatch.setattr(config, "CONTEXT_BUDGET_CHARS", 100000)

    model = StubModel()
    state = {"messages": _messages(3), "context_summary": "", "folded_count": 0}

    summary, folded, window = _fold_context(state, model)

    assert summary == ""
    assert folded == 0
    assert not model.calls
    assert len(window) == 6
