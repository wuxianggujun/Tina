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
    case BuiltinElementKind::ListView:
    case BuiltinElementKind::TreeView:
        return {
            .keyboardFocusable = true,
        };
    case BuiltinElementKind::Label:
        return {
            .supportsText = true,
        };
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Panel:
    case BuiltinElementKind::Slider:
    case BuiltinElementKind::ProgressBar:
    case BuiltinElementKind::Modal:
    case BuiltinElementKind::ScrollView:
    case BuiltinElementKind::Popup:
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
    if (kind == BuiltinElementKind::TextEdit ||
        kind == BuiltinElementKind::Dropdown ||
        kind == BuiltinElementKind::DropdownItem ||
        kind == BuiltinElementKind::RadioButton ||
        kind == BuiltinElementKind::ListViewItem ||
        kind == BuiltinElementKind::TreeViewItem)
    {
        return {
            .horizontal = UIAxisAlignment::Start,
            .vertical = UIAxisAlignment::Center,
        };
    }
    return {};
}

} // namespace Tina::UI::Detail
