import json
import os

import pytest

from code_agent import code_tools, config, verifier, write_tools


def test_safe_path_rejects_repository_escape(repo, tmp_path):
    with pytest.raises(ValueError, match="escapes repository root"):
        code_tools._safe_path(str(tmp_path.parent / "outside.cpp"))


def test_sensitive_file_is_neither_readable_nor_listed(repo):
    secret = repo / ".env"
    secret.write_text("OPENAI_API_KEY=do-not-read", encoding="utf-8")
    with pytest.raises(ValueError, match="sensitive file"):
        code_tools.read_file(".env")
    assert ".env" not in code_tools.list_files(".")


def test_read_file_pages_long_files(repo, monkeypatch):
    monkeypatch.setattr(config, "MAX_READ_LINES", 2)
    result = code_tools.read_file("src/sample.cpp")
    assert "1| class Sample {};" in result
    assert "2| void Sample::run() {}" in result
    assert "continue with start_line=3" not in result

    long_file = repo / "src" / "long.cpp"
    long_file.write_text("a\nb\nc\n", encoding="utf-8")
    result = code_tools.read_file("src/long.cpp")
    assert "continue with start_line=3" in result


def test_source_listing_skips_agent_build_output(repo):
    files = code_tools.list_files("src")
    assert "src/sample.cpp" in files
    assert "generated.cpp" not in files


def test_search_symbol_finds_qualified_definition(repo):
    assert "src/sample.cpp:2: void Sample::run()" in code_tools.search_symbol("Sample::run")


def test_verifier_accepts_valid_source_evidence(repo):
    report = "- 结论: Sample::run 存在（`src/sample.cpp:2 Sample::run`）"
    assert verifier.verify_report(report) == []


def test_verifier_accepts_json_contract_citations(repo):
    contract = repo / "src" / "contract.json"
    contract.write_text('{"method": "GetStatus"}\n', encoding="utf-8")
    assert verifier.check_citations(
        "- 结论: 契约包含 GetStatus (`src/contract.json:1`)"
    ) == []


def test_verifier_rejects_invalid_line_and_uncited_claim(repo):
    report = "\n".join(
        [
            "- 结论: Sample::run 存在（`src/sample.cpp:200 Sample::run`）",
            "- 风险点: 这里没有依据",
        ]
    )
    problems = verifier.verify_report(report)
    assert any("行号越界" in problem for problem in problems)
    assert any("缺少源码依据" in problem for problem in problems)


def test_verifier_rejects_long_uncited_analysis():
    assert any("缺少源码引用" in problem for problem in verifier.verify_report("分析内容。" * 100))


def test_verifier_treats_markdown_heading_as_claim(repo):
    # "## 结论" used to bypass the citation rule entirely; the heading form must be checked too.
    report = "## 结论\n\n工厂在没有匹配的 binding 名字时直接返回空指针。\n" + "补充说明。" * 80
    problems = verifier.verify_report(report)
    assert any("缺少源码依据" in problem for problem in problems), problems


def test_verifier_accepts_heading_claim_with_citation(repo):
    report = "## 结论\n\n- `src/sample.cpp:2` 中 `Sample::run` 是空实现。"
    assert verifier.verify_report(report) == []


def test_verifier_heading_section_followed_by_cited_items(repo):
    # "## 风险点" followed by cited bullets is a section title, not an uncited claim.
    report = "## 风险点\n\n- 风险点：`Sample::run` 是空实现（`src/sample.cpp:2`）\n"
    assert verifier.verify_report(report) == []


def test_edit_file_rejects_non_unique_match(repo):
    target = repo / "src" / "sample.cpp"
    target.write_text("replace me\nreplace me\n", encoding="utf-8")
    with pytest.raises(ValueError, match="matched 2 times"):
        write_tools.edit_file("src/sample.cpp", "replace me", "updated")
    assert target.read_text(encoding="utf-8") == "replace me\nreplace me\n"


