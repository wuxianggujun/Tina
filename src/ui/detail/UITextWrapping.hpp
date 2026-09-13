#pragma once

#include <tina/ui/UIText.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <span>
#include <string_view>
#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

struct UITextLineCursor final {
    usize byteOffset = 0;
    usize scalarOffset = 0;
    bool skipLeadingWhitespace = false;
    bool trailingEmptyLinePending = false;
};

struct UITextVisualLine final {
    usize byteBegin = 0;
    usize byteEnd = 0;
    usize scalarBegin = 0;
    usize scalarEnd = 0;
    float width = 0.0F;
    bool showEllipsis = false;
    bool rightToLeft = false;
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
    float fallbackAdvance, std::span<const UITextScalarMetrics> scalars,
    UITextLineCursor& cursor, UITextVisualLine& line) noexcept;

// Iterates the same visual lines as nextWrappedTextLine, but stops after the
// authored limit. If another visual line remains, the final returned line is
// shortened on a grapheme/shaping-cluster boundary and marked for an ellipsis run.
[[nodiscard]] bool nextClampedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    UITextLineClamp lineClamp, float fallbackAdvance, float ellipsisAdvance,
    std::span<const UITextScalarMetrics> scalars,
    UITextClampedLineCursor& cursor, UITextVisualLine& line) noexcept;

[[nodiscard]] UITextIntrinsicWidths measureTextIntrinsicWidths(
    std::string_view text, const UITextStyle& style, UITextWrapMode wrapMode,
    std::span<const UITextScalarMetrics> scalars) noexcept;

// Owner-thread reusable line-boundary storage. No fixed line count, no pixel
// generation, and no borrowed shaper span survives a subsequent shaper call.
// Returned lines are invalidated by the next build/measure on this owner.
class UITextLineLayout final {
  public:
    explicit UITextLineLayout(std::pmr::memory_resource& resource) : m_lines(&resource) {}

    [[nodiscard]] Core::Result<std::span<const UITextVisualLine>> build(
        std::string_view text, const UITextStyle& style, IUITextRasterizer* rasterizer,
        UIFontFaceId face, UITextMeasureOptions options,
        UITextIntrinsicWidths* intrinsicWidths = nullptr) noexcept;

    [[nodiscard]] Core::Result<UITextMetrics> measure(
        std::string_view text, const UITextStyle& style, IUITextRasterizer* rasterizer,
        UIFontFaceId face, UITextMeasureOptions options,
        UITextIntrinsicWidths* intrinsicWidths = nullptr) noexcept;

  private:
    std::pmr::vector<UITextVisualLine> m_lines;
    UITextMetrics m_metrics{};
};

} // namespace Tina::UI::Detail
