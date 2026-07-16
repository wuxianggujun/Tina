#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/FixedStepAccumulator.hpp>

#include <string>

namespace Tina {

struct EngineConfig final {
    static constexpr Core::u32 MaximumFixedStepsPerFrame = 4;

    std::string applicationName;
    Core::FixedStepConfig fixedSimulation;
    double gameplayTimeScale = 1.0;
    Core::Duration shutdownDeadline{5.0};

    [[nodiscard]] static EngineConfig Defaults();
    [[nodiscard]] Core::Status validate() const;
};

} // namespace Tina
