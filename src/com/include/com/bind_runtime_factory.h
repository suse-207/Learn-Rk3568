#pragma once

#include "com/bind_runtime.h"

#include <memory>
#include <string>

namespace com {

std::unique_ptr<BindRuntime> CreateBindRuntime(
    const std::string& binding,
    const std::string& applicationName);

}  // namespace com
