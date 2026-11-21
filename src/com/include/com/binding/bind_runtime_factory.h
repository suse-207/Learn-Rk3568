#pragma once

#include "com/binding/bind_runtime.h"

#include <memory>
#include <string>

namespace com {

// 绑定工厂：根据 binding 名字创建对应的 BindRuntime 实现。
//
// 以下两种情形都会返回 nullptr，没有默认回退实现：
//   1) binding 名字无效：未匹配任何已实现的名字（例如 "dds"、"raw_tcp" 等
//      尚未实现的名字）；
//   2) 对应实现未编译进本次构建：未定义 COM_WITH_VSOMEIP 宏时，即使传入
//      "vsomeip" 也不会命中任何分支（该宏由 src/com/CMakeLists.txt 在找到
//      vsomeip3 后以 PUBLIC 方式注入）。
//
// 因此调用方必须判空后再使用返回值，例如：
//   auto createdRuntime = com::CreateBindRuntime("vsomeip", "my-app");
//   if (!createdRuntime) { /* 处理创建失败 */ }
std::unique_ptr<BindRuntime> CreateBindRuntime(
    const std::string& binding,
    const std::string& applicationName);

}  // namespace com
