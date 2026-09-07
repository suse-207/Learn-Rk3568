#include "ota/ota_types.h"

namespace ota {

const char* state_to_string(State state) {
    switch (state) {
        case State::Idle:
            return "IDLE";
        case State::Checking:
            return "CHECKING";
        case State::Downloading:
            return "DOWNLOADING";
        case State::Verifying:
            return "VERIFYING";
        case State::Installing:
            return "INSTALLING";
        case State::ReadyToSwitch:
            return "READY_TO_SWITCH";
        case State::Rebooting:
            return "REBOOTING";
        case State::BootVerify:
            return "BOOT_VERIFY";
        case State::Success:
            return "SUCCESS";
        case State::Failed:
            return "FAILED";
        case State::Rollback:
            return "ROLLBACK";
    }
    return "UNKNOWN";
}

}  // namespace ota

