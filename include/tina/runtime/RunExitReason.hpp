#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina {

enum class RunExitReason : Core::u8 {
    GameRequestedExitAfterCurrentFrame = 1,
    PrimaryWindowRequestedClose = 2,
    GameStateStackBecameEmpty = 3,
};

enum class RunStopCause : Core::u8 {
    GameRequestedExitAfterCurrentFrame = 1,
    PrimaryWindowRequestedClose = 2,
    GameStateStackBecameEmpty = 3,
    RuntimeFailure = 4,
};

} // namespace Tina
