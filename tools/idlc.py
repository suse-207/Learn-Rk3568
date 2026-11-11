#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""idlc - 从 YAML 契约生成 com 层 C++ 代码（Python 3.6 兼容）。

用法:
    python3 tools/idlc.py contracts/vehicle_status.yaml generated

产物写入 <out>/<package>/:
    <package>_protocol.h / .cpp          常量、枚举、编解码
    <package>_service.h                  业务抽象接口
    <package>_proxy.h / .cpp             代理
    <package>_service_skeleton.h / .cpp  骨架（含事件注册与通知）

线格式（与现有手写实现逐字节一致）:
    大端；string = u32 长度前缀 + UTF-8；响应首字段 u32 errc；
    仅 errc == kSuccess 时追加载荷。
"""

from __future__ import print_function

import argparse
import os
import sys

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.stderr.write("idlc: 需要 PyYAML，先确认 python3 -c 'import yaml' 可用\n")
    sys.exit(2)

BASE_CPP = {
    "u8": "std::uint8_t",
    "u16": "std::uint16_t",
    "u32": "std::uint32_t",
    "u64": "std::uint64_t",
    "string": "std::string",
}
BASE_WIRE = {"u8": "U8", "u16": "U16", "u32": "U32", "u64": "U64", "string": "String"}


# --------------------------------------------------------------------- 模型


class Field(object):
    __slots__ = ("name", "type")

    def __init__(self, name, type_):
        self.name = name
        self.type = type_


class Method(object):
    def __init__(self, name, id_, request, response):
        self.name = name
        self.id = id_
        self.request = request
        self.response = response


class Event(object):
    def __init__(self, name, id_, group, payload, notify_name):
        self.name = name
        self.id = id_
        self.group = group
        self.payload = payload
        self.notify_name = notify_name


class Contract(object):
    def __init__(self, doc, source):
        self.source = source
        self.package = doc["package"]
        self.service = doc["service"]
        self.service_id = doc["service_id"]
        self.instance_id = doc["instance_id"]
        version = doc.get("version") or {}
        self.major = version.get("major", 1)
        self.minor = version.get("minor", 0)
        self.encoding = doc["encoding"]
        self.logging = bool(doc.get("logging", False))
        self.enums = {}
        for name, spec in (doc.get("enums") or {}).items():
            self.enums[name] = {"type": spec["type"], "values": spec["values"]}
        self.methods = []
        for m in doc.get("methods") or []:
            self.methods.append(Method(
                m["name"], m["id"],
                [Field(f["name"], f["type"]) for f in (m.get("request") or [])],
                [Field(f["name"], f["type"]) for f in (m.get("response") or [])]))
        self.events = []
        for e in doc.get("events") or []:
            self.events.append(Event(
                e["name"], e["id"], e.get("event_group", 0x0001),
                [Field(f["name"], f["type"]) for f in (e.get("payload") or [])],
                e.get("notify_name", "Notify" + e["name"])))

    def known_types(self):
        return set(BASE_CPP.keys()) | set(self.enums.keys())

    def wire_of(self, type_):
        if type_ in self.enums:
            return self.enums[type_]["type"]
        return type_

    def cpp(self, type_):
        return BASE_CPP.get(type_, type_)

    def groups(self):
        out = []
        for e in self.events:
            if e.group not in out:
                out.append(e.group)
        return out


# --------------------------------------------------------------------- 校验


class ContractError(Exception):
    pass


def _need(cond, message):
    if not cond:
        raise ContractError(message)


def _is_ident(text):
    if not text or not isinstance(text, str):
        return False
    if not (text[0].isalpha() or text[0] == "_"):
        return False
    for ch in text[1:]:
        if not (ch.isalnum() or ch == "_"):
            return False
    return True


def _as_int(value, what):
    try:
        return int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError):
        raise ContractError("%s 不是合法整数: %r" % (what, value))


def _validate_fields(contract, fields, where):
    seen = set()
    for f in fields:
        _need(_is_ident(f.name), "%s: 字段名非法: %r" % (where, f.name))
        _need(f.name not in seen, "%s: 字段名重复: %s" % (where, f.name))
        seen.add(f.name)
        _need(f.type in contract.known_types(),
              "%s: 未知类型 %r（字段 %s）" % (where, f.type, f.name))


def validate(contract):
    _need(bool(contract.package), "缺少 package")
    _need(_is_ident(contract.service), "service 名非法: %r" % contract.service)
    _need(_as_int(contract.service_id, "service_id") != 0, "service_id 不能为 0")
    _need(_as_int(contract.instance_id, "instance_id") != 0, "instance_id 不能为 0")

    enc = contract.encoding
    _need(enc.get("endianness") == "big", "只支持 big endian")
    _need(enc.get("string_length") == "u32", "只支持 u32 长度前缀字符串")
    err = enc.get("error_first") or {}
    _need(err.get("type") == "u32", "errc 必须是 u32")
    _need(err.get("name") == "Errc", "errc 枚举名必须是 Errc")
    _need(enc.get("payload_on_success_only") is True,
          "当前实现只支持 payload_on_success_only: true")
    _need("Errc" in contract.enums, "enums 里必须声明 Errc")
    _need(contract.enums["Errc"]["type"] == "u32", "Errc 底层类型必须是 u32")

    for name, spec in contract.enums.items():
        _need(_is_ident(name), "枚举名非法: %r" % name)
        _need(spec["type"] in BASE_CPP and spec["type"] != "string",
              "枚举 %s 的底层类型非法: %r" % (name, spec["type"]))
        _need(bool(spec["values"]), "枚举 %s 没有取值" % name)
        seen = set()
        for key, value in spec["values"].items():
            _need(_is_ident(key), "枚举 %s 的取值名非法: %r" % (name, key))
            num = _as_int(value, "枚举 %s.%s" % (name, key))
            _need(0 <= num <= 0xFFFFFFFF, "枚举 %s.%s 越界" % (name, key))
            _need(num not in seen, "枚举 %s 取值重复: %r" % (name, num))
            seen.add(num)

    _need(bool(contract.methods), "至少要有一个方法")
    names = set()
    ids = set()
    for m in contract.methods:
        _need(_is_ident(m.name), "方法名非法: %r" % m.name)
        _need(m.name not in names, "方法名重复: %s" % m.name)
        names.add(m.name)
        mid = _as_int(m.id, "方法 %s 的 id" % m.name)
        _need(0 < mid <= 0xFFFF, "方法 %s 的 id 越界" % m.name)
        _need(mid not in ids, "方法 id 重复: 0x%04X" % mid)
        ids.add(mid)
        _validate_fields(contract, m.request, "方法 %s 的 request" % m.name)
        _validate_fields(contract, m.response, "方法 %s 的 response" % m.name)

    event_names = set()
    event_ids = set()
    for e in contract.events:
        _need(_is_ident(e.name), "事件名非法: %r" % e.name)
        _need(e.name not in event_names, "事件名重复: %s" % e.name)
        event_names.add(e.name)
        eid = _as_int(e.id, "事件 %s 的 id" % e.name)
        _need(0x8000 <= eid <= 0xFFFF, "事件 %s 的 id 应在 0x8000..0xFFFF" % e.name)
        _need(eid not in event_ids, "事件 id 重复: 0x%04X" % eid)
        event_ids.add(eid)
        _need(bool(e.payload), "事件 %s 的 payload 不能为空" % e.name)
        _validate_fields(contract, e.payload, "事件 %s 的 payload" % e.name)
        _need(_is_ident(e.notify_name), "事件 %s 的 notify_name 非法" % e.name)
    return contract


# --------------------------------------------------------------------- 工具


def param(field, contract, mode):
    """带名字的形参。mode: in | out"""
    t = field.type
    if t == "string":
        return ("std::string& " if mode == "out" else "const std::string& ") + field.name
    if t in contract.enums:
        return (t + "& " if mode == "out" else "const " + t + "& ") + field.name
    return contract.cpp(t) + ("& " if mode == "out" else " ") + field.name


def params(fields, contract, mode):
    return ", ".join(param(f, contract, mode) for f in fields)


def type_expr(field, contract):
    """不带名字的类型，用于 std::function 签名。"""
    t = field.type
    if t == "string":
        return "const std::string&"
    return contract.cpp(t)


def append_line(field, contract, target):
    t = field.type
    if t in contract.enums:
        wire = contract.wire_of(t)
        return "Append%s(%s, static_cast<%s>(%s));" % (
            BASE_WIRE[wire], target, BASE_CPP[wire], field.name)
    return "Append%s(%s, %s);" % (BASE_WIRE[t], target, field.name)


def decode_lines(field, contract, target, indent="    "):
    t = field.type
    out = []
    if t in contract.enums:
        wire = contract.wire_of(t)
        tmp = "wire_" + field.name
        out.append("%s%s %s = 0;" % (indent, BASE_CPP[wire], tmp))
        out.append("%sif (!Read%s(%s, pos, %s)) {" % (indent, BASE_WIRE[wire], target, tmp))
        out.append("%s    return false;" % indent)
        out.append("%s}" % indent)
        out.append("%s%s = static_cast<%s>(%s);" % (indent, field.name, t, tmp))
    else:
        out.append("%sif (!Read%s(%s, pos, %s)) {" % (indent, BASE_WIRE[t], target, field.name))
        out.append("%s    return false;" % indent)
        out.append("%s}" % indent)
    return out


def banner(contract):
    return ("// @generated by tools/idlc.py from contracts/%s — DO NOT EDIT."
            % os.path.basename(contract.source))


def sig_in(fields, contract):
    return params(fields, contract, "in")


def sig_out(fields, contract):
    return params(fields, contract, "out")


# --------------------------------------------------------------------- protocol


def emit_protocol_h(contract):
    pkg = contract.package
    L = [banner(contract), "#pragma once", "", "#include <cstddef>", "#include <cstdint>",
         "#include <string>", "#include <vector>", "", "namespace %s {" % pkg, "",
         "constexpr std::uint16_t kServiceId = 0x%04X;" % _as_int(contract.service_id, "id"),
         "constexpr std::uint16_t kInstanceId = 0x%04X;" % _as_int(contract.instance_id, "id"),
         "constexpr std::uint8_t kMajorVersion = %d;" % contract.major,
         "constexpr std::uint32_t kMinorVersion = %d;" % contract.minor, ""]
    for m in contract.methods:
        L.append("constexpr std::uint16_t kMethod%s = 0x%04X;" % (m.name, _as_int(m.id, "id")))
    if contract.events:
        groups = contract.groups()
        if len(groups) == 1:
            L.append("constexpr std::uint16_t kEventGroupId = 0x%04X;" % groups[0])
        for e in contract.events:
            L.append("constexpr std::uint16_t kEvent%s = 0x%04X;" % (e.name, _as_int(e.id, "id")))
            L.append("constexpr std::uint16_t kEventGroup%s = 0x%04X;" % (e.name, e.group))
    L.append("")

    for name, spec in contract.enums.items():
        L.append("enum class %s : %s {" % (name, BASE_CPP[spec["type"]]))
        for key, value in spec["values"].items():
            L.append("    %s = 0x%02X," % (key, _as_int(value, key)))
        L.extend(["};", ""])

    L.append("void AppendU8(std::vector<std::uint8_t>& out, std::uint8_t value);")
    L.append("void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value);")
    L.append("void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value);")
    L.append("void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value);")
    L.append("void AppendString(std::vector<std::uint8_t>& out, const std::string& value);")
    L.append("bool ReadU8(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint8_t& value);")
    L.append("bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint16_t& value);")
    L.append("bool ReadU32(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint32_t& value);")
    L.append("bool ReadU64(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint64_t& value);")
    L.append("bool ReadString(const std::vector<std::uint8_t>& in, std::size_t& pos, std::string& value);")
    L.append("")

    for m in contract.methods:
        L.append("void Encode%sRequest(std::vector<std::uint8_t>& out%s);" % (
            m.name, (", " + sig_in(m.request, contract)) if m.request else ""))
        L.append("bool Decode%sRequest(const std::vector<std::uint8_t>& in, std::size_t& pos%s);" % (
            m.name, (", " + sig_out(m.request, contract)) if m.request else ""))
        L.append("void Encode%sResponse(std::vector<std::uint8_t>& out, const Errc& errc%s);" % (
            m.name, (", " + sig_in(m.response, contract)) if m.response else ""))
        L.append("bool Decode%sResponse(const std::vector<std::uint8_t>& in, std::size_t& pos, Errc& errc%s);" % (
            m.name, (", " + sig_out(m.response, contract)) if m.response else ""))
        L.append("")

    for e in contract.events:
        L.append("void Encode%sEvent(std::vector<std::uint8_t>& out%s);" % (
            e.name, (", " + sig_in(e.payload, contract)) if e.payload else ""))
        L.append("bool Decode%sEvent(const std::vector<std::uint8_t>& in, std::size_t& pos%s);" % (
            e.name, (", " + sig_out(e.payload, contract)) if e.payload else ""))
        L.append("")
    L.extend(["}  // namespace %s" % pkg, ""])
    return "\n".join(L)


def emit_protocol_cpp(contract):
    pkg = contract.package
    L = [banner(contract), '#include "%s/%s_protocol.h"' % (pkg, pkg), "",
         "namespace %s {" % pkg, ""]
    L.extend([
        "void AppendU8(std::vector<std::uint8_t>& out, std::uint8_t value) {",
        "    out.push_back(value);",
        "}",
        "",
        "void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {",
        "    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));",
        "    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));",
        "}",
        "",
        "void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {",
        "    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));",
        "    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));",
        "    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));",
        "    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));",
        "}",
        "",
        "void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {",
        "    for (int shift = 56; shift >= 0; shift -= 8) {",
        "        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));",
        "    }",
        "}",
        "",
        "void AppendString(std::vector<std::uint8_t>& out, const std::string& value) {",
        "    AppendU32(out, static_cast<std::uint32_t>(value.size()));",
        "    out.insert(out.end(), value.begin(), value.end());",
        "}",
        "",
        "bool ReadU8(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint8_t& value) {",
        "    if (pos + 1 > in.size()) {",
        "        return false;",
        "    }",
        "    value = in[pos];",
        "    ++pos;",
        "    return true;",
        "}",
        "",
        "bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint16_t& value) {",
        "    if (pos + 2 > in.size()) {",
        "        return false;",
        "    }",
        "    value = static_cast<std::uint16_t>((in[pos] << 8) | in[pos + 1]);",
        "    pos += 2;",
        "    return true;",
        "}",
        "",
        "bool ReadU32(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint32_t& value) {",
        "    if (pos + 4 > in.size()) {",
        "        return false;",
        "    }",
        "    value = (static_cast<std::uint32_t>(in[pos]) << 24) |",
        "            (static_cast<std::uint32_t>(in[pos + 1]) << 16) |",
        "            (static_cast<std::uint32_t>(in[pos + 2]) << 8) |",
        "            static_cast<std::uint32_t>(in[pos + 3]);",
        "    pos += 4;",
        "    return true;",
        "}",
        "",
        "bool ReadU64(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint64_t& value) {",
        "    if (pos + 8 > in.size()) {",
        "        return false;",
        "    }",
        "    value = 0;",
        "    for (int index = 0; index < 8; ++index) {",
        "        value = (value << 8) | in[pos + static_cast<std::size_t>(index)];",
        "    }",
        "    pos += 8;",
        "    return true;",
        "}",
        "",
        "bool ReadString(const std::vector<std::uint8_t>& in, std::size_t& pos, std::string& value) {",
        "    std::uint32_t length = 0;",
        "    if (!ReadU32(in, pos, length) || pos + length > in.size()) {",
        "        return false;",
        "    }",
        "    value.assign(in.begin() + static_cast<std::ptrdiff_t>(pos),",
        "                 in.begin() + static_cast<std::ptrdiff_t>(pos + length));",
        "    pos += length;",
        "    return true;",
        "}",
        "",
    ])

    for m in contract.methods:
        L.append("void Encode%sRequest(std::vector<std::uint8_t>& out%s) {" % (
            m.name, (", " + sig_in(m.request, contract)) if m.request else ""))
        if m.request:
            for f in m.request:
                L.append("    " + append_line(f, contract, "out"))
        else:
            L.append("    (void)out;")
        L.extend(["}", ""])

        L.append("bool Decode%sRequest(const std::vector<std::uint8_t>& in, std::size_t& pos%s) {" % (
            m.name, (", " + sig_out(m.request, contract)) if m.request else ""))
        if not m.request:
            L.extend(["    (void)in;", "    (void)pos;"])
        for f in m.request:
            L.extend(decode_lines(f, contract, "in"))
        L.extend(["    return true;", "}", ""])

        L.append("void Encode%sResponse(std::vector<std::uint8_t>& out, const Errc& errc%s) {" % (
            m.name, (", " + sig_in(m.response, contract)) if m.response else ""))
        L.append("    AppendU32(out, static_cast<std::uint32_t>(errc));")
        L.extend(["    if (errc != Errc::kSuccess) {", "        return;", "    }"])
        for f in m.response:
            L.append("    " + append_line(f, contract, "out"))
        if not m.response:
            L.append("    (void)out;")
        L.extend(["}", ""])

        L.append("bool Decode%sResponse(const std::vector<std::uint8_t>& in, std::size_t& pos, Errc& errc%s) {" % (
            m.name, (", " + sig_out(m.response, contract)) if m.response else ""))
        L.extend(["    std::uint32_t raw_errc = 0;",
                  "    if (!ReadU32(in, pos, raw_errc)) {",
                  "        return false;",
                  "    }",
                  "    errc = static_cast<Errc>(raw_errc);",
                  "    if (errc != Errc::kSuccess) {",
                  "        return true;",
                  "    }"])
        for f in m.response:
            L.extend(decode_lines(f, contract, "in"))
        L.extend(["    return true;", "}", ""])

    for e in contract.events:
        L.append("void Encode%sEvent(std::vector<std::uint8_t>& out%s) {" % (
            e.name, (", " + sig_in(e.payload, contract)) if e.payload else ""))
        for f in e.payload:
            L.append("    " + append_line(f, contract, "out"))
        L.extend(["}", ""])
        L.append("bool Decode%sEvent(const std::vector<std::uint8_t>& in, std::size_t& pos%s) {" % (
            e.name, (", " + sig_out(e.payload, contract)) if e.payload else ""))
        for f in e.payload:
            L.extend(decode_lines(f, contract, "in"))
        L.extend(["    return true;", "}", ""])

    L.extend(["}", ""])
    L[-2] = "}  // namespace %s" % pkg
    return "\n".join(L)


# --------------------------------------------------------------------- service


def join_sig(request, response, contract, tail):
    parts = []
    if request:
        parts.append(sig_in(request, contract))
    if response:
        parts.append(sig_out(response, contract))
    return ", ".join(parts) + tail


def emit_service_h(contract):
    pkg = contract.package
    L = [banner(contract), "#pragma once", "", "#include <cstdint>", "#include <string>", "",
         '#include "%s/%s_protocol.h"' % (pkg, pkg), "", "namespace %s {" % pkg, "",
         "class %sService {" % contract.service, "public:",
         "    virtual ~%sService() = default;" % contract.service, ""]
    for m in contract.methods:
        L.append("    virtual Errc %s(%s) = 0;" % (
            m.name, join_sig(m.request, m.response, contract, "")))
        L.append("")
    L.extend(["};", "", "}  // namespace %s" % pkg, ""])
    return "\n".join(L)


# --------------------------------------------------------------------- proxy


def emit_proxy_h(contract):
    pkg = contract.package
    L = [banner(contract), "#pragma once", "", "#include <cstdint>", "#include <functional>",
         "#include <string>", "", '#include "%s/%s_protocol.h"' % (pkg, pkg), "",
         "namespace com {", "namespace proxy {", "class BindHandle;", "}", "}", "",
         "namespace %s {" % pkg, "", "class %sProxy {" % contract.service, "public:",
         "    explicit %sProxy(com::proxy::BindHandle& handle);" % contract.service, ""]
    for m in contract.methods:
        L.append("    Errc %s(%s);" % (m.name, join_sig(m.request, m.response, contract, "")))
        L.append("")
    for e in contract.events:
        L.append("    void Subscribe%s(std::function<void(%s)> handler);" % (
            e.name, ", ".join(type_expr(f, contract) for f in e.payload)))
        L.append("")
    L.extend(["private:", "    com::proxy::BindHandle& handle_;", "};", "",
              "}  // namespace %s" % pkg, ""])
    return "\n".join(L)


def emit_proxy_cpp(contract):
    pkg = contract.package
    L = [banner(contract), '#include "%s/%s_proxy.h"' % (pkg, pkg), "", "#include <chrono>", "",
         '#include "com/proxy.h"', "", "namespace %s {" % pkg, "",
         "%sProxy::%sProxy(com::proxy::BindHandle& handle)" % (contract.service, contract.service),
         "    : handle_(handle) {}", ""]
    for m in contract.methods:
        L.append("Errc %sProxy::%s(%s) {" % (
            contract.service, m.name, join_sig(m.request, m.response, contract, "")))
        L.append("    std::vector<std::uint8_t> request;")
        L.append("    Encode%sRequest(request%s);" % (
            m.name, (", " + ", ".join(f.name for f in m.request)) if m.request else ""))
        L.extend(["    auto result = handle_.SendRequest(",
                  "        kMethod%s, request, std::chrono::seconds(5));" % m.name,
                  "    if (!result) {",
                  "        return Errc::kInternalError;",
                  "    }", "",
                  "    const std::vector<std::uint8_t>& data = result.Value();",
                  "    std::size_t pos = 0;",
                  "    Errc errc = Errc::kInternalError;"])
        L.append("    if (!Decode%sResponse(data, pos, errc%s)) {" % (
            m.name, (", " + ", ".join(f.name for f in m.response)) if m.response else ""))
        L.extend(["        return Errc::kInternalError;", "    }", "    return errc;", "}", ""])

    for e in contract.events:
        L.append("void %sProxy::Subscribe%s(std::function<void(%s)> handler) {" % (
            contract.service, e.name, ", ".join(type_expr(f, contract) for f in e.payload)))
        L.extend(["    handle_.SubscribeEvent(",
                  "        kEvent%s, kEventGroup%s," % (e.name, e.name),
                  "        [handler](const std::vector<std::uint8_t>& data) {",
                  "            std::size_t pos = 0;"])
        for f in e.payload:
            L.append("            %s local_%s{};" % (contract.cpp(f.type), f.name))
        L.append("            if (Decode%sEvent(data, pos%s)) {" % (
            e.name, (", " + ", ".join("local_" + f.name for f in e.payload)) if e.payload else ""))
        L.append("                handler(%s);" % ", ".join("local_" + f.name for f in e.payload))
        L.extend(["            }", "        });", "}", ""])
    L.extend(["}", ""])
    L[-2] = "}  // namespace %s" % pkg
    return "\n".join(L)


# --------------------------------------------------------------------- skeleton


def emit_skeleton_h(contract):
    pkg = contract.package
    L = [banner(contract), "#pragma once", "", "#include <cstdint>", "#include <string>",
         "#include <vector>", "", '#include "%s/%s_protocol.h"' % (pkg, pkg),
         '#include "%s/%s_service.h"' % (pkg, pkg), "",
         "namespace com {", "namespace skeleton {", "class BindSkeleton;", "}", "}", "",
         "namespace %s {" % pkg, "", "class %sServiceSkeleton {" % contract.service, "public:",
         "    explicit %sServiceSkeleton(%sService& service);" % (contract.service, contract.service),
         "", "    void Register(com::skeleton::BindSkeleton& bind);"]
    if contract.events:
        L.append("    void RegisterEvent(com::skeleton::BindSkeleton& bind);")
        for e in contract.events:
            L.append("    void %s(%s);" % (e.notify_name, sig_in(e.payload, contract)))
    L.extend(["", "private:", "    %sService& service_;" % contract.service])
    if contract.events:
        L.append("    std::vector<com::skeleton::BindSkeleton*> binds_;")
    L.extend(["};", "", "}  // namespace %s" % pkg, ""])
    return "\n".join(L)


def log_expr(field, contract):
    if field.type in contract.enums:
        return "static_cast<%s>(%s)" % (BASE_CPP[contract.wire_of(field.type)], field.name)
    return field.name


def emit_skeleton_cpp(contract):
    pkg = contract.package
    includes = ["#include <set>"]
    if contract.logging:
        includes.insert(0, "#include <iostream>")
    L = [banner(contract), '#include "%s/%s_service_skeleton.h"' % (pkg, pkg), ""]
    L.extend(includes)
    L.extend(["", '#include "com/skeleton.h"', "", "namespace %s {" % pkg, ""])
    if contract.logging:
        L.extend([
            "namespace {", "",
            "// payload 小写十六进制输出：板端排查时唯一能看到线格式的地方。",
            "void DumpBytes(const char* tag, const std::vector<std::uint8_t>& data) {",
            '    static const char kHex[] = "0123456789abcdef";',
            '    std::cout << tag << " len=" << data.size() << "B |";',
            "    for (std::size_t i = 0; i < data.size(); ++i) {",
            "        const std::uint8_t byte = data[i];",
            "        std::cout << ' ' << kHex[byte >> 4] << kHex[byte & 0x0Fu];",
            "    }",
            "    std::cout << std::endl;",
            "}", "",
            "}  // namespace", ""])
    L.extend([
         "%sServiceSkeleton::%sServiceSkeleton(%sService& service)" % (
             contract.service, contract.service, contract.service),
         "    : service_(service) {}", "",
         "void %sServiceSkeleton::Register(com::skeleton::BindSkeleton& bind) {" % contract.service])
    if contract.events:
        L.append("    binds_.push_back(&bind);")
        L.append("")
    for m in contract.methods:
        L.extend(["    bind.RegisterMethodHandler(",
                  "        kMethod%s," % m.name,
                  "        [this](const std::vector<std::uint8_t>& request,",
                  "               std::vector<std::uint8_t>& response) {"])
        if contract.logging:
            L.append('            std::cout << "[someip] <- REQUEST %s(0x%04X)" << std::endl;'
                     % (m.name, _as_int(m.id, "id")))
            L.append('            DumpBytes("[someip]    req", request);')
        for f in m.request:
            L.append("            %s %s{};" % (contract.cpp(f.type), f.name))
        for f in m.response:
            L.append("            %s %s{};" % (contract.cpp(f.type), f.name))
        L.append("            std::size_t pos = 0;")
        if m.request:
            L.append("            if (!Decode%sRequest(request, pos%s)) {" % (
                m.name, (", " + ", ".join(f.name for f in m.request)) if m.request else ""))
            if contract.logging:
                L.append('                std::cout << "[someip] %s: bad payload" << std::endl;'
                         % m.name)
            L.append("                Encode%sResponse(response, Errc::kInvalidArgument%s);" % (
                m.name, (", " + ", ".join(f.name for f in m.response)) if m.response else ""))
            L.extend(["                return;", "            }"])
        else:
            L.extend(["            (void)request;", "            (void)pos;"])
        call_args = [f.name for f in m.request] + [f.name for f in m.response]
        L.append("            const Errc errc = service_.%s(%s);" % (m.name, ", ".join(call_args)))
        L.append("            Encode%sResponse(response, errc%s);" % (
            m.name, (", " + ", ".join(f.name for f in m.response)) if m.response else ""))
        if contract.logging:
            L.append('            std::cout << "[someip] -> RESPONSE %s errc="' % m.name)
            extra = "".join(' << " %s=" << %s' % (f.name, log_expr(f, contract))
                            for f in m.response)
            L.append("                      << static_cast<std::uint32_t>(errc)%s" % extra)
            L.append("                      << std::endl;")
            L.append('            DumpBytes("[someip]    rsp", response);')
        L.extend(["        });", ""])
    L.extend(["}", ""])

    if contract.events:
        L.append("void %sServiceSkeleton::RegisterEvent(com::skeleton::BindSkeleton& bind) {" % contract.service)
        L.append("    std::set<com::EventGroupIdentifier> groups{kEventGroupId};")
        for e in contract.events:
            L.append("    bind.OfferEvent(kEvent%s, groups, com::EventType::kEvent);" % e.name)
            if contract.logging:
                L.append('    std::cout << "[someip] event 0x" << std::hex << kEvent%s' % e.name)
                L.append('              << " group 0x" << kEventGroup%s << std::dec << std::endl;'
                         % e.name)
        L.extend(["}", ""])
        for e in contract.events:
            L.append("void %sServiceSkeleton::%s(%s) {" % (
                contract.service, e.notify_name, sig_in(e.payload, contract)))
            L.append("    std::vector<std::uint8_t> data;")
            L.append("    Encode%sEvent(data%s);" % (
                e.name, (", " + ", ".join(f.name for f in e.payload)) if e.payload else ""))
            if contract.logging:
                parts = "".join(' << " %s=" << %s' % (f.name, log_expr(f, contract))
                                for f in e.payload)
                L.append('    std::cout << "[someip] -> NOTIFY %s"' % e.name + parts +
                         " << std::endl;")
            L.append("    for (auto* bind : binds_) {")
            L.append("        bind->SendEvent(kEvent%s, data);" % e.name)
            L.extend(["    }", "}", ""])
    L.extend(["}", ""])
    L[-2] = "}  // namespace %s" % pkg
    return "\n".join(L)


# --------------------------------------------------------------------- 入口


def build_files(contract):
    pkg = contract.package
    return [
        ("%s_protocol.h" % pkg, emit_protocol_h(contract)),
        ("%s_protocol.cpp" % pkg, emit_protocol_cpp(contract)),
        ("%s_service.h" % pkg, emit_service_h(contract)),
        ("%s_proxy.h" % pkg, emit_proxy_h(contract)),
        ("%s_proxy.cpp" % pkg, emit_proxy_cpp(contract)),
        ("%s_service_skeleton.h" % pkg, emit_skeleton_h(contract)),
        ("%s_service_skeleton.cpp" % pkg, emit_skeleton_cpp(contract)),
    ]


def write_files(files, target_dir):
    if not os.path.isdir(target_dir):
        os.makedirs(target_dir)
    written = []
    for name, content in files:
        path = os.path.join(target_dir, name)
        data = content.encode("utf-8")
        if os.path.isfile(path):
            with open(path, "rb") as fh:
                if fh.read() == data:
                    continue
        with open(path, "wb") as fh:
            fh.write(data)
        written.append(path)
    return written


def main(argv):
    parser = argparse.ArgumentParser(description="从 YAML 契约生成 com 层 C++ 代码")
    parser.add_argument("contract", help="契约 YAML 路径")
    parser.add_argument("out", help="输出根目录，产物写入 <out>/<package>/")
    args = parser.parse_args(argv)

    with open(args.contract, "rb") as fh:
        doc = yaml.safe_load(fh.read().decode("utf-8"))
    try:
        contract = validate(Contract(doc, args.contract))
    except ContractError as exc:
        sys.stderr.write("idlc: 契约错误: %s\n" % exc)
        return 1

    files = build_files(contract)
    target = os.path.join(args.out, contract.package)
    written = write_files(files, target)
    print("idlc: %s -> %s (%d 个文件，%d 个有变化)" % (
        args.contract, target, len(files), len(written)))
    for path in written:
        print("  writ %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
