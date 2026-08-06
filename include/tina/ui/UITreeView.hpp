#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIScrollView.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

using UITreeViewItemKey = u64;
inline constexpr UITreeViewItemKey InvalidUITreeViewItemKey = 0;

struct UITreeViewItemDescriptor final {
    UITreeViewItemKey key = InvalidUITreeViewItemKey;
    std::string_view label{};
    u32 level = 0;
    bool enabled = true;
    bool expandable = false;
    bool expanded = false;
};

// Owner-thread data source borrowed by a TreeView. itemCount/resolveItem expose
// the current visible, depth-first projection. setItemExpanded atomically updates
// that projection and must leave it unchanged when returning false. The source
// and every returned label must outlive the binding; callbacks must not mutate
// UIContext or throw.
struct UITreeViewDataSource final {
    using ItemCountOperation = u64 (*)(const void* state) noexcept;
    using ResolveItemOperation = bool (*)(const void* state, u64 logicalIndex,
                                          UITreeViewItemDescriptor& output) noexcept;
    using SetItemExpandedOperation = bool (*)(void* state, UITreeViewItemKey key, bool expanded) noexcept;

    void* state = nullptr;
    ItemCountOperation itemCount = nullptr;
    ResolveItemOperation resolveItem = nullptr;
    SetItemExpandedOperation setItemExpanded = nullptr;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return state != nullptr && itemCount != nullptr && resolveItem != nullptr;
    }

    [[nodiscard]] constexpr bool canSetItemExpanded() const noexcept
    {
        return hasValue() && setItemExpanded != nullptr;
    }
};

struct UITreeViewCreateConfig final {
    static constexpr u32 DefaultMaterializedItemCapacity = 64;
    static constexpr u32 MaximumMaterializedItemCapacity = 4096;

    u32 materializedItemCapacity = DefaultMaterializedItemCapacity;

    auto operator<=>(const UITreeViewCreateConfig&) const = default;
};

struct UITreeViewStyle final {
    // Exact logical row extent. It must be large enough for the active
    // CollectionItem text line; layout rejects a row that would clip text.
    float rowHeight = 28.0F;
    u32 overscanRows = 2;
    UIScrollBarVisibility scrollBarVisibility = UIScrollBarVisibility::Auto;
    float wheelStep = 48.0F;
    float indentation = 18.0F;
    float disclosureExtent = 12.0F;
    float disclosureGap = 6.0F;

    auto operator<=>(const UITreeViewStyle&) const = default;
};

// Selected-row overlay colors. Zero-alpha state overrides fall back with
// pressed > hovered > focused > selected precedence. Focus belongs to the
// TreeView owner while hover/press belong to its committed materialized row;
// disabled rows keep the shared widget-opacity contract.
struct UITreeViewPaint final {
    UIScrollViewPaint scrollBar{};
    UIStraightSrgba8Color selectedItemBackgroundColor{};
    UIStraightSrgba8Color hoveredSelectedItemBackgroundColor{};
    UIStraightSrgba8Color focusedSelectedItemBackgroundColor{};
    UIStraightSrgba8Color pressedSelectedItemBackgroundColor{};
    UIStraightSrgba8Color disclosureColor{};

    auto operator<=>(const UITreeViewPaint&) const = default;
};

struct UITreeViewSelection final {
    UITreeViewItemKey key = InvalidUITreeViewItemKey;
    u64 logicalIndex = 0;
    u32 level = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return key != InvalidUITreeViewItemKey;
    }

    auto operator<=>(const UITreeViewSelection&) const = default;
};

struct UITreeViewMetrics final {
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

    auto operator<=>(const UITreeViewMetrics&) const = default;
};

enum class UITreeViewScrollAlignment : u8 {
    Nearest,
    Start,
    Center,
    End,
};

enum class UITreeViewCommand : u8 {
    PreviousItem,
    NextItem,
    PreviousPage,
    NextPage,
    FirstItem,
    LastItem,
    CollapseOrParent,
    ExpandOrFirstChild,
    ToggleExpanded,
    Activate,
};

struct UITreeViewCommandResult final {
    bool consumed = false;
    bool changed = false;
    bool expansionChanged = false;
    bool activated = false;
    UITreeViewSelection selection{};
};

} // namespace Tina::UI
