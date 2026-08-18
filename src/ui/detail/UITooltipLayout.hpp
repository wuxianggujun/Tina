#pragma once

#include "UILayoutPrimitives.hpp"

#include <tina/ui/UITooltip.hpp>

#include <algorithm>

namespace Tina::UI::Detail {

struct ResolvedTooltipPlacement final {
    UILogicalRect rect{};
    UITooltipPlacement placement = UITooltipPlacement::Below;
};

[[nodiscard]] inline ResolvedTooltipPlacement resolveTooltipPlacement(
    const UILayoutStyle& layoutStyle, const LayoutScratchState& scratch,
    const UITooltipConfig& tooltipStyle, UILogicalRect anchorRect,
    UILogicalRect viewportRect, LayoutPassStatistics& statistics) noexcept
{
    const float marginX = (std::min)(tooltipStyle.viewportMargin,
                                     viewportRect.width * 0.5F);
    const float marginY = (std::min)(tooltipStyle.viewportMargin,
                                     viewportRect.height * 0.5F);
    const UILogicalRect innerViewport{
        .x = normalizeFloat(viewportRect.x + marginX),
        .y = normalizeFloat(viewportRect.y + marginY),
        .width = normalizeFloat((std::max)(0.0F, viewportRect.width - marginX * 2.0F)),
        .height = normalizeFloat((std::max)(0.0F, viewportRect.height - marginY * 2.0F)),
    };

    const float width = (std::min)(
        clampWidth(scratch.measuredSize.width, layoutStyle, scratch, statistics),
        innerViewport.width);
    const float height = (std::min)(
        clampHeight(scratch.measuredSize.height, layoutStyle, scratch, statistics),
        innerViewport.height);
    const float availableAbove = (std::max)(
        0.0F, anchorRect.y - tooltipStyle.anchorGap - innerViewport.y);
    const float availableBelow = (std::max)(
        0.0F, innerViewport.bottom() - anchorRect.bottom() - tooltipStyle.anchorGap);
    const float availableLeft = (std::max)(
        0.0F, anchorRect.x - tooltipStyle.anchorGap - innerViewport.x);
    const float availableRight = (std::max)(
        0.0F, innerViewport.right() - anchorRect.right() - tooltipStyle.anchorGap);

    const auto availableFor = [&](UITooltipPlacement placement) noexcept {
        switch (placement)
        {
        case UITooltipPlacement::Above:
            return availableAbove;
        case UITooltipPlacement::Below:
            return availableBelow;
        case UITooltipPlacement::Left:
            return availableLeft;
        case UITooltipPlacement::Right:
            return availableRight;
        case UITooltipPlacement::Auto:
            break;
        }
        return 0.0F;
    };
    const auto extentFor = [&](UITooltipPlacement placement) noexcept {
        return placement == UITooltipPlacement::Above ||
                       placement == UITooltipPlacement::Below
                   ? height
                   : width;
    };
    const auto opposite = [](UITooltipPlacement placement) noexcept {
        switch (placement)
        {
        case UITooltipPlacement::Above:
            return UITooltipPlacement::Below;
        case UITooltipPlacement::Below:
            return UITooltipPlacement::Above;
        case UITooltipPlacement::Left:
            return UITooltipPlacement::Right;
        case UITooltipPlacement::Right:
            return UITooltipPlacement::Left;
        case UITooltipPlacement::Auto:
            break;
        }
        return UITooltipPlacement::Below;
    };

    UITooltipPlacement resolved = tooltipStyle.placement;
    if (resolved == UITooltipPlacement::Auto)
    {
        constexpr UITooltipPlacement PreferredOrder[]{
            UITooltipPlacement::Below,
            UITooltipPlacement::Above,
            UITooltipPlacement::Right,
            UITooltipPlacement::Left,
        };
        resolved = PreferredOrder[0];
        bool selectedFitting = availableFor(resolved) >= extentFor(resolved);
        for (const UITooltipPlacement candidate : PreferredOrder)
        {
            const bool fits = availableFor(candidate) >= extentFor(candidate);
            if ((fits && !selectedFitting) ||
                (fits == selectedFitting &&
                 availableFor(candidate) > availableFor(resolved)))
            {
                resolved = candidate;
                selectedFitting = fits;
            }
        }
    }
    else
    {
        const UITooltipPlacement flipped = opposite(resolved);
        if (availableFor(resolved) < extentFor(resolved) &&
            availableFor(flipped) > availableFor(resolved))
        {
            resolved = flipped;
        }
    }

    float x = anchorRect.x + (anchorRect.width - width) * 0.5F;
    float y = anchorRect.y + (anchorRect.height - height) * 0.5F;
    switch (resolved)
    {
    case UITooltipPlacement::Above:
        y = anchorRect.y - tooltipStyle.anchorGap - height;
        break;
    case UITooltipPlacement::Below:
        y = anchorRect.bottom() + tooltipStyle.anchorGap;
        break;
    case UITooltipPlacement::Left:
        x = anchorRect.x - tooltipStyle.anchorGap - width;
        break;
    case UITooltipPlacement::Right:
        x = anchorRect.right() + tooltipStyle.anchorGap;
        break;
    case UITooltipPlacement::Auto:
        break;
    }
    const float maximumX = (std::max)(innerViewport.x,
                                      innerViewport.right() - width);
    const float maximumY = (std::max)(innerViewport.y,
                                      innerViewport.bottom() - height);
    x = (std::clamp)(x, innerViewport.x, maximumX);
    y = (std::clamp)(y, innerViewport.y, maximumY);
    return ResolvedTooltipPlacement{
        .rect = {
            .x = normalizeFloat(x),
            .y = normalizeFloat(y),
            .width = normalizeFloat((std::max)(0.0F, width)),
            .height = normalizeFloat((std::max)(0.0F, height)),
        },
        .placement = resolved,
    };
}

} // namespace Tina::UI::Detail
