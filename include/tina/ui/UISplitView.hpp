#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

enum class UISplitViewOrientation : u8 {
    Horizontal = 0,
    Vertical,
};

struct UISplitViewConfig final {
    UISplitViewOrientation orientation = UISplitViewOrientation::Horizontal;
    float initialFraction = 0.5F;
    float minPrimarySize = 0.0F;
    float minSecondarySize = 0.0F;
    // Zero selects the active Theme density metric; a positive value is an
    // explicit product override for this SplitView.
    float splitterExtent = 0.0F;

    auto operator<=>(const UISplitViewConfig&) const = default;
};

// Authoring marker and keyboard adjustment policy for the dedicated Splitter
// contract. Pointer geometry and orientation come from its owning SplitView.
struct UISplitterConfig final {
    float keyboardStep = 0.02F;

    auto operator<=>(const UISplitterConfig&) const = default;
};

// Splitter chrome is intentionally separate from its hit extent. The owning
// SplitView controls layout and pointer geometry; this profile only paints a
// centered line and an optional FocusVisible ring inside that hit target.
struct UISplitterPaint final {
    UIStraightSrgba8Color lineColor{};
    UIStraightSrgba8Color hoveredLineColor{};
    UIStraightSrgba8Color draggingLineColor{};
    UIStraightSrgba8Color focusRingColor{};
    float lineThickness = 1.0F;
    float focusRingThickness = 3.0F;

    auto operator<=>(const UISplitterPaint&) const = default;
};

struct UISplitViewParts final {
    UINodeId primaryPane{};
    UINodeId splitter{};
    UINodeId secondaryPane{};

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return primaryPane.hasValue() && splitter.hasValue() && secondaryPane.hasValue();
    }

    auto operator<=>(const UISplitViewParts&) const = default;
};

// Geometry from the last successful atomic UI publication. The fraction is
// the resolved primary-pane share after minimum-size clamping.
struct UISplitViewMetrics final {
    UILogicalRect primaryRect{};
    UILogicalRect splitterRect{};
    UILogicalRect secondaryRect{};
    float fraction = 0.5F;
    UISplitViewOrientation orientation = UISplitViewOrientation::Horizontal;

    auto operator<=>(const UISplitViewMetrics&) const = default;
};

} // namespace Tina::UI
