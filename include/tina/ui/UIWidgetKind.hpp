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
    // Button-like anchor that owns one Popup child and a single selected item.
    Dropdown,
    // Absolute overlay list. Popup is a focus scope and starts closed.
    Popup,
    // Button-like single-select option owned by a Popup.
    DropdownItem,
    // Fixed-row-height virtualized list backed by a borrowed logical data source.
    ListView,
    // Internal recycled row node owned by a ListView.
    ListViewItem,
    // Fixed-row-height virtualized hierarchy backed by a visible projection.
    TreeView,
    // Internal recycled row node owned by a TreeView.
    TreeViewItem,
};

} // namespace Tina::UI
