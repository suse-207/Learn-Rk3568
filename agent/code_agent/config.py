import os
from pathlib import Path

try:
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
except ImportError:
    pass

AGENT_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = (AGENT_DIR / os.getenv("AGENT_REPO_ROOT", "..")).resolve()
MODEL = os.getenv("AGENT_MODEL", "gpt-4o")
REVIEW_MODEL = os.getenv("AGENT_REVIEW_MODEL", MODEL)
# How structured output is obtained (Spec/design/review schemas). "json_schema" only works on
# providers that implement OpenAI's response_format=json_schema; DeepSeek and most compatible
# endpoints support tool calling instead, so function_calling is the portable default.
STRUCTURED_METHOD = os.getenv("AGENT_STRUCTURED_METHOD", "function_calling")

MEMORY_DIR = AGENT_DIR / "memory"
PROJECT_CONTEXT_FILE = MEMORY_DIR / "project_context.md"   # Level 1
KNOWLEDGE_FILE = MEMORY_DIR / "knowledge.json"             # Level 2
TASK_FILE = MEMORY_DIR / "tasks.json"                      # Level 3
CHECKPOINT_DB = MEMORY_DIR / "checkpoints.sqlite"          # LangGraph short-term state
RUN_LOG = MEMORY_DIR / "runs.jsonl"                        # one JSON line per task turn

SOURCE_EXTS = {
    ".h", ".hpp", ".hh", ".c", ".cc", ".cpp", ".cxx",
    ".txt", ".cmake", ".sh", ".py", ".json", ".md",
}
BACKUP_DIR = MEMORY_DIR / "backups"                        # copies taken before edit_file

# Optional argv-style override. Without it, the agent configures and builds this CMake project
# in a dedicated directory so it never overwrites a developer's existing build tree.
BUILD_CMD = os.getenv("AGENT_BUILD_CMD", "")
BUILD_TIMEOUT = int(os.getenv("AGENT_BUILD_TIMEOUT", "600"))
MAX_BUILD_OUTPUT = 6000
MAX_VERIFY_RETRIES = 2
MAX_DESIGN_RETRIES = 2
MAX_REVIEW_RETRIES = 2
MAX_REPAIR_ROUNDS = 2
MAX_CHECK_FAILURES = 3
MAX_REVIEW_DIFF = 40000

MAX_READ_LINES = 150
MAX_SEARCH_HITS = 80
# One broad question can legitimately need 40+ tool calls (agent+tool pairs); 60 was too low and
# aborted the whole task with GraphRecursionError. Override with AGENT_RECURSION_LIMIT if needed.
RECURSION_LIMIT = int(os.getenv("AGENT_RECURSION_LIMIT", "200"))
