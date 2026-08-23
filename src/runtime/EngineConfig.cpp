#include <tina/runtime/EngineConfig.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include <cmath>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace Tina {
namespace {

[[nodiscard]] Core::Status invalidConfig(std::string_view message)
{
    return Core::failure(ConfigurationErrorCode::InvalidEngineConfig, message);
}

[[nodiscard]] bool isValidActionBindingPattern(const ActionBindingPattern& pattern) noexcept
{
    return std::visit(
        []<typename Pattern>(const Pattern& value) noexcept {
            using PatternType = std::remove_cvref_t<Pattern>;
            if constexpr (std::is_same_v<PatternType, PrimaryWindowKeyBinding>)
            {
                return value.key > Platform::Key::Unknown && value.key < Platform::Key::Count;
            } else if constexpr (std::is_same_v<PatternType, PrimaryPointerButtonBinding>)
            {
                return value.pointer == Platform::PrimaryPointerId && value.button < Platform::PointerButton::Count;
            } else if constexpr (std::is_same_v<PatternType, StandardGamepadButtonBinding>)
            {
                return value.button < Platform::GamepadButton::Count;
            } else
            {
                return value.axis < Platform::GamepadAxis::Count &&
                       value.valueMode >= GamepadAxisValueMode::Signed &&
                       value.valueMode <= GamepadAxisValueMode::Trigger;
            }
        },
        pattern);
}

[[nodiscard]] bool isValidActionTransform(const InputActionBinding& binding) noexcept
{
    const bool analog = std::holds_alternative<StandardGamepadAxisBinding>(binding.input);
    return std::isfinite(binding.deadzone) && std::isfinite(binding.scale) &&
           binding.deadzone >= 0.0F && binding.deadzone < 1.0F &&
           std::abs(binding.scale) > 1.0e-6F && std::abs(binding.scale) <= 16.0F &&
           (analog || binding.deadzone == 0.0F);
}

[[nodiscard]] Core::Status validatePlatformFrameCapacities(const Platform::PlatformFrameCapacityConfig& capacities)
{
    if (capacities.inputTransitionCapacity == 0 ||
        capacities.inputTransitionCapacity > Platform::PlatformFrameCapacityConfig::MaximumInputTransitionCapacity)
    {
        return invalidConfig("input raw transition capacity is outside the supported range");
    }
    if (capacities.inputTextByteCapacity == 0 ||
        capacities.inputTextByteCapacity > Platform::PlatformFrameCapacityConfig::MaximumInputTextByteCapacity)
    {
        return invalidConfig("input text byte capacity is outside the supported range");
    }
    if (capacities.platformEventCapacity == 0 ||
        capacities.platformEventCapacity > Platform::PlatformFrameCapacityConfig::MaximumPlatformEventCapacity)
    {
        return invalidConfig("platform event capacity is outside the supported range");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateInputActionMapConfig(const InputActionMapConfig& input)
{
    const InputActionMapCapacityConfig& capacities = input.capacities;
    if (capacities.simulationActionTransitionCapacity == 0 ||
        capacities.simulationActionTransitionCapacity >
            InputActionMapCapacityConfig::MaximumSimulationActionTransitionCapacity)
    {
        return invalidConfig("simulation action transition capacity is outside the supported range");
    }
    if (capacities.frameActionTransitionCapacity == 0 ||
        capacities.frameActionTransitionCapacity > InputActionMapCapacityConfig::MaximumFrameActionTransitionCapacity)
    {
        return invalidConfig("frame action transition capacity is outside the supported range");
    }
    if (capacities.actionBindingCapacity == 0 ||
        capacities.actionBindingCapacity > InputActionMapCapacityConfig::MaximumActionBindingCapacity)
    {
        return invalidConfig("action binding capacity is outside the supported range");
    }
    if (input.bindings.size() > capacities.actionBindingCapacity)
    {
        return invalidConfig("action bindings exceed the configured capacity");
    }

    for (usize index = 0; index < input.bindings.size(); ++index)
    {
        const InputActionBinding& binding = input.bindings[index];
        if (!binding.action.hasValue())
        {
            return invalidConfig("action binding uses an invalid action id");
        }
        if (!isValidActionBindingPattern(binding.input))
        {
            return invalidConfig("action binding uses an invalid physical control");
        }
        if (binding.domain != InputActionDomain::Simulation && binding.domain != InputActionDomain::Frame)
        {
            return invalidConfig("action binding uses an invalid action domain");
        }
        if (binding.composition != ActionCompositionMode::SumClamped &&
            binding.composition != ActionCompositionMode::StrongestMagnitude)
        {
            return invalidConfig("action binding uses an invalid composition mode");
        }
        if (!isValidActionTransform(binding))
        {
            return invalidConfig("action binding uses an invalid deadzone or scale");
        }

        const auto previousEnd = input.bindings.begin() + static_cast<std::ptrdiff_t>(index);
        const auto duplicate = std::ranges::find(input.bindings.begin(), previousEnd,
                                                 binding.input, &InputActionBinding::input);
        if (duplicate != previousEnd)
        {
            return invalidConfig("one physical control may have only one binding in the default input context");
        }
        if (binding.binding.hasValue())
        {
            const auto duplicateId = std::ranges::find(input.bindings.begin(), previousEnd,
                                                       binding.binding, &InputActionBinding::binding);
            if (duplicateId != previousEnd)
            {
                return invalidConfig("explicit action binding ids must be unique");
            }
        }
        const auto conflictingDomain = std::ranges::find_if(
            input.bindings.begin(), previousEnd, [&binding](const InputActionBinding& previous) {
                return previous.action == binding.action &&
                       (previous.domain != binding.domain ||
                        previous.composition != binding.composition);
            });
        if (conflictingDomain != previousEnd)
        {
            return invalidConfig("one action id must use one input domain and composition mode");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status
validatePrimaryWindowUIDisplayListCapacities(const PrimaryWindowUIDisplayListCapacityConfig& capacities)
{
    if (capacities.commandCapacity == 0 ||
        capacities.commandCapacity > PrimaryWindowUIDisplayListCapacityConfig::MaximumEntryCapacity)
    {
        return invalidConfig("primary-window UI DisplayList command capacity is outside the supported range");
    }
    if (capacities.clipCapacity > capacities.commandCapacity)
    {
        return invalidConfig("primary-window UI DisplayList clip capacity must not exceed command capacity");
    }
    if (capacities.batchCapacity == 0 || capacities.batchCapacity > capacities.commandCapacity)
    {
        return invalidConfig(
            "primary-window UI DisplayList batch capacity must be non-zero and not exceed command capacity");
    }
    return Core::success();
}

} // namespace

EngineConfig EngineConfig::Defaults()
{
    return EngineConfig{
        .applicationName = "Tina",
        .primaryWindow = Platform::PrimaryWindowConfig{},
        .platformFrameCapacities = Platform::PlatformFrameCapacityConfig{},
        .primaryWindowUICapacities = UI::UIContextCapacityConfig{},
        .primaryWindowUIDisplayListCapacities = PrimaryWindowUIDisplayListCapacityConfig{},
        .renderSceneCapacities = Render::RenderSceneCapacity{},
        .shadowMapExtents = Render::ShadowMapExtentConfig{},
        .renderDrawCallCapacity =
            Render::RenderDeviceCreateParams::DefaultDrawCallCapacity,
        .inputActions = InputActionMapConfig{},
        .platformEventSubscriptions = PlatformEventSubscriptionConfig{},
        .fixedSimulation = Core::FixedStepConfig{},
        .gameplayTimeScale = 1.0,
        .shutdownDeadline = Core::Duration{5.0},
    };
}

Core::Status EngineConfig::validate() const
{
    if (applicationName.empty())
    {
        return invalidConfig("applicationName must not be empty");
    }
    if (!Core::isStrictUtf8WithoutNul(applicationName))
    {
        return invalidConfig("applicationName must be valid UTF-8 without embedded NUL bytes");
    }
    if (primaryWindow.title.empty())
    {
        return invalidConfig("primaryWindow.title must not be empty");
    }
    if (!Core::isStrictUtf8WithoutNul(primaryWindow.title))
    {
        return invalidConfig("primaryWindow.title must be valid UTF-8 without embedded NUL bytes");
    }
    if (primaryWindow.initialLogicalExtent.width == 0 || primaryWindow.initialLogicalExtent.height == 0)
    {
        return invalidConfig("primaryWindow initial logical extent must be non-zero");
    }
    if (primaryWindow.mode != Platform::WindowMode::Windowed &&
        primaryWindow.mode != Platform::WindowMode::BorderlessFullscreen)
    {
        return invalidConfig("primaryWindow.mode is invalid");
    }
    if (auto platformCapacityStatus = validatePlatformFrameCapacities(platformFrameCapacities); !platformCapacityStatus)
    {
        return platformCapacityStatus;
    }
    if (auto uiCapacityStatus = UI::validateUIContextCapacityConfig(primaryWindowUICapacities); !uiCapacityStatus)
    {
        Core::Error error{ConfigurationErrorCode::InvalidEngineConfig, "primaryWindowUICapacities is invalid"};
        error.addContext("EngineConfig::validate", uiCapacityStatus.error().message);
        return Core::failure(std::move(error));
    }
    if (auto uiDisplayCapacityStatus =
            validatePrimaryWindowUIDisplayListCapacities(primaryWindowUIDisplayListCapacities);
        !uiDisplayCapacityStatus)
    {
        return uiDisplayCapacityStatus;
    }
    if (auto renderSceneCapacityStatus = Render::validateRenderSceneCapacity(renderSceneCapacities);
        !renderSceneCapacityStatus)
    {
        Core::Error error{ConfigurationErrorCode::InvalidEngineConfig, "renderSceneCapacities is invalid"};
        error.addContext("EngineConfig::validate", renderSceneCapacityStatus.error().message);
        return Core::failure(std::move(error));
    }
    if (auto shadowMapStatus = Render::validateShadowMapExtentConfig(shadowMapExtents);
        !shadowMapStatus)
    {
        Core::Error error{ConfigurationErrorCode::InvalidEngineConfig, "shadowMapExtents is invalid"};
        error.addContext("EngineConfig::validate", shadowMapStatus.error().message);
        return Core::failure(std::move(error));
    }
    if (!Render::RenderDeviceCreateParams::isSupportedDrawCallCapacity(
            renderDrawCallCapacity))
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "renderDrawCallCapacity must be a 1024-entry block or the 65535 native maximum");
    }
    if (renderMsaaSamples != 0U && renderMsaaSamples != 2U && renderMsaaSamples != 4U &&
        renderMsaaSamples != 8U && renderMsaaSamples != 16U)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "renderMsaaSamples must be 0, 2, 4, 8, or 16");
    }
    if (platformEventSubscriptions.subscriberCapacity == 0 ||
        platformEventSubscriptions.subscriberCapacity > PlatformEventSubscriptionConfig::MaximumSubscriberCapacity)
    {
        return invalidConfig("platform event subscriber capacity is outside the supported range");
    }
    if (auto inputStatus = validateInputActionMapConfig(inputActions); !inputStatus)
    {
        return inputStatus;
    }
    if (!std::isfinite(gameplayTimeScale) || gameplayTimeScale < 0.0)
    {
        return invalidConfig("gameplayTimeScale must be finite and non-negative");
    }
    if (!std::isfinite(shutdownDeadline.count()) || shutdownDeadline.count() <= 0.0)
    {
        return invalidConfig("shutdownDeadline must be finite and greater than zero");
    }
    if (fixedSimulation.maximumStepsPerFrame > MaximumFixedStepsPerFrame)
    {
        return invalidConfig("fixedSimulation.maximumStepsPerFrame must not exceed four");
    }
    if (auto accumulator = Core::FixedStepAccumulator::Create(fixedSimulation); !accumulator)
    {
        auto error = Core::Error{ConfigurationErrorCode::InvalidEngineConfig, "fixedSimulation is invalid"};
        error.addContext("EngineConfig::validate", accumulator.error().message);
        return Core::failure(std::move(error));
    }
    return Core::success();
}

} // namespace Tina
