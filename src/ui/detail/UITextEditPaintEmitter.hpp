#pragma once

#include "UITextPaintEmitter.hpp"
#include "UITextEditModel.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>

#include <memory_resource>
#include <optional>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

struct UITextEditPaintState final {
    bool focused = false;
    bool preeditActive = false;
    std::string_view committedText{};
    UITextSelection selection{};
    UITextEditCaretAffinity caretAffinity = UITextEditCaretAffinity::Downstream;
    std::string_view preeditText{};
    u32 preeditCursorCodepoint = 0;
    UITextStyle style{};
    UIPremultipliedRgba8Color textColor{};
    UIPremultipliedRgba8Color selectionColor = premultiply(UITextEditPaint{}.selectionBackgroundColor);
    UIPremultipliedRgba8Color caretColor = premultiply(UITextEditPaint{}.caretColor);
    UITextPaintRasterSource rasterSource{};
    // Single-line overflow policy plus the geometry it is resolved against. Both
    // widths come from the committed content placement, so truncation never
    // re-derives geometry from worldRect + padding. A non-positive
    // availableWidth disables truncation, which keeps unmeasured/zero-size boxes
    // on Clip behaviour; a positive intrinsicWidth that already fits skips the
    // measure work entirely.
    UITextOverflow overflow = UITextOverflow::Clip;
    UITextWrapMode textWrapMode = UITextWrapMode::NoWrap;
    float availableWidth = 0.0F;
    float intrinsicWidth = 0.0F;
    bool multilineEnabled = false;
    UITextEditWrapMode wrapMode = UITextEditWrapMode::NoWrap;
    std::span<const UITextEditVisualLine> visualLines{};
    UITextEditVisualLayout visualLayout{};
    float scrollY = 0.0F;
};

// Geometry emitted for the focused TextEdit caret. UIContext publishes this
// value only after the complete Layout/Hit/Paint/Semantics transaction commits,
// so platform IME placement never observes a half-published UI candidate.
struct UITextEditCaretGeometry final {
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
};

class UITextEditPaintEmitter final {
  public:
    [[nodiscard]] static usize countEntries(const UITextEditPaintState& state) noexcept;

    [[nodiscard]] static std::optional<UITextEditCaretGeometry>
    append(std::pmr::vector<UICommittedPaintEntry>& output,
           const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
           const UITextEditPaintState& state) noexcept;
};

} // namespace Tina::UI::Detail
