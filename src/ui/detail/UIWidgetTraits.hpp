#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIContent.hpp>

namespace Tina::UI::Detail {

// Paint layer, in ascending order. Paint order is tree preorder within a layer,
// then layer between them; there is no per-node z-index, because both the hit
// and semantics snapshots resolve ancestry against already-emitted entries and
// would break if a child could outrank its own parent. Promotion therefore
// always moves whole subtrees: a node's layer is max(parentLayer, ownLayer).
//
// Modal is a real layer rather than plain flow because modality is enforced
// through the hit snapshot. A Modal authored before its siblings used to paint
// under chrome that was already non-interactive, which looked like a rendering
// bug and had no diagnostic.
enum class UIPaintLayer : u8 {
    Content = 0,
    Modal,
    Popup,
    Tooltip,
};

inline constexpr u8 UIPaintLayerCount = 4;

[[nodiscard]] constexpr UIPaintLayer combinePaintLayer(
    UIPaintLayer parent, UIPaintLayer local) noexcept
{
    return parent > local ? parent : local;
}

// Private dispatch tag for retained built-in side storage. Public authoring
// identifies capabilities through UIElementDescriptor instead.
enum class BuiltinElementKind : u8 {
    Root,
    Panel,
    Label,
    Button,
    Checkbox,
    Slider,
    TextEdit,
    ProgressBar,
    RadioButton,
    Modal,
    ScrollView,
    Dropdown,
    Popup,
    DropdownItem,
    ListView,
    ListViewItem,
    TreeView,
    TreeViewItem,
    VirtualGridView,
    VirtualGridViewItem,
    DataGrid,
    DataGridRow,
    DataGridCell,
    DataGridColumnHeader,
    Tooltip,
    SplitView,
    Splitter,
    TabView,
    Tab,
    Menu,
    MenuItem,
};

struct UIWidgetTraits final {
    bool supportsButtonChrome = false;
    bool defaultActivatable = false;
    bool keyboardFocusable = false;
    bool supportsText = false;
};

[[nodiscard]] constexpr UIWidgetTraits widgetTraits(BuiltinElementKind kind) noexcept
{
    switch (kind)
    {
    case BuiltinElementKind::Button:
    case BuiltinElementKind::Dropdown:
    case BuiltinElementKind::DropdownItem:
    case BuiltinElementKind::ListViewItem:
    case BuiltinElementKind::TreeViewItem:
    case BuiltinElementKind::VirtualGridViewItem:
    case BuiltinElementKind::DataGridCell:
    case BuiltinElementKind::Tab:
    case BuiltinElementKind::MenuItem:
        return {
            .supportsButtonChrome = true,
            .defaultActivatable = true,
            .keyboardFocusable = true,
            .supportsText = true,
        };
    case BuiltinElementKind::Checkbox:
        return {
            .defaultActivatable = true,
            .keyboardFocusable = true,
        };
    case BuiltinElementKind::RadioButton:
        return {
            .defaultActivatable = true,
            .keyboardFocusable = true,
            .supportsText = true,
        };
    case BuiltinElementKind::TextEdit:
        return {
            .keyboardFocusable = true,
            .supportsText = true,
        };
    case BuiltinElementKind::Slider:
    case BuiltinElementKind::Splitter:
        return {
            .keyboardFocusable = true,
        };
    case BuiltinElementKind::ListView:
    case BuiltinElementKind::TreeView:
    case BuiltinElementKind::VirtualGridView:
    case BuiltinElementKind::DataGrid:
        return {
            .keyboardFocusable = true,
        };
    case BuiltinElementKind::Label:
    case BuiltinElementKind::Tooltip:
    case BuiltinElementKind::DataGridColumnHeader:
        return {
            .supportsText = true,
        };
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Panel:
    case BuiltinElementKind::ProgressBar:
    case BuiltinElementKind::Modal:
    case BuiltinElementKind::ScrollView:
    case BuiltinElementKind::Popup:
    case BuiltinElementKind::SplitView:
    case BuiltinElementKind::TabView:
    case BuiltinElementKind::Menu:
    case BuiltinElementKind::DataGridRow:
        return {};
    }
    return {};
}

// Layer a kind establishes for itself and its subtree. Ordinary content stays in
// Content; only these four kinds promote. Menu shares Popup because an open menu
// and an open dropdown occupy the same tier; ordering within the active menu
// chain is a separate intra-layer concern a flat layer cannot express.
[[nodiscard]] constexpr UIPaintLayer paintLayerForKind(BuiltinElementKind kind) noexcept
{
    switch (kind)
    {
    case BuiltinElementKind::Modal:
        return UIPaintLayer::Modal;
    case BuiltinElementKind::Popup:
    case BuiltinElementKind::Menu:
        return UIPaintLayer::Popup;
    case BuiltinElementKind::Tooltip:
        return UIPaintLayer::Tooltip;
    default:
        return UIPaintLayer::Content;
    }
}

[[nodiscard]] constexpr bool isButtonChromeKind(BuiltinElementKind kind) noexcept
{
    return widgetTraits(kind).supportsButtonChrome;
}

[[nodiscard]] constexpr bool isDefaultActivatableKind(BuiltinElementKind kind) noexcept
{
    return widgetTraits(kind).defaultActivatable;
}

[[nodiscard]] constexpr bool isKeyboardFocusableKind(BuiltinElementKind kind) noexcept
{
    return widgetTraits(kind).keyboardFocusable;
}

[[nodiscard]] constexpr bool supportsWidgetText(BuiltinElementKind kind) noexcept
{
    return widgetTraits(kind).supportsText;
}

[[nodiscard]] constexpr bool hasCompositeManagedLayoutVisibility(
    BuiltinElementKind kind) noexcept
{
    return kind == BuiltinElementKind::ListViewItem ||
           kind == BuiltinElementKind::TreeViewItem ||
           kind == BuiltinElementKind::VirtualGridViewItem ||
           kind == BuiltinElementKind::DataGridRow ||
           kind == BuiltinElementKind::DataGridCell ||
           kind == BuiltinElementKind::DataGridColumnHeader;
}

[[nodiscard]] constexpr UIContentAlignment
defaultContentAlignment(BuiltinElementKind kind) noexcept
{
    if (kind == BuiltinElementKind::Button)
    {
        return {
            .horizontal = UIAxisAlignment::Center,
            .vertical = UIAxisAlignment::Center,
        };
    }
    if (kind == BuiltinElementKind::Tab)
    {
        return {
            .horizontal = UIAxisAlignment::Center,
            .vertical = UIAxisAlignment::Center,
        };
    }
    if (kind == BuiltinElementKind::TextEdit ||
        kind == BuiltinElementKind::Tooltip ||
        kind == BuiltinElementKind::Dropdown ||
        kind == BuiltinElementKind::DropdownItem ||
        kind == BuiltinElementKind::RadioButton ||
        kind == BuiltinElementKind::ListViewItem ||
        kind == BuiltinElementKind::TreeViewItem ||
        kind == BuiltinElementKind::VirtualGridViewItem ||
        kind == BuiltinElementKind::DataGridCell ||
        kind == BuiltinElementKind::DataGridColumnHeader ||
        kind == BuiltinElementKind::MenuItem)
    {
        return {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        };
    }
    return {};
}

} // namespace Tina::UI::Detail
