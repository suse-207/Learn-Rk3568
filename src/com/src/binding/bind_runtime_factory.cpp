#include "com/binding/bind_runtime_factory.h"

#ifdef COM_WITH_VSOMEIP
#include "com/binding/vsomeip/vsomeip_bind_runtime.h"
#endif

namespace com {

// 绑定工厂：根据 binding 名字创建对应的 BindRuntime 实现。
//
// 选择机制是「编译期宏 + 运行时字符串比较」双重条件：
//   1) 编译期：只有定义了 COM_WITH_VSOMEIP 宏（由 src/com/CMakeLists.txt 在找到
//      vsomeip3 后以 PUBLIC 方式注入）时，下面的 vsomeip 分支才会参与编译；
//      未定义该宏时，即使传入 "vsomeip" 也不会命中任何分支。
//   2) 运行时：在宏已定义的前提下，仅当 binding == "vsomeip" 时返回
//      VsomeipBindRuntime(applicationName)。
//
// 未匹配到任何名字时（例如 "dds"、"raw_tcp" 等尚未实现的名字，或未定义
// COM_WITH_VSOMEIP 宏时的 "vsomeip"）返回 nullptr，没有默认回退实现。
// 因此调用方必须判空后再使用返回值，例如：
//   auto createdRuntime = com::CreateBindRuntime("vsomeip", "my-app");
//   if (!createdRuntime) { /* 处理创建失败 */ }
//
// 注意：本工厂只负责构造对象，不做任何初始化；真正的初始化发生在
// Runtime::RegisterBindRuntime 内部调用 bindRuntime->Init() 时。
std::unique_ptr<BindRuntime> CreateBindRuntime(
    const std::string& binding,
    const std::string& applicationName) {
#ifdef COM_WITH_VSOMEIP
    if (binding == "vsomeip") {
        return std::make_unique<
            com::vsomeip_binding::VsomeipBindRuntime>(applicationName);
    }
#endif

    // Future bindings:
    // if (binding == "dds") ...
    // if (binding == "raw_tcp") ...

    return nullptr;
}

}  // namespace com
