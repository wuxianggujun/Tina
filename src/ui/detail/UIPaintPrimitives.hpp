#pragma once

#include <tina/ui/UICommittedPaint.hpp>

#include <memory_resource>
#include <optional>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

struct UICommittedLineGeometry final {
    UILogicalRect worldEnvelope{};
    UILogicalPoint worldStart{};
    UILogicalPoint worldEnd{};
};

// Resolves local line geometry into the committed float coordinate space.
// Translation and envelope math use double, then fail closed if the float
// representation would overflow or collapse a non-degenerate line.
[[nodiscard]] std::optional<UICommittedLineGeometry> resolveCommittedLineGeometry(
    const UILineGeometry& line, UILogicalPoint worldOrigin) noexcept;

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
