#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>

namespace Tina {

struct FrameTiming final {
    Core::Duration realDelta{};
    Core::Duration acceptedRealDelta{};
    Core::Duration rejectedRealDelta{};
    Core::Duration updateDelta{};
    Core::Duration discardedSimulationDelta{};
    Core::Duration fixedDelta{};
    double interpolation = 0.0;
    Core::u64 frameIndex = 0;
    Core::u64 completedSimulationTicks = 0;
    Core::u32 fixedStepCount = 0;
};

struct FixedUpdateTiming final {
    Core::Duration fixedDelta{};
    Core::u64 simulationTickIndex = 0;
    Core::u32 fixedStepIndexInFrame = 0;
    Core::u32 fixedStepCountInFrame = 0;
};

} // namespace Tina
