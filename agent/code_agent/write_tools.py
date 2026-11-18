"""Side-effecting tools; execution is gated by explicit graph approvals."""
import difflib
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import PurePosixPath

from . import config
from .code_tools import _rel, _safe_path


def _build_environment():
    secret_markers = ("KEY", "TOKEN", "SECRET", "PASSWORD", "CREDENTIAL", "AUTH")
    return {
        name: value
        for name, value in os.environ.items()
        if not any(marker in name.upper() for marker in secret_markers)
    }


def _check_edit(path: str, old_str: str):
    p = _safe_path(path)
    if not p.is_file():
        raise ValueError("file not found: %s" % path)
    text = p.read_text(encoding="utf-8")
    n = text.count(old_str) if old_str else 0
    if n != 1:
        raise ValueError("old_str must match exactly once in %s (matched %d times)" % (path, n))
    return p, text


def preview_edit(path: str, old_str: str, new_str: str) -> str:
    try:
        p, text = _check_edit(path, old_str)
    except ValueError as e:
        return "INVALID: %s" % e
    new_text = text.replace(old_str, new_str, 1)
    diff = difflib.unified_diff(
        text.splitlines(), new_text.splitlines(), "a/" + _rel(p), "b/" + _rel(p), lineterm="", n=3
    )
    return "\n".join(diff)


def edit_file(path: str, old_str: str, new_str: str) -> str:
    """Replace exactly one occurrence of old_str with new_str in a source file.
    old_str must be unique in the file; include enough surrounding lines. Requires human approval."""
    p, text = _check_edit(path, old_str)
    config.BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    backup = config.BACKUP_DIR / ("%s_%s" % (time.time_ns(), p.name))
    shutil.copy2(str(p), str(backup))
    p.write_text(text.replace(old_str, new_str, 1), encoding="utf-8")
    return "edited %s (backup: %s)" % (_rel(p), backup.name)


def create_file(path: str, content: str) -> str:
    """Create one new source file. Requires human approval in the agent workflow."""
    p = _safe_path(path)
    if p.exists():
        raise ValueError("file already exists: %s" % path)
    if not p.parent.is_dir():
        raise ValueError("parent directory does not exist: %s" % p.parent)
    with p.open("x", encoding="utf-8") as output:
        output.write(content)
    return "created %s" % _rel(p)


def file_digest(path: str) -> str:
    return hashlib.sha256(_safe_path(path).read_bytes()).hexdigest()


def _git(command: list[str], timeout: int = 30):
    env = _build_environment()
    for name in tuple(env):
        if name.startswith("GIT_CONFIG_"):
            env.pop(name)
    return subprocess.run(
        ["git"] + command,
        cwd=str(config.REPO_ROOT),
        capture_output=True,
        timeout=timeout,
        env=env,
    )


def ensure_safe_write_target(path: str, modified_files: dict, creating: bool = False):
    target = _safe_path(path)
    relative = _rel(target)
    if relative in modified_files:
        if not target.is_file() or file_digest(relative) != modified_files[relative]:
            raise ValueError(
                "file changed outside this Agent task since its last approved write: "
                + relative
            )
        return

    status = _git(["status", "--porcelain", "--untracked-files=all", "--", relative])
    if status.returncode != 0:
        raise RuntimeError(
            "could not inspect Git status before writing: "
            + status.stderr.decode("utf-8", errors="replace")
        )
    if status.stdout:
        raise ValueError(
            "refusing to overwrite a file with pre-existing Git changes: " + relative
        )
    if creating:
        if target.exists():
            raise ValueError("file already exists: %s" % relative)
    else:
        tracked = _git(["ls-files", "--error-unmatch", "--", relative])
        if tracked.returncode != 0:
            raise ValueError(
                "refusing to edit a file that is not tracked by Git: " + relative
            )


def collect_changed_diff(paths: list[str], modified_files: dict) -> str:
    diffs = []
    for path in sorted(set(paths)):
        if path not in modified_files or file_digest(path) != modified_files[path]:
            raise ValueError("file changed outside this Agent task: " + path)
        tracked = _git(["ls-files", "--error-unmatch", "--", path])
        if tracked.returncode == 0:
            result = _git(["diff", "--no-ext-diff", "--unified=3", "--", path])
            if result.returncode != 0:
                raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
            diff = result.stdout.decode("utf-8", errors="replace")
        else:
            result = _git(
                ["diff", "--no-index", "--no-ext-diff", "--", "/dev/null", path]
            )
            if result.returncode not in (0, 1):
                raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
            diff = result.stdout.decode("utf-8", errors="replace")
        if diff:
            diffs.append(diff)
    combined = "\n".join(diffs)
    if len(combined) > config.MAX_REVIEW_DIFF:
        raise ValueError(
            "review diff exceeds %d characters; split the implementation into smaller tasks"
            % config.MAX_REVIEW_DIFF
        )
    return combined


