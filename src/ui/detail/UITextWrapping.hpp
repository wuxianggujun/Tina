#pragma once

#include <tina/ui/UIText.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <span>
#include <string_view>

namespace Tina::UI::Detail {

struct UITextLineCursor final {
    usize byteOffset = 0;
    usize glyphOffset = 0;
    bool skipLeadingWhitespace = false;
    bool trailingEmptyLinePending = false;
};

struct UITextVisualLine final {
    usize byteBegin = 0;
    usize byteEnd = 0;
    usize glyphBegin = 0;
    usize glyphEnd = 0;
    float width = 0.0F;
    bool showEllipsis = false;
};

struct UITextIntrinsicWidths final {
    float minContent = 0.0F;
    float maxContent = 0.0F;

    auto operator<=>(const UITextIntrinsicWidths&) const = default;
};

struct UITextClampedLineCursor final {
    UITextLineCursor wrapped{};
    u32 emittedLineCount = 0;
    bool finished = false;
};

[[nodiscard]] bool nextWrappedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    float fallbackAdvance, std::span<const UITextGlyphRaster> glyphs,
    UITextLineCursor& cursor, UITextVisualLine& line) noexcept;

// Iterates the same visual lines as nextWrappedTextLine, but stops after the
// authored limit. If another visual line remains, the final returned line is
// shortened on a grapheme boundary and marked for an ellipsis run.
[[nodiscard]] bool nextClampedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    UITextLineClamp lineClamp, float fallbackAdvance, float ellipsisAdvance,
    std::span<const UITextGlyphRaster> glyphs,
    UITextClampedLineCursor& cursor, UITextVisualLine& line) noexcept;

[[nodiscard]] UITextMetrics measureWrappedText(
    std::string_view text, const UITextStyle& style, float maximumWidth,
    UITextWrapMode wrapMode, std::span<const UITextGlyphRaster> glyphs,
    u32 codepointCount, UITextLineClamp lineClamp = {},
    float ellipsisAdvance = 0.0F) noexcept;

[[nodiscard]] UITextIntrinsicWidths measureTextIntrinsicWidths(
    std::string_view text, const UITextStyle& style, UITextWrapMode wrapMode,
    std::span<const UITextGlyphRaster> glyphs) noexcept;

} // namespace Tina::UI::Detail
