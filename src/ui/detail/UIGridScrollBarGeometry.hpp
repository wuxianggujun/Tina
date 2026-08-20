#pragma once

#include "UIControlGeometry.hpp"

#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

namespace Tina::UI::Detail {

struct UIDataGridScrollBarGeometry final {
    ScrollBarGeometry horizontal{};
    ScrollBarGeometry vertical{};
};

[[nodiscard]] inline UIScrollViewMetrics makeVirtualGridViewScrollMetrics(
    const UIVirtualGridViewMetrics& metrics) noexcept
{
    return UIScrollViewMetrics{
        .offset = {.x = 0.0F, .y = metrics.scrollOffset},
        .viewportSize = metrics.viewportSize,
        .contentSize = metrics.contentSize,
        .horizontalScrollBarVisible = false,
        .verticalScrollBarVisible = metrics.verticalScrollBarVisible,
    };
}

[[nodiscard]] inline ScrollBarGeometry makeVirtualGridViewScrollBarGeometry(
    const UIVirtualGridViewMetrics& metrics, UILogicalRect viewportRect,
    const UIScrollViewPaint& paint) noexcept
{
    return makeScrollBarGeometry(makeVirtualGridViewScrollMetrics(metrics),
                                 viewportRect, paint,
                                 UIScrollAxes::Vertical);
}

[[nodiscard]] inline UIScrollViewMetrics makeDataGridScrollMetrics(
    const UIDataGridMetrics& metrics) noexcept
{
    return UIScrollViewMetrics{
        .offset = metrics.scrollOffset,
        .viewportSize = metrics.viewportSize,
        .contentSize = metrics.contentSize,
        .horizontalScrollBarVisible = metrics.horizontalScrollBarVisible,
        .verticalScrollBarVisible = metrics.verticalScrollBarVisible,
    };
}

[[nodiscard]] inline UIDataGridScrollBarGeometry
makeDataGridScrollBarGeometry(const UIDataGridMetrics& metrics,
                              UILogicalRect bodyViewportRect,
                              const UIScrollViewPaint& paint) noexcept
{
    const UIScrollViewMetrics scrollMetrics =
        makeDataGridScrollMetrics(metrics);
    return UIDataGridScrollBarGeometry{
        .horizontal = makeScrollBarGeometry(
            scrollMetrics, bodyViewportRect, paint, UIScrollAxes::Horizontal),
        .vertical = makeScrollBarGeometry(
            scrollMetrics, bodyViewportRect, paint, UIScrollAxes::Vertical),
    };
}

} // namespace Tina::UI::Detail
