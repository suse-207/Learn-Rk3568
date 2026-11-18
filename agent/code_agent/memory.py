"""Three-level memory.

L1 Project Context : hand-written markdown, injected into every system prompt.
L2 Knowledge       : facts the agent learned (topic -> conclusion + evidence), persisted JSON.
L3 Task Memory     : per-task progress / open issues, persisted JSON.
Short-term conversation state lives in the LangGraph checkpointer (see graph.py).
"""
import json
import time
from pathlib import Path
from typing import Dict, List, Optional

from . import config


def _load(path: Path):
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return {}


def _save(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


# ---- Level 1 ----
def project_context() -> str:
    p = config.PROJECT_CONTEXT_FILE
    return p.read_text(encoding="utf-8") if p.exists() else ""


# ---- Level 2 ----
def save_knowledge(topic: str, conclusion: str, evidence: List[str]) -> str:
    """Persist a verified conclusion with evidence such as ["src/com/runtime.cpp:20 Runtime::Init"]."""
    kb = _load(config.KNOWLEDGE_FILE)
    kb[topic] = {"conclusion": conclusion, "evidence": evidence, "updated": time.strftime("%Y-%m-%d %H:%M")}
    _save(config.KNOWLEDGE_FILE, kb)
    return "saved knowledge: %s" % topic


def recall_knowledge(query: str = "") -> str:
    """Return stored knowledge whose topic/conclusion contains query (all if empty)."""
    kb: Dict = _load(config.KNOWLEDGE_FILE)
    q = query.lower()
    items = [
        "## %s\n%s\nevidence: %s" % (k, v["conclusion"], "; ".join(v["evidence"]))
        for k, v in kb.items()
        if not q or q in k.lower() or q in v["conclusion"].lower()
    ]
    return "\n\n".join(items) if items else "(no stored knowledge)"


def knowledge_index() -> str:
    kb = _load(config.KNOWLEDGE_FILE)
    return ", ".join(kb.keys()) if kb else "(empty)"


# ---- Level 3 ----
def update_task(
    task_id: str,
    status: str,
    progress: str,
    open_issues: List[str],
    modified_files: Optional[List[str]] = None,
    build_status: str = "",
) -> str:
    """Record task progress so a later session can resume. status: in_progress | done | blocked.
    modified_files are merged with previously recorded ones; build_status e.g. 'OK' / 'FAILED: ...'."""
    tasks = _load(config.TASK_FILE)
    prev = tasks.get(task_id, {})
    tasks[task_id] = {
        "status": status,
        "progress": progress,
        "open_issues": open_issues,
        "modified_files": sorted(set(prev.get("modified_files", [])) | set(modified_files or [])),
        "build_status": build_status or prev.get("build_status", ""),
        "updated": time.strftime("%Y-%m-%d %H:%M"),
    }
    _save(config.TASK_FILE, tasks)
    return "task %s -> %s" % (task_id, status)


def task_summary(task_id: str) -> str:
    t = _load(config.TASK_FILE).get(task_id)
    if not t:
        return "(new task)"
    return "status=%s\nprogress=%s\nopen_issues=%s\nmodified_files=%s\nbuild_status=%s" % (
        t["status"], t["progress"], t["open_issues"], t.get("modified_files", []), t.get("build_status", "")
    )
