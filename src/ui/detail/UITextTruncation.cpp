#include "UITextTruncation.hpp"

#include "UIGraphemeBreak.hpp"

#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] usize countGraphemeClusters(std::string_view utf8) noexcept
{
    usize byteOffset = 0;
    u32 codepointOffset = 0;
    UIGraphemeCluster cluster{};
    usize count = 0;
    while (nextGraphemeCluster(utf8, byteOffset, codepointOffset, cluster))
    {
        ++count;
    }
    return count;
}

// Byte offset ending the first `clusterCount` clusters. Walking from the start
// on every probe keeps the planner allocation-free; the binary search below
// bounds the number of probes to O(log clusters).
[[nodiscard]] usize byteOffsetForClusterCount(std::string_view utf8, usize clusterCount) noexcept
{
    if (clusterCount == 0)
    {
        return 0;
    }
    usize byteOffset = 0;
    u32 codepointOffset = 0;
    UIGraphemeCluster cluster{};
    usize count = 0;
    usize endByte = 0;
    while (count < clusterCount && nextGraphemeCluster(utf8, byteOffset, codepointOffset, cluster))
    {
        endByte = cluster.endByte;
        ++count;
    }
    return endByte;
}

} // namespace

bool tryMeasureTextWidth(
    const UITextPaintRasterSource& rasterSource,
    std::string_view utf8,
    const UITextStyle& style,
    float& outWidth) noexcept
{
    if (utf8.empty())
    {
        outWidth = 0.0F;
        return true;
    }

    if (rasterSource.rasterizer != nullptr && rasterSource.face.hasValue())
    {
        auto metrics = rasterSource.rasterizer->measure(rasterSource.face, utf8, style);
        if (!metrics)
        {
            return false;
        }
        outWidth = metrics->measuredSize.width;
        return std::isfinite(outWidth);
    }

    auto metrics = measurePlaceholderText(utf8, style);
    if (!metrics)
    {
        return false;
    }
    outWidth = metrics->measuredSize.width;
    return std::isfinite(outWidth);
}

UITextTruncationPlan resolveTextTruncation(
    const UITextPaintRasterSource& rasterSource,
    std::string_view utf8,
    const UITextStyle& style,
    UITextOverflow overflow,
    float availableWidth,
    float intrinsicWidthHint) noexcept
{
    const UITextTruncationPlan untruncated{.visibleText = utf8};
    if (overflow != UITextOverflow::Ellipsis || utf8.empty())
    {
        return untruncated;
    }
    if (!(std::isfinite(availableWidth) && availableWidth > 0.0F))
    {
        return untruncated;
    }
    if (utf8.find('\n') != std::string_view::npos)
    {
        return untruncated;
    }

    // Fast path for the common case: the committed intrinsic width already fits,
    // and intrinsic width is never below the text width, so nothing can overflow.
    if (std::isfinite(intrinsicWidthHint) && intrinsicWidthHint > 0.0F &&
        intrinsicWidthHint <= availableWidth)
    {
        return untruncated;
    }

    // A failed measure keeps the untruncated run instead of guessing a cut.
    float fullWidth = 0.0F;
    if (!tryMeasureTextWidth(rasterSource, utf8, style, fullWidth))
    {
        return untruncated;
    }
    if (fullWidth <= availableWidth)
    {
        return untruncated;
    }

    float ellipsisWidth = 0.0F;
    if (!tryMeasureTextWidth(rasterSource, UITextEllipsisUtf8, style, ellipsisWidth))
    {
        return untruncated;
    }

    const float budget = availableWidth - ellipsisWidth;
    if (!std::isfinite(budget) || budget <= 0.0F)
    {
        // Not even the ellipsis fits. Emitting it alone still tells the reader
        // the value is elided, and the content-box clip bounds the overhang.
        return UITextTruncationPlan{.visibleText = std::string_view{}, .showEllipsis = true};
    }

    const usize clusterCount = countGraphemeClusters(utf8);
    if (clusterCount == 0)
    {
        // Only reachable if the cluster walk rejects the text. Treat it like a
        // failed measure rather than eliding everything.
        return untruncated;
    }

    // Largest cluster prefix whose measured width fits the budget. Prefix width
    // is non-decreasing because both measure implementations sum non-negative
    // per-codepoint advances, so the predicate is monotonic and the search is
    // exact. Half-open [low, high) keeps every index non-negative.
    usize low = 0;
    usize high = clusterCount + 1;
    usize bestBytes = 0;
    while (low < high)
    {
        const usize mid = low + (high - low) / 2;
        const usize candidateBytes = byteOffsetForClusterCount(utf8, mid);
        float width = 0.0F;
        if (!tryMeasureTextWidth(rasterSource, utf8.substr(0, candidateBytes), style, width))
        {
            // Same policy as the whole-run measure: never guess a cut.
            return untruncated;
        }
        if (width <= budget)
        {
            bestBytes = candidateBytes;
            low = mid + 1;
            continue;
        }
        high = mid;
    }

    return UITextTruncationPlan{.visibleText = utf8.substr(0, bestBytes), .showEllipsis = true};
}

} // namespace Tina::UI::Detail
