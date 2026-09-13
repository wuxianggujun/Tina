#include "UITextTruncation.hpp"

#include "UITextShapingClusters.hpp"
#include <tina/ui/UIErrors.hpp>

#include <cmath>
#include <new>
#include <stdexcept>

namespace Tina::UI::Detail {
namespace {

Core::Result<float> measureWidth(const UITextPaintRasterSource& source,
                                std::string_view text, const UITextStyle& style)
{
    auto metrics = source.rasterizer != nullptr && source.face
        ? source.rasterizer->measure(source.face, text, style, source.scale)
        : measurePlaceholderText(text, style);
    if (!metrics) { return Core::failure(metrics.error()); }
    if (!std::isfinite(metrics->measuredSize.width) || metrics->measuredSize.width < 0.0F)
    { return Core::failure(UIErrorCode::InvalidText, "Invalid text truncation width"); }
    return metrics->measuredSize.width;
}

} // namespace

Core::Result<UITextTruncationPlan> resolveTextTruncation(
    UITextTruncationScratch& scratch, const UITextPaintRasterSource& source,
    std::string_view text, const UITextStyle& style, UITextOverflow overflow,
    float availableWidth, float intrinsicWidthHint) noexcept
try
{
    const UITextTruncationPlan untruncated{.visibleText = text};
    if (overflow != UITextOverflow::Ellipsis || text.empty() ||
        !std::isfinite(availableWidth) || availableWidth <= 0.0F ||
        text.find('\n') != std::string_view::npos)
    { return untruncated; }
    if (std::isfinite(intrinsicWidthHint) && intrinsicWidthHint > 0.0F &&
        intrinsicWidthHint <= availableWidth)
    { return untruncated; }

    auto fullWidth = measureWidth(source, text, style);
    if (!fullWidth) { return Core::failure(fullWidth.error()); }
    if (*fullWidth <= availableWidth) { return untruncated; }

    bool rightToLeft = style.direction == UITextDirection::RightToLeft;
    std::span<const UITextScalarMetrics> scalars{};
    if (source.rasterizer != nullptr && source.face)
    {
        auto run = source.rasterizer->shape(source.face, text, style);
        if (!run) { return Core::failure(run.error()); }
        scalars = run->scalars;
        if (!scalars.empty()) { rightToLeft = scalars.front().paragraphRightToLeft; }
    }

    scratch.clusterEnds.clear();
    scratch.clusterEnds.push_back(0);
    usize byteOffset = 0;
    u32 codepointOffset = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, codepointOffset, cluster))
    {
        extendTextShapingCluster(text, 0, 0, scalars, byteOffset, codepointOffset, cluster);
        scratch.clusterEnds.push_back(cluster.endByte);
    }

    UITextStyle lineStyle = style;
    lineStyle.direction = rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
    auto markerWidth = measureWidth(source, UITextEllipsisUtf8, lineStyle);
    if (!markerWidth) { return Core::failure(markerWidth.error()); }
    const float budget = availableWidth - *markerWidth;
    if (budget <= 0.0F)
    {
        // The content clip bounds a marker wider than the whole available box.
        return UITextTruncationPlan{.showEllipsis = true, .rightToLeft = rightToLeft};
    }

    // Actual shaped prefix widths, with O(1) boundary lookup. Contextual widths
    // can be non-monotonic, so this is conservative, but every chosen cut fits.
    usize low = 0;
    usize high = scratch.clusterEnds.size();
    usize bestBytes = 0;
    while (low < high)
    {
        const usize mid = low + (high - low) / 2;
        const usize candidateBytes = scratch.clusterEnds[mid];
        auto width = measureWidth(source, text.substr(0, candidateBytes), lineStyle);
        if (!width) { return Core::failure(width.error()); }
        if (*width <= budget)
        {
            bestBytes = candidateBytes;
            low = mid + 1;
        }
        else { high = mid; }
    }
    return UITextTruncationPlan{.visibleText = text.substr(0, bestBytes),
                                .showEllipsis = true, .rightToLeft = rightToLeft};
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Text truncation scratch allocation failed");
}
catch (const std::length_error&)
{
    return Core::failure(UIErrorCode::CapacityExceeded, "Text truncation exceeds addressable storage");
}

} // namespace Tina::UI::Detail
