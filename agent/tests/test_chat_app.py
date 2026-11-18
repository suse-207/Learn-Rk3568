from pathlib import Path

from streamlit.testing.v1 import AppTest

from chat_app import _load_graph


def test_chat_window_explains_missing_api_key(repo, monkeypatch):
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    app_path = Path(__file__).resolve().parents[1] / "chat_app.py"

    app = AppTest.from_file(str(app_path)).run(timeout=20)

    assert not app.exception
    assert any("OPENAI_API_KEY" in item.value for item in app.error)


def test_chat_window_renders_without_making_model_request(repo, monkeypatch):
    monkeypatch.setenv("OPENAI_API_KEY", "local-test-key")
    monkeypatch.setenv("AGENT_CHECKPOINT_DB", str(repo / "agent-memory" / "state.sqlite"))
    app_path = Path(__file__).resolve().parents[1] / "chat_app.py"

    try:
        app = AppTest.from_file(str(app_path)).run(timeout=20)
    finally:
        _load_graph.clear()

    assert not app.exception
    assert app.title[0].value == "RK3568 代码助手"
    assert len(app.chat_input) == 1
