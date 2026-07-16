#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Tina::UI {

struct UIVisibleRange {
    std::size_t first = 0;
    std::size_t end = 0;

    bool empty() const noexcept { return first >= end; }
    std::size_t count() const noexcept { return end > first ? end - first : 0; }
};

inline UIVisibleRange calculateVisibleRows(std::size_t itemCount,
                                           float rowHeight,
                                           float viewportHeight,
                                           float scrollOffset,
                                           std::size_t overscanRows = 1) noexcept
{
    if (itemCount == 0 || rowHeight <= 0.0f || viewportHeight <= 0.0f) return {};

    const float contentHeight = static_cast<float>(itemCount) * rowHeight;
    const float maxOffset = std::max(0.0f, contentHeight - viewportHeight);
    const float offset = std::clamp(scrollOffset, 0.0f, maxOffset);
    const std::size_t visibleFirst = static_cast<std::size_t>(
        std::floor(offset / rowHeight));
    const std::size_t visibleEnd = std::min(
        itemCount,
        static_cast<std::size_t>(std::ceil((offset + viewportHeight) / rowHeight)));

    UIVisibleRange range;
    range.first = visibleFirst > overscanRows ? visibleFirst - overscanRows : 0;
    range.end = std::min(itemCount, visibleEnd + overscanRows);
    return range;
}

} // namespace Tina::UI
