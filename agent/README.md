# RK3568 vsomeip Code Analysis Agent

基于 LangGraph 与 OpenAI-compatible Chat Completions 的源码分析 Agent，默认分析当前仓库
`rk3568-automotive-platform`，重点覆盖 vsomeip3 / SOME-IP、通信抽象层和 OTA 示例。

## 能力

- 只读工具：源码文件枚举、分页读取、正则搜索、符号搜索；跳过构建产物并拒绝读取 `.env` / 私钥等敏感文件。
- 三级记忆：项目上下文、主题知识和任务进度；LangGraph SQLite checkpoint 保存短期会话。
- 实现任务走受控链路：需求澄清 → Spec 审批 → 只读源码调查 → 架构/文件计划审批
  → 逐个 Diff 审批后写入。
- Spec 的 `allowed_paths` 会在写工具中强制检查；创建/编辑文件需要人工批准。
- 模型不能自行构建：Diff 批准后，由流程固定的校验步骤统一执行 cppcheck、CMake Debug 构建与 CTest，
  结果回灌给模型用于修复（一次任务只构建一次）。
- 校验按固定顺序执行：cppcheck（变更 C/C++ 文件）、Python compileall/Agent pytest（变更 Python 文件）、
  CMake Debug 构建、CTest；每轮执行前都需批准。缺少 CTest 用例会标记为部分验证。
- 独立 Reviewer 审查最终 Diff；有问题时最多自动修复两轮，每轮仍须重新批准 Diff 并重新校验。
- 校验完整通过且 Reviewer 批准后，才展示最终 Diff 并请求 Git 提交审批；仅提交本 Agent 改动文件。
- 证据校验：报告中的 `file:line` 必须指向仓库中的文件和有效行号，声明符号须在引用附近出现；“结论”与“风险点”必须附源码引用。
- 默认只读；普通分析不会进入 Spec 流程。实现模式先澄清并批准 Spec，之后每次副作用操作都先显示预览并等待人工确认。
- 编译子进程会过滤常见密钥 / 凭据环境变量，避免把 API 凭据交给构建脚本。
- 不需要 API Key 的离线测试覆盖搜索、引用校验、工具调用、验证重试和写入审批流程。

## 安装与配置

需要 Python 3.10+ 和可访问的 OpenAI-compatible API。

```bash
cd agent
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
cp .env.example .env
```

编辑 `.env` 设置 `OPENAI_API_KEY`；如使用兼容服务，可设置 `OPENAI_BASE_URL` 和 `AGENT_MODEL`。
如需不同模型做独立代码审查，可设置 `AGENT_REVIEW_MODEL`（默认与 `AGENT_MODEL` 相同）。
`AGENT_REPO_ROOT` 默认是 `..`，即当前仓库根目录。密钥不要提交到 Git。
变更 C/C++ 文件时需要系统安装 `cppcheck`（例如 Debian/Ubuntu：`sudo apt install cppcheck`）。

## 运行分析

启动本地聊天窗口（启动一次后在浏览器中连续对话）：

```bash
streamlit run chat_app.py
```

Streamlit 会输出本地地址，通常是 `http://localhost:8501`。默认是只读问答；打开侧边栏的
“实现/修改代码（先走 Spec）”后，Agent 会先追问并展示 Spec。批准后，它只读检查源码并提出架构/文件计划；
之后每个 Diff、校验运行、Reviewer 通过后的最终提交仍需单独确认。也可继续使用下面的命令行模式：

```bash
python main.py --task vsomeip-review -f prompts/vsomeip_review.txt -o report.md -v
```

也可以直接提问：

```bash
python main.py --task factory-review "检查绑定工厂与 vsomeip runtime 的初始化和销毁路径"
```

同一个 `--task` 会恢复先前的 checkpoint 和任务记忆。报告会输出到终端；指定 `-o` 时另写入文件。

命令行的 `--allow-write` 同样启用 Spec-first 实现流程：

```bash
python main.py --task fix-example --allow-write "修复某处问题并验证"
```

在终端先后批准 Spec 和架构/文件计划，之后每个 Diff、校验运行、最终提交还要单独确认。输入修改意见会回到
需求澄清或只读设计阶段；取消不会修改代码。`allowed_paths` 白名单在写工具中强制执行。
默认构建分别配置并编译 `build-agent-debug` / `build-agent-release`，不会复用或覆盖已有 build 目录。
自动 Checker 不执行模型生成的 shell 命令；C/C++ Diff 使用 cppcheck，Agent Python Diff 运行语法检查和
`agent/tests`，之后运行固定 CMake Debug 构建和 CTest。CTest 无用例时是部分验证，Git 提交会被阻止。
自动修复最多两轮。写入前会拒绝覆盖目标文件已有的 Git 改动；提交前还会拒绝有已暂存内容的索引，
并且只 stage 本任务记录的文件，保留其他未暂存改动。

## 测试

无需 API Key 或网络调用：

```bash
python -m pytest -q
```

## 项目分析边界

- 根 CMake 的 vsomeip 开关为 `WITH_VSOMEIP`；通信层另有 `COM_WITH_VSOMEIP`。
- 当前绑定工厂实现 vsomeip；DDS / Raw TCP 只是扩展设想，不是已实现传输。
- 实现 Reviewer 使用单独的审查提示和结构化结果；默认复用 `AGENT_MODEL`，需要模型层面更独立时配置
  `AGENT_REVIEW_MODEL`。源码引用验证仍用于最终报告。
- 当前仓库 CTest 注册用例为 0，因此即使构建成功，Checker 仍报告部分验证并禁止自动 Git 提交。
- VehicleStatus 接口由 JSON 契约生成；检查时要区分生成代码、业务实现与 demo/mock。
- DoIP / UDS 是独立诊断链路，不应误认为经 vsomeip 传输。
- 任何资料可能过期；源码和当前构建配置优先于 README、handoff 和历史记忆。

## 模块

| 路径 | 职责 |
| --- | --- |
| `main.py` | CLI、流式输出、审批交互和 checkpoint |
| `chat_app.py` | 本地 Streamlit 聊天窗口 |
| `code_agent/graph.py` | Agent / 工具 / 审批 / 引用校验工作流 |
| `code_agent/spec_workflow.py` | grill-me 结构化决策、Spec 模型和展示 |
| `code_agent/review_workflow.py` | 独立 Diff Reviewer 的结构化审查模型 |
| `code_agent/code_tools.py` | 只读源码工具及仓库路径约束 |
| `code_agent/write_tools.py` | 审批后文件编辑和 CMake 构建 |
| `code_agent/memory.py` | 项目、知识和任务记忆 |
| `memory/project_context.md` | 注入每轮提示的本项目上下文 |
| `prompts/vsomeip_review.txt` | vsomeip / SOME-IP 分析任务模板 |
