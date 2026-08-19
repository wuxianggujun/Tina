#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <compare>

namespace Tina::UI {

enum class UIMenuPlacement : u8 {
    Auto = 0,
    Below,
    Above,
    Left,
    Right,
};

struct UIMenuConfig final {
    UIMenuPlacement placement = UIMenuPlacement::Auto;
    float anchorGap = 4.0F;
    float viewportMargin = 8.0F;
    bool matchAnchorWidth = false;
    bool wrapKeyboardNavigation = true;
    bool closeOnActivate = true;

    auto operator<=>(const UIMenuConfig&) const = default;
};

enum class UIMenuItemKind : u8 {
    Command = 0,
    Check,
    Radio,
    Separator,
};

struct UIMenuItemConfig final {
    UIMenuItemKind kind = UIMenuItemKind::Command;
    // Radio items with the same value belong to one exclusive group. Zero is a
    // valid default group and keeps simple menus terse.
    u32 radioGroup = 0;
    bool checked = false;

    auto operator<=>(const UIMenuItemConfig&) const = default;
};

// Geometry and presentation from the last successful atomic UI publication.
struct UIMenuMetrics final {
    UILogicalRect anchorRect{};
    UILogicalRect menuRect{};
    UIMenuPlacement resolvedPlacement = UIMenuPlacement::Below;
    bool open = false;

    auto operator<=>(const UIMenuMetrics&) const = default;
};

enum class UIMenuCommand : u8 {
    Previous = 0,
    Next,
    First,
    Last,
    Dismiss,
};

struct UIMenuCommandResult final {
    bool targeted = false;
    bool consumed = false;
    bool focusChanged = false;
    bool dismissed = false;
    UINodeId menu{};
    UINodeId focus{};
};

} // namespace Tina::UI
