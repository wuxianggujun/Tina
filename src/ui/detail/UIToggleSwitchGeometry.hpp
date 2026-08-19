#pragma once

#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include <algorithm>

namespace Tina::UI::Detail {

struct UIToggleSwitchGeometry final {
    UILogicalRect thumb{};
    UILogicalCornerRadii thumbCornerRadii{};
};

// Pure committed-geometry projection. The track is the Element box; the thumb
// stays centered on the cross axis and moves only along the logical X axis.
[[nodiscard]] constexpr UIToggleSwitchGeometry resolveToggleSwitchGeometry(
    UILogicalRect track, float inset, bool checked) noexcept
{
    const float availableWidth = (std::max)(0.0F, track.width - inset * 2.0F);
    const float availableHeight = (std::max)(0.0F, track.height - inset * 2.0F);
    const float extent = (std::min)(availableWidth, availableHeight);
    const float x = checked ? track.x + track.width - inset - extent
                            : track.x + inset;
    const float y = track.y + (track.height - extent) * 0.5F;
    return UIToggleSwitchGeometry{
        .thumb = {
            .x = x,
            .y = y,
            .width = extent,
            .height = extent,
        },
        .thumbCornerRadii = UILogicalCornerRadii::uniform(extent * 0.5F),
    };
}

} // namespace Tina::UI::Detail
