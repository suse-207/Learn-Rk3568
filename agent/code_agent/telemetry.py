"""Per-run structured logging.

One JSON line per task turn (``memory/runs.jsonl``) so cost and behaviour can be reviewed later:
model, mode, duration, graph steps, LLM calls, tool calls and token usage.
"""
import json
import time

from langchain_core.callbacks import BaseCallbackHandler

from . import config


class RunStats(BaseCallbackHandler):
    """Collects step/tool/token counters for a single graph invocation."""

    def __init__(self):
        self.started = time.time()
        self.steps = 0
        self.llm_calls = 0
        self.tool_calls = 0
        self.prompt_tokens = 0
        self.completion_tokens = 0

    def on_llm_end(self, response, **kwargs):
        self.llm_calls += 1
        usage = {}
        try:
            for batch in getattr(response, "generations", None) or []:
                message = getattr(batch[0], "message", None) if batch else None
                usage = getattr(message, "usage_metadata", None) or usage
        except (IndexError, AttributeError, TypeError):
            usage = usage or {}
        if usage:
            self.prompt_tokens += int(usage.get("input_tokens", 0) or 0)
            self.completion_tokens += int(usage.get("output_tokens", 0) or 0)
            return
        llm_output = getattr(response, "llm_output", None) or {}
        legacy = llm_output.get("token_usage") or llm_output.get("usage") or {}
        self.prompt_tokens += int(legacy.get("prompt_tokens", 0) or 0)
        self.completion_tokens += int(legacy.get("completion_tokens", 0) or 0)

    def summary(self):
        return {
            "duration_s": round(time.time() - self.started, 2),
            "steps": self.steps,
            "llm_calls": self.llm_calls,
            "tool_calls": self.tool_calls,
            "prompt_tokens": self.prompt_tokens,
            "completion_tokens": self.completion_tokens,
        }


def record_run(task_id, model, mode, stats, status, report_chars=0):
    entry = {
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "task_id": task_id,
        "model": model,
        "mode": mode,
        "status": status,
        "report_chars": report_chars,
    }
    entry.update(stats.summary())
    try:
        config.RUN_LOG.parent.mkdir(parents=True, exist_ok=True)
        with config.RUN_LOG.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry, ensure_ascii=False) + "\n")
    except OSError:
        pass  # telemetry must never break a run
    return entry
