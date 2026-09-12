#include <tina/runtime/InputActionMap.hpp>

#include "input/InputBindingRules.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <string>
#include <string_view>
#include <type_traits>

namespace Tina {
namespace {

[[nodiscard]] std::string describePattern(const ActionBindingPattern& pattern)
{
    return std::visit([]<typename Pattern>(const Pattern& value) -> std::string {
        if constexpr (std::is_same_v<Pattern, PrimaryWindowKeyBinding>)
        {
            return "key=" + std::to_string(static_cast<u32>(value.key));
        } else if constexpr (std::is_same_v<Pattern, PointerButtonBinding>)
        {
            return "pointer=" + std::to_string(value.pointer) +
                   " button=" + std::to_string(static_cast<u32>(value.button));
        } else if constexpr (std::is_same_v<Pattern, StandardGamepadButtonBinding>)
        {
            return "gamepadButton=" + std::to_string(static_cast<u32>(value.button));
        } else
        {
            return "gamepadAxis=" + std::to_string(static_cast<u32>(value.axis)) +
                   " mode=" + std::to_string(static_cast<u32>(value.valueMode));
        }
    }, pattern);
}

[[nodiscard]] Core::Status invalidBinding(std::string_view message, usize index,
                                          const InputActionBinding& binding)
{
    Core::Error error{ConfigurationErrorCode::InvalidEngineConfig, message};
    error.addContext("InputActionMapConfig::validate",
        "index=" + std::to_string(index) + " binding=" + std::to_string(binding.binding.value()) +
        " action=" + std::to_string(binding.action.value()) + " " + describePattern(binding.input));
    return Core::failure(std::move(error));
}

} // namespace

Core::Status InputActionMapConfig::validate() const
{
    if (capacities.simulationActionTransitionCapacity == 0 ||
        capacities.simulationActionTransitionCapacity > InputActionMapCapacityConfig::MaximumSimulationActionTransitionCapacity ||
        capacities.frameActionTransitionCapacity == 0 ||
        capacities.frameActionTransitionCapacity > InputActionMapCapacityConfig::MaximumFrameActionTransitionCapacity ||
        capacities.actionBindingCapacity == 0 ||
        capacities.actionBindingCapacity > InputActionMapCapacityConfig::MaximumActionBindingCapacity)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "input action capacities are outside the supported range");
    }
    if (bindings.size() > capacities.actionBindingCapacity)
    {
        return Core::failure(ConfigurationErrorCode::InvalidEngineConfig,
                             "action bindings exceed the configured capacity");
    }
    for (usize index = 0; index < bindings.size(); ++index)
    {
        const InputActionBinding& binding = bindings[index];
        if (!binding.action.hasValue())
        {
            return invalidBinding("action binding uses an invalid action id", index, binding);
        }
        if (!Runtime::Input::isValidBindingPattern(binding.input))
        {
            return invalidBinding("action binding uses an invalid physical control", index, binding);
        }
        if (binding.domain != InputActionDomain::Simulation && binding.domain != InputActionDomain::Frame)
        {
            return invalidBinding("action binding uses an invalid action domain", index, binding);
        }
        if (binding.composition != ActionCompositionMode::SumClamped &&
            binding.composition != ActionCompositionMode::StrongestMagnitude)
        {
            return invalidBinding("action binding uses an invalid composition mode", index, binding);
        }
        if (!Runtime::Input::isValidBindingTransform(binding.input, binding.deadzone, binding.scale))
        {
            return invalidBinding("action binding uses an invalid deadzone or scale", index, binding);
        }
        for (usize previous = 0; previous < index; ++previous)
        {
            const InputActionBinding& other = bindings[previous];
            if (binding.binding.hasValue() && binding.binding == other.binding)
            {
                return invalidBinding("explicit action binding ids must be unique", index, binding);
            }
            if (binding.action != other.action)
            {
                continue;
            }
            if (binding.domain != other.domain || binding.composition != other.composition)
            {
                return invalidBinding("one action id must use one input domain and composition mode", index, binding);
            }
            if (binding.input == other.input)
            {
                return invalidBinding("one action cannot bind the same input pattern twice", index, binding);
            }
        }
    }
    return Core::success();
}

} // namespace Tina