def commit_agent_changes(
    paths: list[str], modified_files: dict, message: str
) -> str:
    candidates = sorted(set(paths))
    if not candidates:
        return "COMMIT NOT RUN: no Agent-authored files were recorded."
    for path in candidates:
        safe = PurePosixPath(path)
        if safe.is_absolute() or ".." in safe.parts or path not in modified_files:
            return "COMMIT BLOCKED: invalid or untracked Agent change path: %s" % path
        try:
            if file_digest(path) != modified_files[path]:
                return "COMMIT BLOCKED: file changed after review: %s" % path
        except (OSError, ValueError) as exc:
            return "COMMIT BLOCKED: could not verify %s: %s" % (path, exc)

    staged = _git(["diff", "--cached", "--quiet"])
    if staged.returncode == 1:
        return "COMMIT BLOCKED: the Git index already contains staged changes."
    if staged.returncode != 0:
        return "COMMIT FAILED: could not inspect the Git index."

    for path in candidates:
        status = _git(
            ["status", "--porcelain", "--untracked-files=all", "--", path]
        )
        if status.returncode != 0:
            return "COMMIT FAILED: could not inspect Git status for %s." % path
        entry = status.stdout.decode("utf-8", errors="replace").rstrip("\n")
        if not entry or not (entry.startswith(" M ") or entry.startswith("?? ")):
            return "COMMIT BLOCKED: unexpected Git state for %s: %s" % (
                path,
                entry or "(clean)",
            )

    added = _git(["add", "--"] + candidates)
    if added.returncode != 0:
        return "COMMIT FAILED: git add failed: %s" % added.stderr.decode(
            "utf-8", errors="replace"
        )
    staged_paths = _git(["diff", "--cached", "--name-only", "-z"])
    if staged_paths.returncode != 0:
        return "COMMIT FAILED: could not verify staged paths."
    actual_paths = {
        item.decode("utf-8", errors="surrogateescape")
        for item in staged_paths.stdout.split(b"\0")
        if item
    }
    if actual_paths != set(candidates):
        _git(["reset", "--"] + candidates)
        return "COMMIT BLOCKED: staged paths differ from the reviewed Agent changes."

    commit_message = " ".join(message.split())[:200] or "Implement approved Agent task"
    result = _git(["commit", "-m", commit_message])
    if result.returncode != 0:
        _git(["reset", "--"] + candidates)
        return "COMMIT FAILED: git commit failed: %s" % _truncate_output(
            result.stderr.decode("utf-8", errors="replace")
        )
    revision = _git(["rev-parse", "--short", "HEAD"])
    if revision.returncode != 0:
        return "COMMIT SUCCEEDED, but the commit hash could not be read."
    return "COMMIT SUCCEEDED: %s" % revision.stdout.decode("ascii").strip()


def build_project(mode: str = "debug") -> str:
    """Configure and compile the CMake project. mode: debug | release. Requires human approval."""
    if mode not in ("debug", "release"):
        return "ERROR: mode must be debug or release"
    build_dir = "build-agent-%s" % mode
    build_type = "Debug" if mode == "debug" else "Release"
    if config.BUILD_CMD:
        cmd = [
            part.format(mode=mode, build_type=build_type)
            for part in shlex.split(config.BUILD_CMD)
        ]
        commands = [cmd]
    else:
        commands = [
            ["cmake", "-S", ".", "-B", build_dir, "-DCMAKE_BUILD_TYPE=%s" % build_type],
            ["cmake", "--build", build_dir, "--parallel", str(os.cpu_count() or 4)],
        ]

    output = []
    for cmd in commands:
        try:
            result = subprocess.run(
                cmd, cwd=str(config.REPO_ROOT), capture_output=True,
                timeout=config.BUILD_TIMEOUT, env=_build_environment(),
            )
        except subprocess.TimeoutExpired as exc:
            partial = exc.stdout or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            return "BUILD TIMEOUT after %ds\n%s" % (config.BUILD_TIMEOUT, partial)
        text = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        output.append("$ %s\n%s" % (" ".join(cmd), text))
        if result.returncode != 0:
            combined = "\n".join(output)
            if len(combined) > config.MAX_BUILD_OUTPUT:
                combined = "...(truncated)...\n" + combined[-config.MAX_BUILD_OUTPUT:]
            return "BUILD FAILED (exit=%d)\n%s" % (result.returncode, combined)

    combined = "\n".join(output)
    if len(combined) > config.MAX_BUILD_OUTPUT:
        combined = "...(truncated)...\n" + combined[-config.MAX_BUILD_OUTPUT:]
    return "BUILD OK\n%s" % combined


