#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/FixedStepAccumulator.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>
#include <tina/runtime/InputActionMap.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/ui/UIContextConfig.hpp>

#include <string>

namespace Tina {

struct EngineConfig final {
    static constexpr Core::u32 MaximumFixedStepsPerFrame = 4;

    std::string applicationName;
    Platform::PrimaryWindowConfig primaryWindow;
    Platform::PlatformFrameCapacityConfig platformFrameCapacities{};
    UI::UIContextCapacityConfig primaryWindowUICapacities{};
    InputActionMapConfig inputActions;
    PlatformEventSubscriptionConfig platformEventSubscriptions{};
    Core::FixedStepConfig fixedSimulation;
    double gameplayTimeScale = 1.0;
    Core::Duration shutdownDeadline{5.0};

    [[nodiscard]] static EngineConfig Defaults();
    [[nodiscard]] Core::Status validate() const;
};

} // namespace Tina
