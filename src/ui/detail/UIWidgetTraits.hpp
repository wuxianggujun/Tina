#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIContent.hpp>

namespace Tina::UI::Detail {

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
