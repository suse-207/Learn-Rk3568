# Agent handoff

## Purpose and target

`agent/` contains a LangGraph-based code analysis assistant for this repository. The default target
is the repository root (`AGENT_REPO_ROOT=..`), with analysis focused on vsomeip3 / SOME-IP,
`com` bindings, OTA demos, and adjacent diagnostics. `memory/project_context.md` is concise guidance,
not evidence; the source currently on disk is authoritative.

## Run and verify

Use Python 3.10+, install `requirements.txt`, set `OPENAI_API_KEY` in the ignored `.env`, then:

```bash
streamlit run chat_app.py
python main.py --task vsomeip-review -f prompts/vsomeip_review.txt -o report.md -v
python -m pytest -q
```

The Streamlit app is only the user interface; both it and the CLI use the same LangGraph agent
(the harness for model/tool routing, memory, evidence checks, and approvals). Tests use a scripted
local model and `MemorySaver`; they need no API key or network. Chat requests do make model API calls.

## Implemented safeguards

- Read tools resolve every requested path under the configured repository, omit build, VCS,
  virtual-environment, and node_modules trees from source enumeration, and reject `.env` / private-key
  paths so credentials are not sent to the model.
- Write tools are absent in read-only mode. CLI `--allow-write` or the chat UI implementation mode enables them.
  Each request passes grill-me and Spec approval, then a read-only source discovery and architecture/file
  plan approval. Each write is shown as a Diff and requires approval before execution.
- Approved Spec paths are injected into edit/create tools and checked server-side; wildcard patterns
  must be anchored under an explicit top-level directory.
- Edits require exactly one old-text match and save a backup before writing.
- New files can be created only in the approved path scope and only after an approval preview.
- Builds use dedicated `build-agent-debug` / `build-agent-release` directories by default. An optional
  `AGENT_BUILD_CMD` is parsed as argv (no shell) and supports `{mode}` / `{build_type}` substitution.
  Common key / token / password / credential environment variables are withheld from build subprocesses.
- After implementation, the graph requests approval and runs cppcheck on changed C/C++ files, Python
  compileall and Agent pytest on changed Python files, then fixed CMake Debug configure/build + CTest
  checks in `build-agent-debug`. No model-generated shell commands are executed. Zero CTest cases are
  reported as partial and block commit.
- A separate structured Reviewer examines the final Diff. Findings are returned to the implementation
  agent for at most two repair rounds; each repair returns through Diff approval and checks.
- After all checks pass and review approves, a final human approval displays the Diff and commits only
  recorded Agent-authored paths. Writes reject pre-existing changes to their target; commit refuses a
  non-empty Git index and leaves unrelated unstaged work untouched.
- Final analysis reports are checked for valid source paths, line ranges, nearby symbols, and citations
  on each `结论` / `风险点`.

## Deliberate limitations

- Architecture/file plans are generated from a read-only source-discovery pass. The Reviewer has a
  distinct prompt and structured decision; `AGENT_REVIEW_MODEL` can select a separate model, but defaults
  to the same configured model.
- The current repository has no registered top-level CTest cases; automated commit is therefore blocked
  until relevant tests are registered and pass.
- `edit_file` is a unique text replacement tool, not a general patch editor.
- Evidence checks validate citation structure and nearby text; they do not prove that a cited line
  semantically supports the associated claim. A human must still review analysis and changes.
- The repository may contain local edits or untracked work. Do not overwrite or clean unrelated state.
