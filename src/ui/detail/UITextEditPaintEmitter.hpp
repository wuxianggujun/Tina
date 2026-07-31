#pragma once

#include "UITextPaintEmitter.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>

#include <memory_resource>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

struct UITextEditPaintState final {
    bool focused = false;
    bool preeditActive = false;
    std::string_view committedText{};
    UITextSelection selection{};
    std::string_view preeditText{};
    u32 preeditCursorCodepoint = 0;
    UITextStyle style{};
    UIPremultipliedRgba8Color textColor{};
    UIPremultipliedRgba8Color selectionColor = premultiply(UITextEditPaint{}.selectionBackgroundColor);
    UIPremultipliedRgba8Color caretColor = premultiply(UITextEditPaint{}.caretColor);
    UITextPaintRasterSource rasterSource{};
};

class UITextEditPaintEmitter final {
  public:
    [[nodiscard]] static usize countEntries(const UITextEditPaintState& state) noexcept;

    static void append(std::pmr::vector<UICommittedPaintEntry>& output,
                       const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                       const UITextEditPaintState& state) noexcept;
};

} // namespace Tina::UI::Detail
