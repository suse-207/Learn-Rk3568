#pragma once

#include <cstdint>
#include <memory>

#include "ara/core/result.h"
#include "isoft/naicpp/evloop.h"

namespace isoft {
namespace naicpp {

class GlobalGeneralEvLoop
{
public:
    GlobalGeneralEvLoop() noexcept = delete;

    enum class Mode : std::uint8_t
    {
        kUserThread = 1,
        kInsideThread
    };

    static ara::core::Result< void > Initialize(Mode const runMode = Mode::kInsideThread,
                                                bool const enableExecutor = true) noexcept;
    static void Deinitialize() noexcept;
    static std::shared_ptr< EvLoop > Get() noexcept;
};

}  // namespace naicpp
}  // namespace isoft
