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
};

[[nodiscard]] bool nextWrappedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    float fallbackAdvance, std::span<const UITextGlyphRaster> glyphs,
    UITextLineCursor& cursor, UITextVisualLine& line) noexcept;

[[nodiscard]] UITextMetrics measureWrappedText(
    std::string_view text, const UITextStyle& style, float maximumWidth,
    UITextWrapMode wrapMode, std::span<const UITextGlyphRaster> glyphs,
    u32 codepointCount) noexcept;

} // namespace Tina::UI::Detail
