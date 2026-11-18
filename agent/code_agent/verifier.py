"""Evidence checker: every conclusion in the final report must cite real source.

Checks
  1. Each `file:line[-line]` citation points to an existing file and valid line range.
  2. If the citation is followed by a symbol (e.g. `VsomeipBindRuntime::Init`), that symbol's
     short name must appear within +-WINDOW lines of the cited line.
  3. Every "结论" / "风险点" item must contain at least one citation in its block.
"""
import re
from typing import List

from . import config
from .code_tools import _iter_sources, _safe_path

WINDOW = 15
CITE_RX = re.compile(
    r"(?P<file>[\w./\\-]+\.(?:hpp|hh|h|cpp|cc|cxx|c|txt|sh|json|md|py|cmake|ya?ml))"
    r":(?P<start>\d+)(?:-(?P<end>\d+))?"
    r"(?:[`\s,，(（]*(?P<sym>[A-Za-z_~]\w*(?:::~?\w+)+|[A-Za-z_]\w*\(\)))?"
)
# Markdown headings ("## 结论", "### 风险点") must count as claims too, otherwise the
# "every conclusion needs a citation" rule can be bypassed by simply using a heading.
CLAIM_RX = re.compile(r"^\s*#{0,6}\s*[\s>*\-\d.、|]*\**\s*(结论|风险点)")
BLOCK_END_RX = re.compile(
    r"^\s*(#{1,6}\s|[\s>*\-\d.、|]*\**\s*(结论|风险点))"
)


def _resolve(file: str):
    file = file.replace("\\", "/")
    while file.startswith("./"):
        file = file[2:]
    try:
        p = _safe_path(file)
    except ValueError:
        p = None
    if p is not None and p.is_file():
        return p
    basename = file.rsplit("/", 1)[-1]
    hits = [p for p in _iter_sources() if p.name == basename]
    return hits[0] if len(hits) == 1 else None


def check_citations(report: str) -> List[str]:
    problems = []
    cache = {}
    for m in CITE_RX.finditer(report):
        ref = m.group(0).strip()
        p = _resolve(m.group("file"))
        if p is None:
            problems.append("引用的文件不存在: %s" % ref)
            continue
        if p not in cache:
            cache[p] = p.read_text(encoding="utf-8", errors="replace").splitlines()
        lines = cache[p]
        start = int(m.group("start"))
        end = int(m.group("end") or start)
        if not (1 <= start <= end <= len(lines)):
            problems.append("行号越界: %s (文件共 %d 行)" % (ref, len(lines)))
            continue
        sym = m.group("sym")
        if sym:
            short = re.sub(r"\(\)$", "", sym).split("::")[-1]
            lo, hi = max(0, start - 1 - WINDOW), min(len(lines), end + WINDOW)
            if not any(re.search(r"\b%s\b" % re.escape(short), l) for l in lines[lo:hi]):
                problems.append("符号 %s 不在 %s 附近 ±%d 行内" % (sym, ref, WINDOW))
    return problems


HEADING_RX = re.compile(r"^(#{1,6})\s")


def check_uncited_claims(report: str) -> List[str]:
    lines = report.splitlines()
    problems = []
    in_code = False
    for i, line in enumerate(lines):
        if line.strip().startswith("```"):
            in_code = not in_code
        if in_code or not CLAIM_RX.match(line):
            continue
        heading = HEADING_RX.match(line)
        block = [line]
        if heading:
            # A heading such as "## 风险点" speaks for its whole section, not for the single line.
            level = len(heading.group(1))
            for nxt in lines[i + 1:]:
                nested = HEADING_RX.match(nxt)
                if nested and len(nested.group(1)) <= level:
                    break
                block.append(nxt)
            if not any(text.strip() for text in block):
                continue  # empty section heading, nothing was claimed yet
        else:
            for nxt in lines[i + 1:]:
                if BLOCK_END_RX.match(nxt):
                    break
                block.append(nxt)
        if not CITE_RX.search("\n".join(block)):
            problems.append("缺少源码依据: %s" % line.strip()[:80])
    return problems


def verify_report(report: str) -> List[str]:
    has_claim = any(CLAIM_RX.match(l) for l in report.splitlines())
    if not CITE_RX.search(report) and not has_claim and len(report) < 400:
        return []  # short conversational answer, not a report
    problems = check_citations(report) + check_uncited_claims(report)
    if not CITE_RX.search(report) and not has_claim:
        problems.append("较长分析内容缺少源码引用")
    return problems
