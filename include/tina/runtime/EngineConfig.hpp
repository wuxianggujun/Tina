#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/time/FixedStepAccumulator.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>
#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/InputActionMap.hpp>
#include <tina/runtime/PlatformEvents.hpp>
#include <tina/ui/UIContextConfig.hpp>

#include <string>

namespace Tina {

struct PrimaryWindowUIDisplayListCapacityConfig final {
    static constexpr Core::u32 DefaultCommandCapacity = 4096;
    static constexpr Core::u32 DefaultClipCapacity = 4096;
    static constexpr Core::u32 DefaultBatchCapacity = 4096;
    static constexpr Core::u32 MaximumEntryCapacity = 1'048'576;

    Core::u32 commandCapacity = DefaultCommandCapacity;
    Core::u32 clipCapacity = DefaultClipCapacity;
    Core::u32 batchCapacity = DefaultBatchCapacity;
};

struct EngineConfig final {
    static constexpr Core::u32 MaximumFixedStepsPerFrame = 4;

    std::string applicationName;
    Platform::PrimaryWindowConfig primaryWindow;
    Platform::PlatformFrameCapacityConfig platformFrameCapacities{};
    UI::UIContextCapacityConfig primaryWindowUICapacities{};
    PrimaryWindowUIDisplayListCapacityConfig primaryWindowUIDisplayListCapacities{};
    Render::RenderSceneCapacity renderSceneCapacities{};
    Render::ShadowMapExtentConfig shadowMapExtents{};
    // Per-frame backend submission storage. Tools with bounded workloads can
    // lower this without changing RenderScene or UI capacities.
    Core::u32 renderDrawCallCapacity =
        Render::RenderDeviceCreateParams::DefaultDrawCallCapacity;
    // Backbuffer MSAA sample count (0 = off, 2/4/8/16). Pixel-evidence gates
    // and samples keep the 0 default; interactive tools may opt in.
    Core::u8 renderMsaaSamples = 0;
    InputActionMapConfig inputActions;
    PlatformEventSubscriptionConfig platformEventSubscriptions{};
    Core::FixedStepConfig fixedSimulation;
    double gameplayTimeScale = 1.0;
    // Maximum TaskSystem worker-exit/join wait during EngineHost shutdown.
    Core::Duration shutdownDeadline{5.0};

    [[nodiscard]] static EngineConfig Defaults();
    [[nodiscard]] Core::Status validate() const;
};

} // namespace Tina
