#pragma once

#include <tina/ui/UICommittedPaint.hpp>

#include <memory_resource>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

[[nodiscard]] usize countBoxChromePaintEntries(
    const UIBoxPaint& paint, const UILogicalRect& worldRect,
    bool hasResolvedFill) noexcept;
void appendBoxChromePaints(std::pmr::vector<UICommittedPaintEntry>& output,
                           UINodeId node, const UILogicalRect& worldRect,
                           const UILogicalRect& effectiveClip,
                           u32& nextPaintOrdinal, const UIBoxPaint& paint,
                           UIPremultipliedRgba8Color resolvedFill) noexcept;

[[nodiscard]] usize countDrawableTextCodepoints(std::string_view utf8) noexcept;
[[nodiscard]] UIPremultipliedRgba8Color applyOpacity(
    UIPremultipliedRgba8Color color, u8 opacity) noexcept;

} // namespace Tina::UI::Detail
