#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Orthogonal interaction capabilities authored on an Element. Built-in
// recipes compose these flags; committed hit snapshots publish the same set so
// routing and focus never need a concrete widget type.
enum class UIElementBehavior : u32 {
    None = 0,
    Focusable = 1U << 0U,
    Activate = 1U << 1U,
    Toggle = 1U << 2U,
    RangeInput = 1U << 3U,
    TextInput = 1U << 4U,
    ProgressValue = 1U << 5U,
    ExclusiveChoice = 1U << 6U,
    ModalBarrier = 1U << 7U,
    Scroll = 1U << 8U,
    Select = 1U << 9U,
    Popup = 1U << 10U,
    SelectOption = 1U << 11U,
    VirtualList = 1U << 12U,
    VirtualListItem = 1U << 13U,
    VirtualTree = 1U << 14U,
    VirtualTreeItem = 1U << 15U,
};

[[nodiscard]] constexpr UIElementBehavior operator|(UIElementBehavior left, UIElementBehavior right) noexcept
{
    return static_cast<UIElementBehavior>(static_cast<u32>(left) | static_cast<u32>(right));
}

[[nodiscard]] constexpr UIElementBehavior operator&(UIElementBehavior left, UIElementBehavior right) noexcept
{
    return static_cast<UIElementBehavior>(static_cast<u32>(left) & static_cast<u32>(right));
}

constexpr UIElementBehavior& operator|=(UIElementBehavior& left, UIElementBehavior right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool hasBehavior(UIElementBehavior set, UIElementBehavior behavior) noexcept
{
    return (set & behavior) == behavior;
}

} // namespace Tina::UI
