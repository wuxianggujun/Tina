#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIScrollView.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

using UIListViewItemKey = u64;
inline constexpr UIListViewItemKey InvalidUIListViewItemKey = 0;

struct UIListViewItemDescriptor final {
    UIListViewItemKey key = InvalidUIListViewItemKey;
    std::string_view label{};
    bool enabled = true;
};

// Owner-thread data source borrowed by a ListView. The source and every label
// returned by resolveItem must remain alive until the source is replaced or
// cleared. Functions must not mutate UIContext or throw.
struct UIListViewDataSource final {
    using ItemCountOperation = u64 (*)(const void* state) noexcept;
    using ResolveItemOperation = bool (*)(const void* state, u64 logicalIndex,
                                          UIListViewItemDescriptor& output) noexcept;

    const void* state = nullptr;
    ItemCountOperation itemCount = nullptr;
    ResolveItemOperation resolveItem = nullptr;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return state != nullptr && itemCount != nullptr && resolveItem != nullptr;
    }
};

struct UIListViewCreateConfig final {
    static constexpr u32 DefaultMaterializedItemCapacity = 64;
    static constexpr u32 MaximumMaterializedItemCapacity = 4096;

    // Fixed internal row-node pool. It must cover the largest viewport plus
    // overscan; logical item count is independent and may be much larger.
    u32 materializedItemCapacity = DefaultMaterializedItemCapacity;

    auto operator<=>(const UIListViewCreateConfig&) const = default;
};

struct UIListViewStyle final {
    float rowHeight = 28.0F;
    u32 overscanRows = 2;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float wheelStep = 48.0F;

    auto operator<=>(const UIListViewStyle&) const = default;
};

// Selected-row overlay colors. Zero-alpha state overrides fall back with
// pressed > hovered > focused > selected precedence. Focus belongs to the
// ListView owner while hover/press belong to its committed materialized row;
// disabled rows keep the shared widget-opacity contract.
struct UIListViewPaint final {
    UIScrollViewPaint scrollBar{};
    UIStraightSrgba8Color selectedItemBackgroundColor{};
    UIStraightSrgba8Color hoveredSelectedItemBackgroundColor{};
    UIStraightSrgba8Color focusedSelectedItemBackgroundColor{};
    UIStraightSrgba8Color pressedSelectedItemBackgroundColor{};

    auto operator<=>(const UIListViewPaint&) const = default;
};

struct UIListViewSelection final {
    UIListViewItemKey key = InvalidUIListViewItemKey;
    u64 logicalIndex = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return key != InvalidUIListViewItemKey;
    }

    auto operator<=>(const UIListViewSelection&) const = default;
};

struct UIListViewMetrics final {
    u64 logicalItemCount = 0;
    u64 firstVisibleIndex = 0;
    u32 visibleItemCount = 0;
    u64 firstMaterializedIndex = 0;
    u32 materializedItemCount = 0;
    u32 materializedItemCapacity = 0;
    float scrollOffset = 0.0F;
    float maxScrollOffset = 0.0F;
    UILogicalSize viewportSize{};
    UILogicalSize contentSize{};
    bool verticalScrollBarVisible = false;

    auto operator<=>(const UIListViewMetrics&) const = default;
};

enum class UIListViewScrollAlignment : u8 {
    Nearest,
    Start,
    Center,
    End,
};

enum class UIListViewCommand : u8 {
    PreviousItem,
    NextItem,
    PreviousPage,
    NextPage,
    FirstItem,
    LastItem,
    Activate,
};

struct UIListViewCommandResult final {
    bool consumed = false;
    bool changed = false;
    bool activated = false;
    UIListViewSelection selection{};
};

} // namespace Tina::UI
