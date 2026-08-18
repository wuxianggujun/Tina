#pragma once

#include "UITextPaintEmitter.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIText.hpp>

#include <string_view>

namespace Tina::UI::Detail {

// Result of fitting one logical line into the committed content box.
// visibleText is always a prefix of the source text ending on a grapheme
// cluster boundary, so no cluster is ever split across the ellipsis.
struct UITextTruncationPlan final {
    std::string_view visibleText{};
    bool showEllipsis = false;
};

// Measures through the same rasterizer/face selection that measureWidgetText
// and the paint emitter use. Returns false when the measure fails; callers keep
// the untruncated text instead of guessing a cut.
//
// This agrees with the emitted run whenever the emitter takes its atlas path.
// If the atlas is missing or bails mid-run, the emitter falls back to a
// fixed logicalSize * advanceScale advance per codepoint and the drawn width can
// differ from the planned width; the content-box clip is the backstop there.
[[nodiscard]] bool tryMeasureTextWidth(
    const UITextPaintRasterSource& rasterSource,
    std::string_view utf8,
    const UITextStyle& style,
    float& outWidth) noexcept;

// Pure function of its arguments. The paint snapshot resolves this once while
// counting entries and once while appending them, and both passes must observe
// the same plan, so this must not depend on cached or frame-local state.
//
// intrinsicWidthHint is the committed intrinsic content width when known, or a
// non-positive value when it is not. Because intrinsic width is never smaller
// than the text width, a hint that already fits proves no truncation is needed
// and skips every measure on the common path.
//
// Truncation applies to single logical lines only. Text containing '\n' keeps
// Clip behaviour because an ellipsis has no defined position on a wrapped box.
[[nodiscard]] UITextTruncationPlan resolveTextTruncation(
    const UITextPaintRasterSource& rasterSource,
    std::string_view utf8,
    const UITextStyle& style,
    UITextOverflow overflow,
    float availableWidth,
    float intrinsicWidthHint) noexcept;

} // namespace Tina::UI::Detail
