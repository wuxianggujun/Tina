#pragma once

#include "UIGraphemeBreak.hpp"
#include <tina/ui/text/UITextRasterizer.hpp>

namespace Tina::UI::Detail {

// Break only at the union of grapheme and shaping-cluster boundaries. A font
// ligature may span several graphemes, and one grapheme may contain many glyphs.
// text is a slice of the shaped input; both absolute offsets locate that slice.
inline void extendTextShapingCluster(
    std::string_view text, usize absoluteByteBegin, usize scalarBegin,
    std::span<const UITextScalarMetrics> scalars,
    usize& byteOffset, u32& codepointOffset, UIGraphemeCluster& cluster) noexcept
{
    while (cluster.endCodepoint != 0 && scalarBegin <= scalars.size() &&
           cluster.endCodepoint <= scalars.size() - scalarBegin)
    {
        const usize shapedEnd = scalars[scalarBegin + cluster.endCodepoint - 1U].clusterByteEnd;
        if (absoluteByteBegin + cluster.endByte >= shapedEnd || byteOffset >= text.size()) { break; }
        UIGraphemeCluster continuation{};
        if (!nextGraphemeCluster(text, byteOffset, codepointOffset, continuation)) { break; }
        cluster.endByte = continuation.endByte;
        cluster.endCodepoint = continuation.endCodepoint;
    }
}

} // namespace Tina::UI::Detail
