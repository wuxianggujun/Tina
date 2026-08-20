#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UIText.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

using UIVirtualGridViewItemKey = u64;
inline constexpr UIVirtualGridViewItemKey InvalidUIVirtualGridViewItemKey = 0;

struct UIVirtualGridViewItemDescriptor final {
    UIVirtualGridViewItemKey key = InvalidUIVirtualGridViewItemKey;
    std::string_view label{};
    bool enabled = true;
};

// Owner-thread data source borrowed by a VirtualGridView. The source and every
// label returned by resolveItem must remain alive until the source is replaced
// or cleared. Functions must not mutate UIContext or throw.
struct UIVirtualGridViewDataSource final {
    using ItemCountOperation = u64 (*)(const void* state) noexcept;
    using ResolveItemOperation = bool (*)(
        const void* state, u64 logicalIndex,
        UIVirtualGridViewItemDescriptor& output) noexcept;

    const void* state = nullptr;
    ItemCountOperation itemCount = nullptr;
    ResolveItemOperation resolveItem = nullptr;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return state != nullptr && itemCount != nullptr && resolveItem != nullptr;
    }
};

struct UIVirtualGridViewCreateConfig final {
    static constexpr u32 DefaultMaterializedItemCapacity = 64;
    static constexpr u32 MaximumMaterializedItemCapacity = 4096;

    // Fixed internal item-node pool. It must cover every column in the largest
    // viewport plus vertically overscanned rows.
    u32 materializedItemCapacity = DefaultMaterializedItemCapacity;

    auto operator<=>(const UIVirtualGridViewCreateConfig&) const = default;
};

struct UIVirtualGridViewStyle final {
    // The planner fits as many equal-width columns as possible without making
    // an item narrower than minimumItemWidth. A narrow viewport still keeps
    // one column and lets that column use the available width.
    float minimumItemWidth = 120.0F;
    float itemHeight = 96.0F;
    float columnGap = 8.0F;
    float rowGap = 8.0F;
    // Zero leaves the responsive column count uncapped.
    u32 maximumColumnCount = 0;
    u32 overscanRows = 2;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float wheelStep = 48.0F;
    UITextOverflow itemTextOverflow = UITextOverflow::Clip;

    auto operator<=>(const UIVirtualGridViewStyle&) const = default;
};

struct UIVirtualGridViewPaint final {
    UIScrollViewPaint scrollBar{};
    UIStraightSrgba8Color selectedItemBackgroundColor{};
    UIStraightSrgba8Color hoveredSelectedItemBackgroundColor{};
    UIStraightSrgba8Color focusedSelectedItemBackgroundColor{};
    UIStraightSrgba8Color pressedSelectedItemBackgroundColor{};

    auto operator<=>(const UIVirtualGridViewPaint&) const = default;
};

struct UIVirtualGridViewSelection final {
    UIVirtualGridViewItemKey key = InvalidUIVirtualGridViewItemKey;
    u64 logicalIndex = 0;
    u64 logicalRow = 0;
    u32 logicalColumn = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return key != InvalidUIVirtualGridViewItemKey;
    }

    auto operator<=>(const UIVirtualGridViewSelection&) const = default;
};

struct UIVirtualGridViewMetrics final {
    u64 logicalItemCount = 0;
    u64 logicalRowCount = 0;
    u32 logicalColumnCount = 0;
    u64 firstVisibleRow = 0;
    u32 visibleRowCount = 0;
    u64 firstMaterializedRow = 0;
    u32 materializedRowCount = 0;
    u64 firstMaterializedIndex = 0;
    u32 materializedItemCount = 0;
    u32 materializedItemCapacity = 0;
    float itemWidth = 0.0F;
    float scrollOffset = 0.0F;
    float maxScrollOffset = 0.0F;
    UILogicalSize viewportSize{};
    UILogicalSize contentSize{};
    bool verticalScrollBarVisible = false;

    auto operator<=>(const UIVirtualGridViewMetrics&) const = default;
};

enum class UIVirtualGridViewScrollAlignment : u8 {
    Nearest,
    Start,
    Center,
    End,
};

enum class UIVirtualGridViewCommand : u8 {
    PreviousItem,
    NextItem,
    PreviousRow,
    NextRow,
    PreviousPage,
    NextPage,
    FirstItem,
    LastItem,
    Activate,
};

struct UIVirtualGridViewCommandResult final {
    bool consumed = false;
    bool changed = false;
    bool activated = false;
    UIVirtualGridViewSelection selection{};
};

} // namespace Tina::UI
