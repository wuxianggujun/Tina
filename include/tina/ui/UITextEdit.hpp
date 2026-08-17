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

enum class UITextEditWrapMode : u8 {
    NoWrap,
    SoftWrap,
};

// Multiline storage and public offsets remain scalar-indexed; editing and hit
// testing align those offsets to grapheme-cluster boundaries.
struct UITextEditMultilineConfig final {
    bool enabled = false;
    UITextEditWrapMode wrapMode = UITextEditWrapMode::NoWrap;
    // Zero preserves the context text-arena limit as the only byte limit.
    usize maximumBytes = 0;
    // Zero preserves the single-line default. When enabled, this is the maximum
    // number of visual rows, including rows created by soft wrapping.
    u32 maximumVisualLines = 0;
    bool verticalScrollEnabled = true;
    float wheelStep = 24.0F;

    auto operator<=>(const UITextEditMultilineConfig&) const = default;
};

// Selection offsets count Unicode scalar values in the committed UTF-8 text.
// Accepted and generated positions align to grapheme-cluster boundaries.
// BiDi- and shaping-aware visual cursor movement remains deferred.
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
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    Backspace,
    Delete,
    SelectAll,
};

} // namespace Tina::UI
