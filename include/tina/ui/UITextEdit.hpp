#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>

namespace Tina::UI {

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
