import json

from code_agent import config, telemetry


def test_record_run_appends_one_json_line(tmp_path, monkeypatch):
    monkeypatch.setattr(config, "RUN_LOG", tmp_path / "runs.jsonl")
    stats = telemetry.RunStats()
    stats.steps = 3
    stats.tool_calls = 2
    stats.llm_calls = 2

    entry = telemetry.record_run("t1", "deepseek-chat", "analysis", stats, "ok", 100)

    lines = (tmp_path / "runs.jsonl").read_text(encoding="utf-8").splitlines()
    assert len(lines) == 1
    data = json.loads(lines[0])
    assert data["task_id"] == "t1"
    assert data["model"] == "deepseek-chat"
    assert data["steps"] == 3
    assert data["tool_calls"] == 2
    assert data["status"] == "ok"
    assert data["report_chars"] == 100
    assert entry["duration_s"] >= 0


def test_record_run_never_raises_when_log_is_unwritable(tmp_path, monkeypatch):
    blocker = tmp_path / "not-a-dir"
    blocker.write_text("x", encoding="utf-8")
    monkeypatch.setattr(config, "RUN_LOG", blocker / "runs.jsonl")
    assert telemetry.record_run("t", "m", "analysis", telemetry.RunStats(), "ok")["task_id"] == "t"