def test_default_build_uses_separate_cmake_directory(repo, monkeypatch):
    calls = []

    def fake_run(command, **kwargs):
        calls.append((command, kwargs))
        return type("Result", (), {"stdout": b"", "stderr": b"", "returncode": 0})()

    monkeypatch.setattr(config, "BUILD_CMD", "")
    monkeypatch.setattr(write_tools.subprocess, "run", fake_run)
    monkeypatch.setenv("OPENAI_API_KEY", "must-not-reach-build")
    monkeypatch.setenv("PATH", "/usr/bin")
    assert write_tools.build_project("debug").startswith("BUILD OK")
    assert calls[0][0][:4] == ["cmake", "-S", ".", "-B"]
    assert calls[0][0][4] == "build-agent-debug"
    assert calls[1][0] == [
        "cmake",
        "--build",
        "build-agent-debug",
        "--parallel",
        str(os.cpu_count() or 4),
    ]
    assert all("shell" not in kwargs for _, kwargs in calls)
    assert "OPENAI_API_KEY" not in calls[0][1]["env"]
    assert calls[0][1]["env"]["PATH"] == "/usr/bin"


def test_project_checks_build_and_run_registered_ctest_cases(repo, monkeypatch):
    calls = []

    monkeypatch.setattr(write_tools.shutil, "which", lambda _: "/usr/bin/cppcheck")

    def fake_run(command, **kwargs):
        calls.append((command, kwargs))
        if command[0] == "/usr/bin/cppcheck":
            output = b""
        elif command == ["ctest", "-N"]:
            output = b"Total Tests: 2\n"
        else:
            output = b"100% tests passed\n"
        return type("Result", (), {"stdout": output, "stderr": b"", "returncode": 0})()

    monkeypatch.setattr(write_tools.subprocess, "run", fake_run)

    result = write_tools.run_project_checks(["src/sample.cpp"])

    assert result.startswith("CHECK PASSED: static checks, CMake build and CTest")
    assert calls[0][0][:2] == [
        "/usr/bin/cppcheck",
        "--enable=warning,performance,portability",
    ]
    assert [command for command, _ in calls[1:]] == [
        ["cmake", "-S", ".", "-B", "build-agent-debug", "-DCMAKE_BUILD_TYPE=Debug"],
        ["cmake", "--build", "build-agent-debug", "--parallel", str(os.cpu_count() or 4)],
        ["ctest", "-N"],
        ["ctest", "--output-on-failure"],
    ]
    assert all("shell" not in kwargs for _, kwargs in calls)


def test_cppcheck_uses_compilation_database_when_present(repo, monkeypatch):
    (repo / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": str(repo),
                    "file": "src/sample.cpp",
                    "command": "c++ -c src/sample.cpp",
                }
            ]
        ),
        encoding="utf-8",
    )
    calls = []

    monkeypatch.setattr(write_tools.shutil, "which", lambda _: "/usr/bin/cppcheck")

    def fake_run(command, **kwargs):
        calls.append(command)
        output = b"Total Tests: 1\n" if command == ["ctest", "-N"] else b""
        return type("Result", (), {"stdout": output, "stderr": b"", "returncode": 0})()

    monkeypatch.setattr(write_tools.subprocess, "run", fake_run)

    write_tools.run_project_checks(["src/sample.cpp"])

    cppcheck_cmd = calls[0]
    assert any(arg.startswith("--project=") for arg in cppcheck_cmd)
    assert "--file-filter=%s" % (repo / "src/sample.cpp") in cppcheck_cmd
    assert "--std=c++17" not in cppcheck_cmd
    assert "src/sample.cpp" not in cppcheck_cmd


def test_cppcheck_checks_headers_standalone_not_via_project(repo, monkeypatch):
    # Headers are absent from compile_commands.json; a --file-filter that matches nothing makes
    # cppcheck fail with "could not find any files matching the filter".
    (repo / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": str(repo),
                    "file": "src/sample.cpp",
                    "command": "c++ -c src/sample.cpp",
                }
            ]
        ),
        encoding="utf-8",
    )
    calls = []

    monkeypatch.setattr(write_tools.shutil, "which", lambda _: "/usr/bin/cppcheck")

    def fake_run(command, **kwargs):
        calls.append(command)
        output = b"Total Tests: 1\n" if command == ["ctest", "-N"] else b""
        return type("Result", (), {"stdout": output, "stderr": b"", "returncode": 0})()

    monkeypatch.setattr(write_tools.subprocess, "run", fake_run)

    result = write_tools.run_project_checks(["src/sample.h"])

    cppcheck_cmd = calls[0]
    assert not any(arg.startswith("--project=") for arg in cppcheck_cmd)
    assert "--language=c++" in cppcheck_cmd
    assert "--std=c++17" in cppcheck_cmd
    assert "src/sample.h" in cppcheck_cmd
    assert result.startswith("CHECK PASSED:")


