#pragma once

#include <tina/core/base/EnumFlags.hpp>
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
    VirtualGrid = 1U << 16U,
    VirtualGridItem = 1U << 17U,
    DataGrid = 1U << 18U,
    DataGridRow = 1U << 19U,
    DataGridCell = 1U << 20U,
    DataGridColumnHeader = 1U << 21U,
};

TINA_ENUM_FLAG_OPERATORS(UIElementBehavior);

[[nodiscard]] constexpr bool hasBehavior(UIElementBehavior set, UIElementBehavior behavior) noexcept
{
    return hasAllFlags(set, behavior);
}

} // namespace Tina::UI
