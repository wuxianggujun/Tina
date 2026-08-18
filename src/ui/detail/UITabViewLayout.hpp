#pragma once

#include <tina/ui/UITabView.hpp>

#include <algorithm>

namespace Tina::UI::Detail {

struct UITabViewRegions final {
    UILogicalRect tabStripRect{};
    UILogicalRect panelRect{};
};

[[nodiscard]] inline UITabViewRegions resolveTabViewRegions(
    UILogicalRect contentRect, UITabViewPlacement placement, float tabStripExtent,
    float contentGap) noexcept
{
    const bool horizontal = placement == UITabViewPlacement::Top ||
                            placement == UITabViewPlacement::Bottom;
    const float availableCross = horizontal ? contentRect.height : contentRect.width;
    const float strip = std::clamp(tabStripExtent, 0.0F, availableCross);
    const float gap = strip > 0.0F
                          ? (std::min)(contentGap, (std::max)(0.0F, availableCross - strip))
                          : 0.0F;
    if (horizontal)
    {
        const float panelHeight = (std::max)(0.0F, contentRect.height - strip - gap);
        if (placement == UITabViewPlacement::Top)
        {
            return {
                .tabStripRect = {contentRect.x, contentRect.y, contentRect.width, strip},
                .panelRect = {contentRect.x, contentRect.y + strip + gap,
                              contentRect.width, panelHeight},
            };
        }
        return {
            .tabStripRect = {contentRect.x, contentRect.bottom() - strip,
                             contentRect.width, strip},
            .panelRect = {contentRect.x, contentRect.y, contentRect.width, panelHeight},
        };
    }

    const float panelWidth = (std::max)(0.0F, contentRect.width - strip - gap);
    if (placement == UITabViewPlacement::Left)
    {
        return {
            .tabStripRect = {contentRect.x, contentRect.y, strip, contentRect.height},
            .panelRect = {contentRect.x + strip + gap, contentRect.y,
                          panelWidth, contentRect.height},
        };
    }
    return {
        .tabStripRect = {contentRect.right() - strip, contentRect.y,
                         strip, contentRect.height},
        .panelRect = {contentRect.x, contentRect.y, panelWidth, contentRect.height},
    };
}

} // namespace Tina::UI::Detail
