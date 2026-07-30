#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Contain keeps keyboard focus traversal inside the node's committed subtree
// once focus enters it. Modal nodes always behave as Contain scopes.
enum class UIFocusScopeMode : u8 {
    None,
    Contain,
};

// Logical direction used by committed spatial focus navigation. Selection is
// based on the last published focusable geometry and never wraps at an edge.
enum class UIFocusNavigationDirection : u8 {
    Left,
    Right,
    Up,
    Down,
};

} // namespace Tina::UI
