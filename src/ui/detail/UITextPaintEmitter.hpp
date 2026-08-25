#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <memory_resource>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

struct UITextPaintCursor final {
    float x = 0.0F;
    float y = 0.0F;
    float lineHeight = 0.0F;
    float baseX = 0.0F;
};

struct UITextPaintRasterSource final {
    IUITextRasterizer* rasterizer = nullptr;
    UIFontFaceId face{};
    UIGlyphAtlas* atlas = nullptr;
};

class UITextPaintEmitter final {
  public:
    [[nodiscard]] static usize countEntries(
        std::string_view utf8, const UITextStyle& style,
        const UITextPaintRasterSource& rasterSource, float maximumWidth,
        UITextWrapMode wrapMode, UITextLineClamp lineClamp) noexcept;

    static void append(std::pmr::vector<UICommittedPaintEntry>& output,
                       const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal, std::string_view utf8,
                       const UITextStyle& style, UIPremultipliedRgba8Color color, float startX, float startY,
                       const UITextPaintRasterSource& rasterSource, UITextPaintCursor* outCursor,
                       float maximumWidth = 0.0F,
                       UITextWrapMode wrapMode = UITextWrapMode::NoWrap,
                       UITextLineClamp lineClamp = {}) noexcept;
};

} // namespace Tina::UI::Detail
