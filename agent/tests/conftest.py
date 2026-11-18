import sys
import subprocess
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from code_agent import config


@pytest.fixture
def repo(tmp_path, monkeypatch):
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "sample.cpp").write_text(
        "class Sample {};\nvoid Sample::run() {}\n", encoding="utf-8"
    )
    (tmp_path / "build-agent-debug").mkdir()
    (tmp_path / "build-agent-debug" / "generated.cpp").write_text(
        "void generated() {}\n", encoding="utf-8"
    )
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    subprocess.run(["git", "config", "user.name", "Agent Test"], cwd=tmp_path, check=True)
    subprocess.run(
        ["git", "config", "user.email", "agent-test@example.invalid"],
        cwd=tmp_path,
        check=True,
    )
    subprocess.run(["git", "add", "src/sample.cpp"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "test baseline"], cwd=tmp_path, check=True)
    monkeypatch.setattr(config, "REPO_ROOT", tmp_path)
    monkeypatch.setattr(config, "BACKUP_DIR", tmp_path / "memory" / "backups")
    monkeypatch.setattr(config, "PROJECT_CONTEXT_FILE", tmp_path / "project_context.md")
    monkeypatch.setattr(config, "KNOWLEDGE_FILE", tmp_path / "knowledge.json")
    monkeypatch.setattr(config, "TASK_FILE", tmp_path / "tasks.json")
    return tmp_path
