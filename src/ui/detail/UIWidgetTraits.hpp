#pragma once

#include <tina/ui/UIWidgetKind.hpp>

namespace Tina::UI::Detail {

struct UIWidgetTraits final {
    bool supportsButtonChrome = false;
    bool defaultActivatable = false;
    bool keyboardFocusable = false;
    bool supportsText = false;
};

[[nodiscard]] constexpr UIWidgetTraits widgetTraits(UIWidgetKind kind) noexcept
{
    switch (kind)
    {
    case UIWidgetKind::Button:
    case UIWidgetKind::Dropdown:
    case UIWidgetKind::DropdownItem:
    case UIWidgetKind::ListViewItem:
    case UIWidgetKind::TreeViewItem:
        return {
            .supportsButtonChrome = true,
            .defaultActivatable = true,
            .keyboardFocusable = true,
            .supportsText = true,
        };
    case UIWidgetKind::Checkbox:
        return {
            .defaultActivatable = true,
            .keyboardFocusable = true,
        };
    case UIWidgetKind::RadioButton:
        return {
            .defaultActivatable = true,
            .keyboardFocusable = true,
            .supportsText = true,
        };
    case UIWidgetKind::TextEdit:
        return {
            .keyboardFocusable = true,
            .supportsText = true,
        };
    case UIWidgetKind::ListView:
    case UIWidgetKind::TreeView:
        return {
            .keyboardFocusable = true,
        };
    case UIWidgetKind::Label:
        return {
            .supportsText = true,
        };
    case UIWidgetKind::Root:
    case UIWidgetKind::Panel:
    case UIWidgetKind::Slider:
    case UIWidgetKind::ProgressBar:
    case UIWidgetKind::Modal:
    case UIWidgetKind::ScrollView:
    case UIWidgetKind::Popup:
        return {};
    }
    return {};
}

[[nodiscard]] constexpr bool isButtonChromeKind(UIWidgetKind kind) noexcept
{
    return widgetTraits(kind).supportsButtonChrome;
}

[[nodiscard]] constexpr bool isDefaultActivatableKind(UIWidgetKind kind) noexcept
{
    return widgetTraits(kind).defaultActivatable;
}

[[nodiscard]] constexpr bool isKeyboardFocusableKind(UIWidgetKind kind) noexcept
{
    return widgetTraits(kind).keyboardFocusable;
}

[[nodiscard]] constexpr bool supportsWidgetText(UIWidgetKind kind) noexcept
{
    return widgetTraits(kind).supportsText;
}

} // namespace Tina::UI::Detail
