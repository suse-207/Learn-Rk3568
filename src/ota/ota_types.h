#pragma once

#include <string>

namespace ota {

enum class State {
    Idle,
    Checking,
    Downloading,
    Verifying,
    Installing,
    ReadyToSwitch,
    Rebooting,
    BootVerify,
    Success,
    Failed,
    Rollback,
};

const char* state_to_string(State state);

struct Progress {
    State state = State::Idle;
    int percent = 0;
    std::string detail;
};

}  // namespace ota

