#pragma once

#include <tina/runtime/InputActionMap.hpp>

#include <cmath>
#include <limits>
#include <type_traits>

namespace Tina::Runtime::Input {

inline constexpr float ActionValueEpsilon = 1.0e-6F;
inline constexpr usize InvalidBindingIndex = (std::numeric_limits<usize>::max)();
inline constexpr usize PointerControlOffset = Platform::KeyCount;
inline constexpr usize GamepadButtonControlOffset =
    PointerControlOffset + Platform::PointerCapacity * Platform::PointerButtonCount;
inline constexpr usize GamepadAxisControlOffset = GamepadButtonControlOffset + Platform::GamepadButtonCount;
inline constexpr usize PhysicalControlCount = GamepadAxisControlOffset + Platform::GamepadAxisCount;

// A control is the routing key, not a binding. Axis value modes are transforms
// of the same physical control and therefore share one fanout bucket.
[[nodiscard]] constexpr usize physicalControlIndex(const ActionBindingPattern& pattern) noexcept
{
    return std::visit([]<typename Pattern>(const Pattern& value) -> usize {
        if constexpr (std::is_same_v<Pattern, PrimaryWindowKeyBinding>)
        {
            return value.key > Platform::Key::Unknown && value.key < Platform::Key::Count
                ? static_cast<usize>(value.key) : InvalidBindingIndex;
        } else if constexpr (std::is_same_v<Pattern, PointerButtonBinding>)
        {
            return value.pointer < Platform::PointerCapacity && value.button < Platform::PointerButton::Count
                ? PointerControlOffset + value.pointer * Platform::PointerButtonCount + static_cast<usize>(value.button)
                : InvalidBindingIndex;
        } else if constexpr (std::is_same_v<Pattern, StandardGamepadButtonBinding>)
        {
            return value.button < Platform::GamepadButton::Count
                ? GamepadButtonControlOffset + static_cast<usize>(value.button) : InvalidBindingIndex;
        } else
        {
            return value.axis < Platform::GamepadAxis::Count
                ? GamepadAxisControlOffset + static_cast<usize>(value.axis) : InvalidBindingIndex;
        }
    }, pattern);
}

[[nodiscard]] constexpr bool isValidBindingPattern(const ActionBindingPattern& pattern) noexcept
{
    if (physicalControlIndex(pattern) == InvalidBindingIndex)
    {
        return false;
    }
    const auto* axis = std::get_if<StandardGamepadAxisBinding>(&pattern);
    return axis == nullptr || (axis->valueMode >= GamepadAxisValueMode::Signed &&
                               axis->valueMode <= GamepadAxisValueMode::Trigger);
}

[[nodiscard]] inline bool isValidBindingTransform(const ActionBindingPattern& pattern,
                                                  float deadzone, float scale) noexcept
{
    return std::isfinite(deadzone) && std::isfinite(scale) && deadzone >= 0.0F && deadzone < 1.0F &&
           std::abs(scale) > ActionValueEpsilon && std::abs(scale) <= 16.0F &&
           (std::holds_alternative<StandardGamepadAxisBinding>(pattern) || deadzone == 0.0F);
}

} // namespace Tina::Runtime::Input
