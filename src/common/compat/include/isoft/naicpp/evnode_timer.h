#pragma once

#include <memory>

#include "isoft/naicpp/evloop.h"

namespace isoft {
namespace naicpp {

// EvNodeTimer lives in evloop.h in this port; this header exists so callers
// that include only evnode_timer.h still compile unchanged.

}  // namespace naicpp
}  // namespace isoft
