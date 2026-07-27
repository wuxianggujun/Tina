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
    // M11-C1: horizontal value control; pointer drag maps X to [min,max].
    Slider,
    // Single-line UTF-8 editor with caret, selection, and IME composition.
    TextEdit,
    // Horizontal determinate range indicator; not pointer-interactive.
    ProgressBar,
    // Parent-scoped exclusive selection control.
    RadioButton,
    // Overlay container that establishes a focus scope and blocks pointer
    // routing to lower committed content while effectively visible.
    Modal,
    // Clipping container with retained two-axis offset, wheel routing, and
    // interactive scrollbars.
    ScrollView,
};

} // namespace Tina::UI
