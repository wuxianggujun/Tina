#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

struct UITextEditPaint final {
    UIStraightSrgba8Color hoveredBackgroundColor{};
    UIStraightSrgba8Color pressedBackgroundColor{};
    UIStraightSrgba8Color focusedBackgroundColor{};
    UIStraightSrgba8Color disabledBackgroundColor{};
    UIStraightSrgba8Color selectionBackgroundColor{
        .red = 42,
        .green = 112,
        .blue = 190,
        .alpha = 190,
    };
    UIStraightSrgba8Color caretColor{
        .red = 255,
        .green = 255,
        .blue = 255,
        .alpha = 255,
    };

    auto operator<=>(const UITextEditPaint&) const = default;
};

// Selection offsets count Unicode scalar values in the committed UTF-8 text.
// Grapheme-cluster navigation and shaping-aware cursor movement are deferred.
struct UITextSelection final {
    u32 anchorCodepoint = 0;
    u32 caretCodepoint = 0;

    [[nodiscard]] constexpr bool isCollapsed() const noexcept
    {
        return anchorCodepoint == caretCodepoint;
    }

    auto operator<=>(const UITextSelection&) const = default;
};

enum class UITextEditCommand : u8 {
    MoveLeft,
    MoveRight,
    MoveHome,
    MoveEnd,
    Backspace,
    Delete,
    SelectAll,
};

} // namespace Tina::UI
