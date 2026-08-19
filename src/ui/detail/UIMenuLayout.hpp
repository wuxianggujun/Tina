#pragma once

#include "UILayoutPrimitives.hpp"

#include <tina/ui/UIMenu.hpp>

#include <algorithm>

namespace Tina::UI::Detail {

struct ResolvedMenuPlacement final {
    UILogicalRect rect{};
    UIMenuPlacement placement = UIMenuPlacement::Below;
};

[[nodiscard]] inline ResolvedMenuPlacement resolveMenuPlacement(
    const UILayoutStyle& layoutStyle, const LayoutScratchState& scratch,
    const UIMenuConfig& config, UILogicalRect anchorRect,
    UILogicalRect viewportRect, LayoutPassStatistics& statistics) noexcept
{
    const float marginX = (std::min)(config.viewportMargin, viewportRect.width * 0.5F);
    const float marginY = (std::min)(config.viewportMargin, viewportRect.height * 0.5F);
    const UILogicalRect innerViewport{
        .x = normalizeFloat(viewportRect.x + marginX),
        .y = normalizeFloat(viewportRect.y + marginY),
        .width = normalizeFloat((std::max)(0.0F, viewportRect.width - marginX * 2.0F)),
        .height = normalizeFloat((std::max)(0.0F, viewportRect.height - marginY * 2.0F)),
    };

    const float requestedWidth = config.matchAnchorWidth
                                     ? anchorRect.width
                                     : scratch.measuredSize.width;
    const float width = (std::min)(
        clampWidth(requestedWidth, layoutStyle, scratch, statistics),
        innerViewport.width);
    const float height = (std::min)(
        clampHeight(scratch.measuredSize.height, layoutStyle, scratch, statistics),
        innerViewport.height);
    const float availableAbove = (std::max)(
        0.0F, anchorRect.y - config.anchorGap - innerViewport.y);
    const float availableBelow = (std::max)(
        0.0F, innerViewport.bottom() - anchorRect.bottom() - config.anchorGap);
    const float availableLeft = (std::max)(
        0.0F, anchorRect.x - config.anchorGap - innerViewport.x);
    const float availableRight = (std::max)(
        0.0F, innerViewport.right() - anchorRect.right() - config.anchorGap);

    const auto availableFor = [&](UIMenuPlacement placement) noexcept {
        switch (placement)
        {
        case UIMenuPlacement::Above: return availableAbove;
        case UIMenuPlacement::Below: return availableBelow;
        case UIMenuPlacement::Left: return availableLeft;
        case UIMenuPlacement::Right: return availableRight;
        case UIMenuPlacement::Auto: break;
        }
        return 0.0F;
    };
    const auto extentFor = [&](UIMenuPlacement placement) noexcept {
        return placement == UIMenuPlacement::Above || placement == UIMenuPlacement::Below
                   ? height
                   : width;
    };
    const auto opposite = [](UIMenuPlacement placement) noexcept {
        switch (placement)
        {
        case UIMenuPlacement::Above: return UIMenuPlacement::Below;
        case UIMenuPlacement::Below: return UIMenuPlacement::Above;
        case UIMenuPlacement::Left: return UIMenuPlacement::Right;
        case UIMenuPlacement::Right: return UIMenuPlacement::Left;
        case UIMenuPlacement::Auto: break;
        }
        return UIMenuPlacement::Below;
    };

    UIMenuPlacement resolved = config.placement;
    if (resolved == UIMenuPlacement::Auto)
    {
        constexpr UIMenuPlacement PreferredOrder[]{
            UIMenuPlacement::Below,
            UIMenuPlacement::Above,
            UIMenuPlacement::Right,
            UIMenuPlacement::Left,
        };
        resolved = PreferredOrder[0];
        bool selectedFitting = availableFor(resolved) >= extentFor(resolved);
        for (const UIMenuPlacement candidate : PreferredOrder)
        {
            const bool fits = availableFor(candidate) >= extentFor(candidate);
            if ((fits && !selectedFitting) ||
                (fits == selectedFitting && availableFor(candidate) > availableFor(resolved)))
            {
                resolved = candidate;
                selectedFitting = fits;
            }
        }
    }
    else
    {
        const UIMenuPlacement flipped = opposite(resolved);
        if (availableFor(resolved) < extentFor(resolved) &&
            availableFor(flipped) > availableFor(resolved))
        {
            resolved = flipped;
        }
    }

    float x = anchorRect.x;
    float y = anchorRect.bottom() + config.anchorGap;
    switch (resolved)
    {
    case UIMenuPlacement::Above:
        y = anchorRect.y - config.anchorGap - height;
        break;
    case UIMenuPlacement::Below:
        break;
    case UIMenuPlacement::Left:
        x = anchorRect.x - config.anchorGap - width;
        y = anchorRect.y;
        break;
    case UIMenuPlacement::Right:
        x = anchorRect.right() + config.anchorGap;
        y = anchorRect.y;
        break;
    case UIMenuPlacement::Auto:
        break;
    }
    const float maximumX = (std::max)(innerViewport.x, innerViewport.right() - width);
    const float maximumY = (std::max)(innerViewport.y, innerViewport.bottom() - height);
    x = (std::clamp)(x, innerViewport.x, maximumX);
    y = (std::clamp)(y, innerViewport.y, maximumY);
    return ResolvedMenuPlacement{
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
