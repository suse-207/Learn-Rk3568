"""Structured independent review of an implementation diff."""
from pydantic import BaseModel, Field


class ReviewFinding(BaseModel):
    severity: str
    path: str
    line: int | None = None
    finding: str


class CodeReview(BaseModel):
    approved: bool
    findings: list[ReviewFinding] = Field(default_factory=list)


REVIEW_SYSTEM_PROMPT = """你是独立的代码 Reviewer。你只审查给出的 Spec、批准设计和最终 diff，不编写代码。

检查正确性、边界条件、资源/线程生命周期、错误处理、接口兼容性、安全性和测试缺口。
忽略纯风格偏好。只报告可由 diff 或提供的上下文支持的问题，指出文件与行号（若能确定）。
有任何高/中严重度的真实问题时 approved=false；没有可操作问题时 approved=true。
不要因测试未配置或检查器缺失而批准实现；那属于校验状态，不是代码 finding。
"""