def test_project_checks_report_when_no_ctest_cases_are_registered(repo, monkeypatch):
    calls = []

    monkeypatch.setattr(write_tools.shutil, "which", lambda _: "/usr/bin/cppcheck")

    def fake_run(command, **kwargs):
        calls.append(command)
        if command[0] == "/usr/bin/cppcheck":
            output = b""
        elif command == ["ctest", "-N"]:
            output = b"Total Tests: 0\n"
        else:
            output = b""
        return type("Result", (), {"stdout": output, "stderr": b"", "returncode": 0})()

    monkeypatch.setattr(write_tools.subprocess, "run", fake_run)

    result = write_tools.run_project_checks(["src/sample.cpp"])

    assert result.startswith("CHECK PARTIAL:")
    assert "registered 0 tests" in result
    assert calls[-1] == ["ctest", "-N"]


def test_cpp_changes_require_cppcheck_before_build(repo, monkeypatch):
    calls = []
    monkeypatch.setattr(write_tools.shutil, "which", lambda _: None)
    monkeypatch.setattr(
        write_tools.subprocess,
        "run",
        lambda *args, **kwargs: calls.append(args[0]),
    )

    result = write_tools.run_project_checks(["src/sample.cpp"])

    assert result.startswith("CHECK FAILED: cppcheck is required")
    assert calls == []


def test_agent_commit_stages_only_reviewed_files_and_preserves_unrelated_changes(repo):
    unrelated = repo / "unrelated.txt"
    unrelated.write_text("existing user work\n", encoding="utf-8")
    target = repo / "src" / "sample.cpp"
    write_tools.ensure_safe_write_target("src/sample.cpp", {})
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    modified = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}

    result = write_tools.commit_agent_changes(
        ["src/sample.cpp"], modified, "Rename Sample"
    )

    assert result.startswith("COMMIT SUCCEEDED:")
    committed = write_tools._git(["show", "--pretty=format:", "--name-only", "HEAD"])
    assert committed.stdout.decode().splitlines() == ["src/sample.cpp"]
    assert unrelated.read_text(encoding="utf-8") == "existing user work\n"
    status_lines = write_tools._git(["status", "--porcelain"]).stdout.decode().splitlines()
    assert "?? unrelated.txt" in status_lines


def test_agent_commit_refuses_preexisting_staged_changes(repo):
    target = repo / "src" / "sample.cpp"
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    modified = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}
    unrelated = repo / "unrelated.txt"
    unrelated.write_text("staged by user\n", encoding="utf-8")
    subprocess = write_tools._git(["add", "--", "unrelated.txt"])
    assert subprocess.returncode == 0

    result = write_tools.commit_agent_changes(
        ["src/sample.cpp"], modified, "Rename Sample"
    )

    assert "Git index already contains staged changes" in result
    assert target.read_text(encoding="utf-8").startswith("class Vehicle")
    staged = write_tools._git(["diff", "--cached", "--name-only"])
    assert staged.stdout.decode().splitlines() == ["unrelated.txt"]


def test_agent_commit_refuses_file_changed_after_review(repo):
    target = repo / "src" / "sample.cpp"
    write_tools.edit_file("src/sample.cpp", "class Sample", "class Vehicle")
    reviewed = {"src/sample.cpp": write_tools.file_digest("src/sample.cpp")}
    target.write_text(target.read_text(encoding="utf-8") + "// external edit\n", encoding="utf-8")

    result = write_tools.commit_agent_changes(
        ["src/sample.cpp"], reviewed, "Rename Sample"
    )

    assert "file changed after review" in result
    assert write_tools._git(["diff", "--cached", "--quiet"]).returncode == 0


def test_agent_write_refuses_preexisting_target_changes(repo):
    target = repo / "src" / "sample.cpp"
    target.write_text(target.read_text(encoding="utf-8") + "// user change\n", encoding="utf-8")

    with pytest.raises(ValueError, match="pre-existing Git changes"):
        write_tools.ensure_safe_write_target("src/sample.cpp", {})
