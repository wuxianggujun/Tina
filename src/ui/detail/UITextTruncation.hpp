#pragma once

#include "UITextPaintEmitter.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIText.hpp>

#include <memory_resource>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

// Result of fitting one logical line into the committed content box.
// visibleText ends on both a grapheme and a shaping-cluster boundary.
struct UITextTruncationPlan final {
    std::string_view visibleText{};
    bool showEllipsis = false;
    bool rightToLeft = false;
};

struct UITextTruncationScratch final {
    explicit UITextTruncationScratch(std::pmr::memory_resource& resource) : clusterEnds(&resource) {}
    std::pmr::vector<usize> clusterEnds;
};

// Reuses owner scratch, but the result depends only on the current arguments.
// Snapshot boundaries before prefix measurements invalidate the shaped view.
// Errors are explicit: never switch to placeholder metrics or silently publish
// untruncated text after a shaping/allocation failure.
//
// intrinsicWidthHint is the committed intrinsic content width when known, or a
// non-positive value when it is not. Because intrinsic width is never smaller
// than the text width, a hint that already fits proves no truncation is needed
// and skips every measure on the common path.
//
// Truncation applies to single logical lines only. Text containing '\n' keeps
// Clip behaviour because an ellipsis has no defined position on a wrapped box.
[[nodiscard]] Core::Result<UITextTruncationPlan> resolveTextTruncation(
    UITextTruncationScratch& scratch,
    const UITextPaintRasterSource& rasterSource,
    std::string_view utf8,
    const UITextStyle& style,
    UITextOverflow overflow,
    float availableWidth,
    float intrinsicWidthHint) noexcept;

} // namespace Tina::UI::Detail
