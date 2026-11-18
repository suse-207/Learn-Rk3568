"""Structured requirement interview and implementation-spec formatting."""
import json

from pydantic import BaseModel, Field


class TaskSpec(BaseModel):
    goal: str
    included_scope: list[str] = Field(default_factory=list)
    excluded_scope: list[str] = Field(default_factory=list)
    target_areas: list[str] = Field(default_factory=list)
    allowed_paths: list[str] = Field(default_factory=list)
    implementation_steps: list[str] = Field(default_factory=list)
    acceptance_criteria: list[str] = Field(default_factory=list)
    verification: list[str] = Field(default_factory=list)
    assumptions: list[str] = Field(default_factory=list)


class GrillDecision(BaseModel):
    ready: bool
    questions: list[str] = Field(default_factory=list)
    spec: TaskSpec | None = None


class PlannedFile(BaseModel):
    path: str
    change: str
    reason: str
    evidence: list[str] = Field(min_length=1)


class ImplementationDesign(BaseModel):
    architecture: str
    files: list[PlannedFile] = Field(min_length=1)
    verification: list[str] = Field(min_length=1)
    risks: list[str] = Field(default_factory=list)


GRILL_SYSTEM_PROMPT = """你是需求澄清助手，负责在任何代码修改前把用户的实现意图问清楚。

项目上下文：
{project_context}

规则：
- 结合本轮请求和之前对话，只追问会影响实现范围、兼容性或验收的关键问题；每轮最多 5 个，合并重复问题。
- 不要开始分析或修改代码，也不要把默认假设冒充成用户决定。
- 只有当目标、范围（包含和排除）、成功标准、约束和验证方式足以指导实现时才设 ready=true。
- 若尚不清楚，设 ready=false 并给出明确、易回答的问题；不要同时生成最终 Spec。
- ready=true 时给出完整 TaskSpec。目标文件尚未通过源码检查时，把 target_areas 写成模块/目录或“实施前定位”，不要编造文件路径。
- allowed_paths 是写入白名单，只能使用用户明确同意的相对路径或 glob（例如 src/ota/**）；范围未明确时继续追问，不能用 ** 代表整个仓库。
- implementation_steps 是待执行的粗粒度计划；验收标准要可检查。把未确认内容列入 assumptions。
- 用户明确要求直接进行小而明确的修改时，可以在一轮内整理 Spec；仍须等待用户批准。
"""


DESIGN_SYSTEM_PROMPT = """你是资深 C++/嵌入式软件架构师。你必须先通过只读工具检查当前源码，再提出实施设计。

获批需求 Spec：
{approved_spec}

规则：
- 先用 read_file 检查至少一个实际源码文件；结合 search_code/search_symbol 追踪相关实现与调用。
- 架构方案必须说明要复用的现有模块、接口/数据流变化及兼容性，不要臆造仓库中不存在的组件。
- files 只列出确实需要修改/新增的文件，且每个路径必须位于 Spec.allowed_paths 范围内。
- 每个文件都要给出至少一条精确源码证据（仓库相对路径:行号），且证据必须来自只读工具结果。
- verification 必须是可执行的仓库验证方式；不确定的测试先说明现状和缺口，不要声称已存在。
- 不执行写操作，不生成代码。把范围外的必要改动作为风险提出，而不是扩大修改范围。
- 证据不足时继续调用只读工具；不得用猜测补全架构。
"""


def format_spec(spec: TaskSpec | dict) -> str:
    data = spec.model_dump() if isinstance(spec, TaskSpec) else spec
    sections = (
        ("目标", "goal"),
        ("包含范围", "included_scope"),
        ("不包含范围", "excluded_scope"),
        ("目标模块 / 文件范围", "target_areas"),
        ("允许写入路径", "allowed_paths"),
        ("实施步骤", "implementation_steps"),
        ("验收标准", "acceptance_criteria"),
        ("验证方式", "verification"),
        ("待确认假设", "assumptions"),
    )
    lines = []
    for title, key in sections:
        value = data.get(key)
        if not value:
            continue
        lines.append("### %s" % title)
        if isinstance(value, list):
            lines.extend("- %s" % item for item in value)
        else:
            lines.append(str(value))
    return "## 实施 Spec\n\n" + "\n".join(lines)


def format_design(design: ImplementationDesign | dict) -> str:
    data = (
        design.model_dump()
        if isinstance(design, ImplementationDesign)
        else design
    )
    lines = ["## 架构方案", data["architecture"], "", "## 文件计划"]
    lines.extend(
        "- `%s` — %s（%s）" % (item["path"], item["change"], item["reason"])
        for item in data["files"]
    )
    lines.extend(
        "  - 源码依据：%s" % ", ".join(item["evidence"])
        for item in data["files"]
    )
    lines.append("")
    lines.append("## 验证方式")
    lines.extend("- %s" % item for item in data["verification"])
    if data.get("risks"):
        lines.extend(("", "## 风险 / 未覆盖项"))
        lines.extend("- %s" % item for item in data["risks"])
    return "\n".join(lines)


def format_plan(steps: list[str]) -> str:
    return "## 待批准的实施计划\n\n" + "\n".join(
        "%d. %s" % (index, step) for index, step in enumerate(steps, 1)
    )


def spec_as_json(spec: dict) -> str:
    return json.dumps(spec, ensure_ascii=False, indent=2)
