#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>
#include <limits>
#include <string_view>

namespace Tina::UI {

enum class UITextDirection : u8 { Auto, LeftToRight, RightToLeft };

// Snap the run origin, never individual advances or glyph edges. MSDF keeps
// fractional kerning and dimensions at every DPI and animation scale.
enum class UITextPixelSnap : u8 { None, Baseline, RunOrigin };

struct UITextStyle final {
    float logicalSize = 16.0F;
    float advanceScale = 0.6F;
    float lineHeightScale = 1.2F;
    // Shared authoring color for shaped glyphs and the explicit placeholder.
    // Transparent alpha emits no text paint.
    UIStraightSrgba8Color color{
        .red = 0,
        .green = 0,
        .blue = 0,
        .alpha = 255,
    };
    UITextDirection direction = UITextDirection::Auto;
    UITextPixelSnap pixelSnap = UITextPixelSnap::Baseline;

    auto operator<=>(const UITextStyle&) const = default;
};

// What a single logical line does when the measured text is wider than the
// committed content box. Clip keeps every glyph and relies on the content-box
// clip; Ellipsis drops trailing grapheme clusters and appends
// UITextEllipsisUtf8 so no glyph is cut in half.
//
// Overflow is authoring intent, not a Theme property: it lives beside the text
// instead of inside UITextStyle so re-theming never resets it and so intrinsic
// measure keeps reporting the untruncated size.
enum class UITextOverflow : u8 {
    Clip = 0,
    Ellipsis = 1,
};

// Ordinary intrinsic text either preserves authored lines or wraps against the
// final committed content width. Words prefers ASCII whitespace boundaries and
// hard-wraps long words/CJK without splitting grapheme or shaping clusters.
enum class UITextWrapMode : u8 {
    NoWrap = 0,
    Words = 1,
};

// Optional visual-line limit for ordinary wrapped text. Zero keeps every
// visual line. A positive limit requires UITextWrapMode::Words; when more text
// remains, the final visible line is shortened on a grapheme/shaping boundary and
// ends with UITextEllipsisUtf8. Accessibility continues to expose the full
// authored text.
struct UITextLineClamp final {
    u32 maximumLines = 0;

    [[nodiscard]] constexpr bool enabled() const noexcept
    {
        return maximumLines != 0;
    }

    auto operator<=>(const UITextLineClamp&) const = default;
};

// Content-space constraints, before widget padding/margin. Infinity preserves
// authored lines; zero is a real zero-width constraint (a cluster never splits).
// A line clamp requires Words. NoWrap ignores maximumWidth, not explicit LF.
struct UITextMeasureOptions final {
    float maximumWidth = std::numeric_limits<float>::infinity();
    UITextWrapMode wrapMode = UITextWrapMode::NoWrap;
    UITextLineClamp lineClamp{};

    auto operator<=>(const UITextMeasureOptions&) const = default;
};

// U+2026 HORIZONTAL ELLIPSIS. A face without a visible glyph for it emits no
// paint entry, so truncation degrades to an unmarked hard cut rather than a
// fallback box; the reserved entry count stays a safe over-estimate.
inline constexpr std::string_view UITextEllipsisUtf8{"\xE2\x80\xA6"};

struct UITextContent final {
    std::string_view utf8{};
    UITextStyle style{};

    auto operator<=>(const UITextContent&) const = default;
};

struct UITextMetrics final {
    // Logical advance/line-box dimensions, not the glyph ink bounding box.
    UILogicalSize measuredSize{};
    // Unicode scalar count of the complete input, including LF, even when clamped.
    u32 codepointCount = 0;
    u32 lineCount = 0;

    auto operator<=>(const UITextMetrics&) const = default;
};

// Measures intrinsic text without a width constraint. Explicit '\n' advances
// the line count; constrained Label wrapping is resolved by UIContext layout.
[[nodiscard]] Core::Result<UITextMetrics> measurePlaceholderText(
    std::string_view utf8,
    UITextStyle style) noexcept;

} // namespace Tina::UI
