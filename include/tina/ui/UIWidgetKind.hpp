#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

enum class UIWidgetKind : u8 {
    Root,
    Panel,
    Label,
    Button,
    // M11-C0: shares Button default-action arm/activate/Tab; holds checked bit.
    Checkbox,
};

} // namespace Tina::UI
