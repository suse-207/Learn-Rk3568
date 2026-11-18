"""Read-only source tools. Every path is resolved inside REPO_ROOT (sandbox)."""
import os
import re
from pathlib import Path
from typing import List, Optional

from . import config


_IGNORED_DIRS = {".git", ".venv", "node_modules", "third_party", "3rdparty", "extern"}
_SENSITIVE_NAMES = {
    ".netrc", "credentials", "id_rsa", "id_ed25519", "id_ecdsa",
}
_SENSITIVE_SUFFIXES = {".pem", ".key", ".p12", ".pfx", ".p8"}


def _is_ignored_tree(parts) -> bool:
    return any(
        part in _IGNORED_DIRS or part == "build" or part.startswith("build-")
        for part in parts
    )


def _is_sensitive_file(path: Path) -> bool:
    name = path.name.lower()
    return (
        (name.startswith(".env") and name != ".env.example")
        or name in _SENSITIVE_NAMES
        or path.suffix.lower() in _SENSITIVE_SUFFIXES
    )


def _safe_path(rel: str) -> Path:
    p = (config.REPO_ROOT / rel).resolve()
    if p != config.REPO_ROOT and config.REPO_ROOT not in p.parents:
        raise ValueError("path escapes repository root: %s" % rel)
    if _is_ignored_tree(p.relative_to(config.REPO_ROOT).parts):
        raise ValueError("path is inside an excluded directory: %s" % rel)
    if _is_sensitive_file(p):
        raise ValueError("refusing to access a sensitive file: %s" % rel)
    return p


def _rel(p: Path) -> str:
    return p.relative_to(config.REPO_ROOT).as_posix()


def _iter_sources(sub: str = "."):
    base = _safe_path(sub)
    for current, directories, filenames in os.walk(base):
        current_path = Path(current)
        relative_parts = current_path.relative_to(config.REPO_ROOT).parts
        directories[:] = sorted(
            name for name in directories
            if not _is_ignored_tree(relative_parts + (name,))
        )
        for name in sorted(filenames):
            path = current_path / name
            if (
                path.suffix.lower() in config.SOURCE_EXTS
                and not _is_sensitive_file(path)
            ):
                yield path


def list_files(directory: str = ".") -> str:
    """List source files under a directory (relative to repo root)."""
    files = [_rel(p) for p in _iter_sources(directory)]
    return "\n".join(files) if files else "(no source files)"


def read_file(path: str, start_line: int = 1, end_line: Optional[int] = None) -> str:
    """Read a file with line numbers. Long files are paged; use start_line/end_line."""
    p = _safe_path(path)
    if not p.is_file():
        return "ERROR: file not found: %s" % path
    lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    start = max(1, start_line)
    end = min(len(lines), end_line or start + config.MAX_READ_LINES - 1, start + config.MAX_READ_LINES - 1)
    body = "\n".join("%5d| %s" % (i, lines[i - 1]) for i in range(start, end + 1))
    tail = "" if end >= len(lines) else "\n... (%d lines total, continue with start_line=%d)" % (len(lines), end + 1)
    return "// %s [%d-%d]\n%s%s" % (_rel(p), start, end, body, tail)


def search_code(pattern: str, directory: str = ".", ignore_case: bool = False) -> str:
    """Regex search across source files. Returns file:line: text."""
    try:
        rx = re.compile(pattern, re.IGNORECASE if ignore_case else 0)
    except re.error as e:
        return "ERROR: bad regex: %s" % e
    hits: List[str] = []
    for p in _iter_sources(directory):
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if rx.search(line):
                hits.append("%s:%d: %s" % (_rel(p), i, line.strip()))
                if len(hits) >= config.MAX_SEARCH_HITS:
                    return "\n".join(hits) + "\n... (truncated)"
    return "\n".join(hits) if hits else "(no matches)"


_DEF_PATTERNS = [
    r"^\s*(class|struct|enum(\s+class)?)\s+{name}\b",   # type definition
    r"\b{name}\s*\(",                                   # declaration / definition / call
]


def search_symbol(name: str) -> str:
    """Locate definitions/declarations of a class, function or member (e.g. 'VsomeipBindRuntime', 'Runtime::Init').
    Results are ordered: type definitions, out-of-line definitions (Class::name), header declarations."""
    short = re.escape(name.split("::")[-1])
    owner = name.split("::")[0] if "::" in name else None
    type_rx = re.compile(_DEF_PATTERNS[0].format(name=short))
    func_rx = re.compile(_DEF_PATTERNS[1].format(name=short))
    qual_rx = re.compile(r"\b\w+::%s\s*\(" % short)
    types, defs, decls = [], [], []
    for p in _iter_sources():
        is_header = p.suffix.lower() in (".h", ".hpp", ".hh")
        for i, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            s = line.strip()
            if s.startswith(("//", "*", "/*")):
                continue
            loc = "%s:%d: %s" % (_rel(p), i, s)
            if type_rx.search(line):
                types.append(loc)
            elif qual_rx.search(line) and not s.endswith(";") and (not owner or (owner + "::") in line):
                defs.append(loc)
            elif is_header and func_rx.search(line) and (not owner or p.stem == owner):
                decls.append(loc)
    hits = types + defs + decls
    return "\n".join(hits[: config.MAX_SEARCH_HITS]) if hits else "(symbol not found; try search_code)"
