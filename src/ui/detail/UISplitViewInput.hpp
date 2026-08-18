#pragma once

#include <tina/ui/UISplitView.hpp>

#include <algorithm>
#include <optional>

namespace Tina::UI::Detail {

[[nodiscard]] inline float splitterGrabOffset(
    const UISplitViewMetrics& metrics, UILogicalPoint pointer) noexcept
{
    return metrics.orientation == UISplitViewOrientation::Horizontal
               ? pointer.x - metrics.splitterRect.x
               : pointer.y - metrics.splitterRect.y;
}

[[nodiscard]] inline std::optional<float> resolveSplitViewFractionFromPointer(
    UILogicalRect contentRect, const UISplitViewConfig& config,
    UILogicalPoint pointer, float grabOffset) noexcept
{
    const bool horizontal = config.orientation == UISplitViewOrientation::Horizontal;
    const float mainExtent = horizontal ? contentRect.width : contentRect.height;
    const float paneExtent = mainExtent - (std::min)(config.splitterExtent, mainExtent);
    if (!(paneExtent > 0.0F))
    {
        return std::nullopt;
    }
    const float origin = horizontal ? contentRect.x : contentRect.y;
    const float pointerAxis = horizontal ? pointer.x : pointer.y;
    return std::clamp((pointerAxis - origin - grabOffset) / paneExtent, 0.0F, 1.0F);
}

} // namespace Tina::UI::Detail
