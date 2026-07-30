#pragma once

#include <tina/ui/UIScrollView.hpp>

#include "UILayoutPrimitives.hpp"

#include <algorithm>

namespace Tina::UI::Detail {

struct ScrollViewLayoutInput final {
    UILogicalRect availableRect{};
    UILogicalSize rawContentSize{};
    UIScrollViewStyle style{};
    float scrollBarThickness = 0.0F;
    UIScrollOffset requestedOffset{};
};

struct ScrollViewLayoutPlan final {
    UIScrollViewMetrics metrics{};
    UILogicalRect viewportRect{};
    UILogicalRect contentRect{};
};

[[nodiscard]] inline ScrollViewLayoutPlan
resolveScrollViewLayout(const ScrollViewLayoutInput& input) noexcept
{
    const bool horizontalEnabled =
        hasScrollAxis(input.style.axes, UIScrollAxes::Horizontal);
    const bool verticalEnabled =
        hasScrollAxis(input.style.axes, UIScrollAxes::Vertical);
    const bool barsHidden =
        input.style.scrollBarVisibility == UIScrollBarVisibility::Hidden;
    bool horizontalBar =
        !barsHidden && horizontalEnabled &&
        input.style.scrollBarVisibility == UIScrollBarVisibility::Always;
    bool verticalBar =
        !barsHidden && verticalEnabled &&
        input.style.scrollBarVisibility == UIScrollBarVisibility::Always;

    // Auto bars can cause one another by reducing the opposite axis.
    // Two updates reach the fixed point for a rectangular viewport.
    for (usize passIndex = 0; passIndex < 2; ++passIndex)
    {
        const float viewportWidth = (std::max)(
            0.0F,
            input.availableRect.width -
                (verticalBar ? input.scrollBarThickness : 0.0F));
        const float viewportHeight = (std::max)(
            0.0F,
            input.availableRect.height -
                (horizontalBar ? input.scrollBarThickness : 0.0F));
        if (!barsHidden &&
            input.style.scrollBarVisibility == UIScrollBarVisibility::Auto)
        {
            horizontalBar =
                horizontalEnabled && input.rawContentSize.width > viewportWidth;
            verticalBar =
                verticalEnabled && input.rawContentSize.height > viewportHeight;
        }
    }

    const UILogicalRect viewportRect{
        .x = input.availableRect.x,
        .y = input.availableRect.y,
        .width = normalizeFloat((std::max)(
            0.0F,
            input.availableRect.width -
                (verticalBar ? input.scrollBarThickness : 0.0F))),
        .height = normalizeFloat((std::max)(
            0.0F,
            input.availableRect.height -
                (horizontalBar ? input.scrollBarThickness : 0.0F))),
    };
    const UILogicalSize contentSize{
        .width = normalizeFloat(
            horizontalEnabled
                ? (std::max)(input.rawContentSize.width, viewportRect.width)
                : viewportRect.width),
        .height = normalizeFloat(
            verticalEnabled
                ? (std::max)(input.rawContentSize.height, viewportRect.height)
                : viewportRect.height),
    };
    const UIScrollOffset offset{
        .x = horizontalEnabled
                 ? normalizeFloat((std::clamp)(
                       input.requestedOffset.x,
                       0.0F,
                       (std::max)(0.0F, contentSize.width - viewportRect.width)))
                 : 0.0F,
        .y = verticalEnabled
                 ? normalizeFloat((std::clamp)(
                       input.requestedOffset.y,
                       0.0F,
                       (std::max)(0.0F, contentSize.height - viewportRect.height)))
                 : 0.0F,
    };

    return ScrollViewLayoutPlan{
        .metrics =
            UIScrollViewMetrics{
                .offset = offset,
                .viewportSize = viewportRect.size(),
                .contentSize = contentSize,
                .horizontalScrollBarVisible = horizontalBar,
                .verticalScrollBarVisible = verticalBar,
            },
        .viewportRect = viewportRect,
        .contentRect =
            UILogicalRect{
                .x = normalizeFloat(viewportRect.x - offset.x),
                .y = normalizeFloat(viewportRect.y - offset.y),
                .width = contentSize.width,
                .height = contentSize.height,
            },
    };
}

} // namespace Tina::UI::Detail
