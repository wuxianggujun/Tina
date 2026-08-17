#pragma once

#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::UI::Detail {

enum class UITextEditCaretAffinity : u8 {
    Downstream,
    Upstream,
};

struct UITextEditCommandPlan final {
    UITextSelection nextSelection{};
    u32 deleteBeginCodepoint = 0;
    u32 deleteEndCodepoint = 0;
    std::optional<float> updatedPreferredX{};
    UITextEditCaretAffinity nextCaretAffinity = UITextEditCaretAffinity::Downstream;
    bool deletesText = false;
};
struct UITextEditVisualLine final {
    u32 beginCodepoint = 0;
    u32 endCodepoint = 0;
    // Glyph records omit LF, so visual consumers must not derive this from a
    // scalar offset when rows follow a hard line break.
    u32 beginGlyphIndex = 0;
    // The scalar index of the LF that ended this row, or InvalidCodepoint.
    u32 hardBreakCodepoint = (std::numeric_limits<u32>::max)();
    float width = 0.0F;
    float top = 0.0F;
};

struct UITextEditVisualLayout final {
    u32 lineCount = 0;
    float lineHeight = 0.0F;
    float contentHeight = 0.0F;
    float maximumScrollY = 0.0F;
};

struct UITextEditVisualHit final {
    u32 codepoint = 0;
    UITextEditCaretAffinity affinity = UITextEditCaretAffinity::Downstream;
};

// Builds a fixed-capacity visual row layout. The output span is caller-owned;
// false means invalid geometry, invalid UTF-8 accounting, or capacity exhaustion
// and leaves no partially published layout for callers to consume.
[[nodiscard]] bool buildTextEditVisualLayout(
    std::string_view text, float viewportWidth, float viewportHeight, float lineHeight,
    float fallbackAdvance, UITextEditWrapMode wrapMode,
    std::span<const UITextGlyphRaster> glyphs, std::span<UITextEditVisualLine> output,
    UITextEditVisualLayout& result) noexcept;

[[nodiscard]] UITextEditVisualHit textEditHitFromVisualPosition(
    std::string_view text, float relativeX, float relativeY, float scrollY,
    const UITextEditVisualLayout& layout,
    std::span<const UITextEditVisualLine> lines, float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs = {}) noexcept;

[[nodiscard]] bool isTextEditSoftWrapBoundary(
    std::span<const UITextEditVisualLine> lines, usize lineIndex,
    u32 codepoint) noexcept;

[[nodiscard]] bool containsLineBreak(std::string_view text) noexcept;

// The caller provides strict UTF-8. Offsets beyond the scalar count clamp to
// the end of the byte sequence.
[[nodiscard]] usize utf8ByteOffsetForCodepoint(
    std::string_view text, u32 codepointOffset) noexcept;

[[nodiscard]] u32 textEditCodepointFromHorizontalPosition(
    std::string_view text, float relativeX, float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs = {}) noexcept;

[[nodiscard]] std::optional<UITextEditCommandPlan> planTextEditVisualCommand(
    std::string_view text, UITextSelection currentSelection, UITextEditCommand command,
    bool extendSelection,
    std::span<const UITextEditVisualLine> lines, UITextEditCaretAffinity caretAffinity,
    std::optional<float> preferredX,
    float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs = {}) noexcept;

[[nodiscard]] std::optional<UITextEditCommandPlan> planTextEditCommand(
    std::string_view text, UITextSelection currentSelection,
    UITextEditCommand command, bool extendSelection) noexcept;

} // namespace Tina::UI::Detail
