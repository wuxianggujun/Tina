#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

enum class UITabViewPlacement : u8 {
    Top = 0,
    Bottom,
    Left,
    Right,
};

enum class UITabActivationMode : u8 {
    Automatic = 0,
    Manual,
};

struct UITabViewConfig final {
    UITabViewPlacement placement = UITabViewPlacement::Top;
    UITabActivationMode activationMode = UITabActivationMode::Automatic;
    float tabGap = 2.0F;
    float contentGap = 0.0F;
    bool wrapKeyboardNavigation = true;

    auto operator<=>(const UITabViewConfig&) const = default;
};

// Strong authoring marker for a Tab header. Selection ownership and keyboard
// policy live on the owning TabView.
struct UITabConfig final {
    auto operator<=>(const UITabConfig&) const = default;
};

// Interaction chrome for a Tab header. The owning TabView supplies selection;
// zero-alpha state colors fall back through disabled > pressed > selected >
// hovered > focused > the Tab's UIBoxPaint fill.
struct UITabPaint final {
    UIStraightSrgba8Color selectedBackgroundColor{};
    UIStraightSrgba8Color hoveredBackgroundColor{};
    UIStraightSrgba8Color focusedBackgroundColor{};
    UIStraightSrgba8Color pressedBackgroundColor{};
    UIStraightSrgba8Color disabledBackgroundColor{};
    UIStraightSrgba8Color focusedBorderColor{};

    auto operator<=>(const UITabPaint&) const = default;
};

struct UITabViewItem final {
    UINodeId tab{};
    UINodeId panel{};

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return tab.hasValue() && panel.hasValue();
    }

    auto operator<=>(const UITabViewItem&) const = default;
};

// Geometry and selection from the last successful atomic UI publication.
struct UITabViewMetrics final {
    UILogicalRect tabStripRect{};
    UILogicalRect activePanelRect{};
    UINodeId activeTab{};
    UINodeId activePanel{};
    u32 activeIndex = 0;
    u32 itemCount = 0;
    UITabViewPlacement placement = UITabViewPlacement::Top;

    auto operator<=>(const UITabViewMetrics&) const = default;
};

enum class UITabViewCommand : u8 {
    Previous = 0,
    Next,
    First,
    Last,
};

struct UITabViewCommandResult final {
    bool targeted = false;
    bool consumed = false;
    bool focusChanged = false;
    bool selectionChanged = false;
    UINodeId tabView{};
    UINodeId tab{};
};

} // namespace Tina::UI
