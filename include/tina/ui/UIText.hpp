#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIContent.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

// Deterministic monospaced placeholder metrics used by Null/UI tests before a
// FreeType adapter is wired. Raster pixel size and glyph atlas stay out of this
// slice; only logical measure/paint-fallback inputs are exposed.
struct UITextStyle final {
    float logicalSize = 16.0F;
    float advanceScale = 0.6F;
    float lineHeightScale = 1.2F;
    // Authoring color for per-codepoint SolidQuad fallback boxes until FreeType
    // glyph atlas paint is wired. Transparent alpha emits no text paint.
    UIStraightSrgba8Color color{
        .red = 0,
        .green = 0,
        .blue = 0,
        .alpha = 255,
    };

    auto operator<=>(const UITextStyle&) const = default;
};

struct UITextContent final {
    std::string_view utf8{};
    UITextStyle style{};

    auto operator<=>(const UITextContent&) const = default;
};

struct UITextMetrics final {
    UILogicalSize measuredSize{};
    u32 codepointCount = 0;
    u32 lineCount = 0;

    auto operator<=>(const UITextMetrics&) const = default;
};

// Measures a single logical line. Explicit '\n' is allowed and advances the
// line count, but wrapping against a max width is deferred.
[[nodiscard]] Core::Result<UITextMetrics> measurePlaceholderText(
    std::string_view utf8,
    UITextStyle style) noexcept;

} // namespace Tina::UI
