#include "com/bind_runtime_factory.h"

#ifdef COM_WITH_VSOMEIP
#include "com/vsomeip/vsomeip_bind_runtime.h"
#endif

namespace com {

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
