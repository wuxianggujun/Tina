#pragma once

#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace Tina::UI::Detail {

struct UITextEditCommandPlan final {
    UITextSelection nextSelection{};
    u32 deleteBeginCodepoint = 0;
    u32 deleteEndCodepoint = 0;
    bool deletesText = false;
};

[[nodiscard]] bool containsLineBreak(std::string_view text) noexcept;

// The caller provides strict UTF-8. Offsets beyond the scalar count clamp to
// the end of the byte sequence.
[[nodiscard]] usize utf8ByteOffsetForCodepoint(
    std::string_view text, u32 codepointOffset) noexcept;

[[nodiscard]] u32 textEditCodepointFromHorizontalPosition(
    float relativeX, u32 codepointCount, float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs = {}) noexcept;

[[nodiscard]] std::optional<UITextEditCommandPlan> planTextEditCommand(
    UITextSelection currentSelection, u32 codepointCount,
    UITextEditCommand command, bool extendSelection) noexcept;

} // namespace Tina::UI::Detail
