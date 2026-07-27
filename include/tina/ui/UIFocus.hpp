#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Contain keeps keyboard focus traversal inside the node's committed subtree
// once focus enters it. Modal nodes always behave as Contain scopes.
enum class UIFocusScopeMode : u8 {
    None,
    Contain,
};

} // namespace Tina::UI
