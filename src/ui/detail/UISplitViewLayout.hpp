#pragma once

#include <tina/ui/UISplitView.hpp>

#include <algorithm>

namespace Tina::UI::Detail {

struct UISplitViewLayoutPlan final {
    UISplitViewMetrics metrics{};
    float paneExtent = 0.0F;
};

[[nodiscard]] inline UISplitViewLayoutPlan resolveSplitViewLayout(
    UILogicalRect contentRect, const UISplitViewConfig& config,
    float requestedFraction) noexcept
{
    const bool horizontal = config.orientation == UISplitViewOrientation::Horizontal;
    const float mainExtent = (std::max)(0.0F, horizontal ? contentRect.width : contentRect.height);
    const float splitterExtent = (std::min)(config.splitterExtent, mainExtent);
    const float paneExtent = (std::max)(0.0F, mainExtent - splitterExtent);
    float primaryExtent = paneExtent * std::clamp(requestedFraction, 0.0F, 1.0F);
    const float minimumSum = config.minPrimarySize + config.minSecondarySize;
    if (minimumSum <= paneExtent)
    {
        primaryExtent = std::clamp(primaryExtent, config.minPrimarySize,
                                   paneExtent - config.minSecondarySize);
    }
    else if (minimumSum > 0.0F)
    {
        primaryExtent = paneExtent * (config.minPrimarySize / minimumSum);
    }
    const float secondaryExtent = (std::max)(0.0F, paneExtent - primaryExtent);
    const float resolvedFraction = paneExtent > 0.0F ? primaryExtent / paneExtent : 0.0F;

    UISplitViewMetrics metrics{
        .fraction = resolvedFraction,
        .orientation = config.orientation,
    };
    if (horizontal)
    {
        metrics.primaryRect = {contentRect.x, contentRect.y, primaryExtent, contentRect.height};
        metrics.splitterRect = {contentRect.x + primaryExtent, contentRect.y,
                                splitterExtent, contentRect.height};
        metrics.secondaryRect = {contentRect.x + primaryExtent + splitterExtent,
                                 contentRect.y, secondaryExtent, contentRect.height};
    }
    else
    {
        metrics.primaryRect = {contentRect.x, contentRect.y, contentRect.width, primaryExtent};
        metrics.splitterRect = {contentRect.x, contentRect.y + primaryExtent,
                                contentRect.width, splitterExtent};
        metrics.secondaryRect = {contentRect.x, contentRect.y + primaryExtent + splitterExtent,
                                 contentRect.width, secondaryExtent};
    }
    return UISplitViewLayoutPlan{.metrics = metrics, .paneExtent = paneExtent};
}

} // namespace Tina::UI::Detail