def run_project_checks(paths: list[str]) -> str:
    """Run static checks, CMake build, CTest, and Agent tests for changed Python code."""
    build_dir = "build-agent-debug"
    output = []
    cpp_paths = [
        path for path in paths
        if PurePosixPath(path).suffix.lower()
        in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
    ]
    python_paths = [
        path for path in paths if PurePosixPath(path).suffix.lower() == ".py"
    ]

    if cpp_paths:
        cppcheck = shutil.which("cppcheck")
        if not cppcheck:
            return "CHECK FAILED: cppcheck is required for C/C++ changes but is not installed."
        project_file = config.REPO_ROOT / "compile_commands.json"
        project_files = set()
        if project_file.is_file():
            try:
                entries = json.loads(project_file.read_text(encoding="utf-8"))
                project_files = {
                    str((config.REPO_ROOT / entry["file"]).resolve())
                    for entry in entries
                    if entry.get("file")
                }
            except (OSError, ValueError, KeyError, TypeError):
                project_files = set()

        # Headers never appear in compile_commands.json, and a --file-filter that matches nothing
        # makes cppcheck exit 1 with "could not find any files matching the filter". Split the
        # changed files: translation units go through the database, the rest are checked standalone.
        in_project = [
            path
            for path in cpp_paths
            if str((config.REPO_ROOT / path).resolve()) in project_files
        ]
        standalone = [path for path in cpp_paths if path not in in_project]

        base = [
            cppcheck,
            "--enable=warning,performance,portability",
            "--error-exitcode=1",
            "--suppress=missingIncludeSystem",
            # Third-party config headers (ara-core/phsl) cannot detect the compiler through
            # cppcheck's preprocessor; it is unrelated to the agent's own change.
            "--suppress=preprocessorErrorDirective",
        ]
        commands = []
        if in_project:
            # cppcheck resolves --file-filter against the project path; with an absolute project
            # file the filters must be absolute too (a relative filter silently matches nothing).
            commands.append(
                base
                + ["--project=%s" % project_file]
                + ["--file-filter=%s" % (config.REPO_ROOT / path) for path in in_project]
            )
        if standalone:
            # .h files are guessed as C by cppcheck; force C++ so a header-only change parses.
            commands.append(base + ["--language=c++", "--std=c++17"] + standalone)

        for cmd in commands:
            try:
                result = subprocess.run(
                    cmd, cwd=str(config.REPO_ROOT), capture_output=True,
                    timeout=config.BUILD_TIMEOUT, env=_build_environment(),
                )
            except subprocess.TimeoutExpired as exc:
                return "CHECK FAILED: cppcheck timed out after %ds\n%s" % (
                    config.BUILD_TIMEOUT,
                    str(exc.stdout or ""),
                )
            text = (result.stdout + result.stderr).decode("utf-8", errors="replace")
            output.append("$ cppcheck %s\n%s" % (" ".join(cmd[1:]), text))
            if result.returncode != 0:
                return "CHECK FAILED: cppcheck (exit=%d)\n%s" % (
                    result.returncode,
                    _truncate_output("\n".join(output)),
                )
    else:
        output.append("STATIC CHECK: cppcheck skipped (no C/C++ files changed).")

    if python_paths:
        compile_cmd = [sys.executable, "-m", "compileall", "-q"] + python_paths
        try:
            result = subprocess.run(
                compile_cmd, cwd=str(config.REPO_ROOT), capture_output=True,
                timeout=config.BUILD_TIMEOUT, env=_build_environment(),
            )
        except subprocess.TimeoutExpired:
            return "CHECK FAILED: Python compileall timed out."
        text = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        if result.returncode != 0:
            return "CHECK FAILED: Python compileall\n%s" % _truncate_output(text)
        output.append("STATIC CHECK PASSED: Python compileall.")
        if any(path.startswith("agent/") for path in python_paths):
            try:
                result = subprocess.run(
                    [sys.executable, "-m", "pytest", "-q", "agent/tests"],
                    cwd=str(config.REPO_ROOT), capture_output=True,
                    timeout=config.BUILD_TIMEOUT, env=_build_environment(),
                )
            except subprocess.TimeoutExpired:
                return "CHECK FAILED: Agent pytest timed out."
            text = (result.stdout + result.stderr).decode("utf-8", errors="replace")
            output.append("$ python -m pytest -q agent/tests\n%s" % text)
            if result.returncode != 0:
                return "CHECK FAILED: Agent pytest (exit=%d)\n%s" % (
                    result.returncode,
                    _truncate_output("\n".join(output)),
                )

    commands = [
        ["cmake", "-S", ".", "-B", build_dir, "-DCMAKE_BUILD_TYPE=Debug"],
        ["cmake", "--build", build_dir, "--parallel", str(os.cpu_count() or 4)],
    ]
    for cmd in commands:
        try:
            result = subprocess.run(
                cmd, cwd=str(config.REPO_ROOT), capture_output=True,
                timeout=config.BUILD_TIMEOUT, env=_build_environment(),
            )
        except subprocess.TimeoutExpired as exc:
            partial = exc.stdout or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            return "CHECK FAILED: CMake build timed out after %ds\n%s" % (
                config.BUILD_TIMEOUT,
                partial,
            )
        text = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        output.append("$ %s\n%s" % (" ".join(cmd), text))
        if result.returncode != 0:
            return "CHECK FAILED: CMake build (exit=%d)\n%s" % (
                result.returncode,
                _truncate_output("\n".join(output)),
            )

    build_path = config.REPO_ROOT / build_dir
    try:
        listing = subprocess.run(
            ["ctest", "-N"],
            cwd=str(build_path),
            capture_output=True,
            timeout=config.BUILD_TIMEOUT,
            env=_build_environment(),
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return "CHECK FAILED: could not list CTest cases: %s" % exc
    listing_text = (listing.stdout + listing.stderr).decode("utf-8", errors="replace")
    if listing.returncode != 0:
        return "CHECK FAILED: ctest -N (exit=%d)\n%s" % (
            listing.returncode,
            _truncate_output(listing_text),
        )
    match = re.search(r"Total Tests:\s*(\d+)", listing_text)
    if match is None:
        return "CHECK FAILED: could not determine the number of registered CTest cases\n%s" % (
            _truncate_output(listing_text)
        )
    if int(match.group(1)) == 0:
        return (
            "CHECK PARTIAL: CMake build passed, but CTest registered 0 tests; "
            "test behavior was not verified.\n"
            + _truncate_output("\n".join(output))
        )

    try:
        result = subprocess.run(
            ["ctest", "--output-on-failure"],
            cwd=str(build_path),
            capture_output=True,
            timeout=config.BUILD_TIMEOUT,
            env=_build_environment(),
        )
    except subprocess.TimeoutExpired as exc:
        partial = exc.stdout or b""
        if isinstance(partial, bytes):
            partial = partial.decode("utf-8", errors="replace")
        return "CHECK FAILED: CTest timed out after %ds\n%s" % (
            config.BUILD_TIMEOUT,
            partial,
        )
    test_output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    if result.returncode != 0:
        return "CHECK FAILED: CTest (exit=%d)\n%s" % (
            result.returncode,
            _truncate_output(test_output),
        )
    return "CHECK PASSED: static checks, CMake build and CTest\n%s" % _truncate_output(
        "\n".join(output) + "\n" + test_output
    )


def _truncate_output(output: str) -> str:
    if len(output) > config.MAX_BUILD_OUTPUT:
        return "...(truncated)...\n" + output[-config.MAX_BUILD_OUTPUT:]
    return output


def describe(tool_call: dict) -> str:
    """Human-readable preview shown at the approval prompt."""
    name, args = tool_call["name"], tool_call["args"]
    if name == "edit_file":
        return "edit_file %s\n%s" % (args.get("path"), preview_edit(**args))
    if name == "create_file":
        return "create_file %s\n%s" % (
            args.get("path"),
            "\n".join(
                difflib.unified_diff(
                    [],
                    args.get("content", "").splitlines(),
                    fromfile="/dev/null",
                    tofile="b/" + args.get("path", ""),
                    lineterm="",
                )
            ),
        )
    if name == "build_project":
        mode = args.get("mode", "debug")
        if config.BUILD_CMD:
            cmd = " ".join(
                part.format(mode=mode, build_type=mode.capitalize())
                for part in shlex.split(config.BUILD_CMD)
            )
        else:
            build_dir = "build-agent-%s" % mode
            build_type = "Debug" if mode == "debug" else "Release"
            cmd = (
                "cmake -S . -B %s -DCMAKE_BUILD_TYPE=%s && "
                "cmake --build %s --parallel %d"
            ) % (build_dir, build_type, build_dir, os.cpu_count() or 4)
        return "build_project: cd %s && %s" % (config.REPO_ROOT, cmd)
    return "%s(%s)" % (name, args)


def describe_project_checks(paths: list[str]) -> str:
    cpp_paths = [
        path
        for path in paths
        if PurePosixPath(path).suffix.lower()
        in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
    ]
    return (
        "Fixed checks for %s: cppcheck %s; Python compileall + agent pytest for changed "
        "Python files; CMake Debug build; CTest. No model-generated command is executed."
    ) % (
        ", ".join(sorted(set(paths))) or "(no files)",
        " ".join(cpp_paths) if cpp_paths else "(no changed C/C++ files)",
    )
