#pragma once

#include "UIWidgetTraits.hpp"

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIElement.hpp>

namespace Tina::UI::Detail {

struct BuiltinSemanticsDefaults final {
    UISemanticsMode mode = UISemanticsMode::Publish;
    UISemanticsRole role = UISemanticsRole::Group;
    UISemanticsAction actions = UISemanticsAction::None;
    bool useContentAsName = false;
    bool readOnly = false;

    bool operator==(const BuiltinSemanticsDefaults&) const = default;
};

[[nodiscard]] constexpr bool isValidSemanticsMode(UISemanticsMode mode) noexcept
{
    return mode >= UISemanticsMode::Automatic && mode <= UISemanticsMode::Exclude;
}

[[nodiscard]] constexpr bool isValidSemanticsRole(UISemanticsRole role) noexcept
{
    return role >= UISemanticsRole::Group && role <= UISemanticsRole::Image;
}

[[nodiscard]] constexpr bool isValidSemanticsActions(UISemanticsAction actions) noexcept
{
    constexpr u8 AllActions = static_cast<u8>(UISemanticsAction::Focus) |
                              static_cast<u8>(UISemanticsAction::Activate) |
                              static_cast<u8>(UISemanticsAction::Toggle) |
                              static_cast<u8>(UISemanticsAction::SetRangeValue) |
                              static_cast<u8>(UISemanticsAction::SetTextValue);
    return (static_cast<u8>(actions) & static_cast<u8>(~AllActions)) == 0;
}

[[nodiscard]] constexpr bool isValidElementBehaviors(UIElementBehavior behaviors) noexcept
{
    constexpr u32 AllBehaviors = (1U << 16U) - 1U;
    return (static_cast<u32>(behaviors) & ~AllBehaviors) == 0;
}

[[nodiscard]] Core::Result<BuiltinElementKind>
resolveElementBuiltinKind(const UIElementDescriptor& descriptor);

[[nodiscard]] constexpr UIElementBehavior
defaultBehaviorsForKind(BuiltinElementKind kind) noexcept
{
    switch (kind)
    {
    case BuiltinElementKind::Button:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate;
    case BuiltinElementKind::Checkbox:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate |
               UIElementBehavior::Toggle;
    case BuiltinElementKind::Slider:
        return UIElementBehavior::Focusable | UIElementBehavior::RangeInput;
    case BuiltinElementKind::TextEdit:
        return UIElementBehavior::Focusable | UIElementBehavior::TextInput;
    case BuiltinElementKind::ProgressBar:
        return UIElementBehavior::ProgressValue;
    case BuiltinElementKind::RadioButton:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate |
               UIElementBehavior::ExclusiveChoice;
    case BuiltinElementKind::Modal:
        return UIElementBehavior::ModalBarrier;
    case BuiltinElementKind::ScrollView:
        return UIElementBehavior::Scroll;
    case BuiltinElementKind::Dropdown:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate |
               UIElementBehavior::Select;
    case BuiltinElementKind::Popup:
        return UIElementBehavior::Popup;
    case BuiltinElementKind::DropdownItem:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate |
               UIElementBehavior::SelectOption;
    case BuiltinElementKind::ListView:
        return UIElementBehavior::Focusable | UIElementBehavior::VirtualList;
    case BuiltinElementKind::ListViewItem:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate |
               UIElementBehavior::VirtualListItem;
    case BuiltinElementKind::TreeView:
        return UIElementBehavior::Focusable | UIElementBehavior::VirtualTree;
    case BuiltinElementKind::TreeViewItem:
        return UIElementBehavior::Focusable | UIElementBehavior::Activate |
               UIElementBehavior::VirtualTreeItem;
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Panel:
    case BuiltinElementKind::Label:
    case BuiltinElementKind::Tooltip:
        return UIElementBehavior::None;
    }
    return UIElementBehavior::None;
}

[[nodiscard]] constexpr UIStyleRoleId
defaultStyleRoleForKind(BuiltinElementKind kind) noexcept
{
    switch (kind)
    {
    case BuiltinElementKind::Label:
        return UIStyleRoleId::TextBody;
    case BuiltinElementKind::Button:
        return UIStyleRoleId::ButtonTonal;
    case BuiltinElementKind::Checkbox:
        return UIStyleRoleId::Checkbox;
    case BuiltinElementKind::Slider:
        return UIStyleRoleId::Slider;
    case BuiltinElementKind::TextEdit:
        return UIStyleRoleId::TextInput;
    case BuiltinElementKind::ProgressBar:
        return UIStyleRoleId::ProgressBar;
    case BuiltinElementKind::RadioButton:
        return UIStyleRoleId::RadioButton;
    case BuiltinElementKind::Modal:
        return UIStyleRoleId::ModalSurface;
    case BuiltinElementKind::ScrollView:
        return UIStyleRoleId::ScrollView;
    case BuiltinElementKind::Dropdown:
        return UIStyleRoleId::Dropdown;
    case BuiltinElementKind::Popup:
        return UIStyleRoleId::PopupSurface;
    case BuiltinElementKind::DropdownItem:
    case BuiltinElementKind::ListViewItem:
    case BuiltinElementKind::TreeViewItem:
        return UIStyleRoleId::CollectionItem;
    case BuiltinElementKind::ListView:
        return UIStyleRoleId::ListView;
    case BuiltinElementKind::TreeView:
        return UIStyleRoleId::TreeView;
    case BuiltinElementKind::Tooltip:
        return UIStyleRoleId::TooltipSurface;
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Panel:
        return UIStyleRoleId::None;
    }
    return UIStyleRoleId::None;
}

[[nodiscard]] constexpr BuiltinSemanticsDefaults
defaultSemanticsForKind(BuiltinElementKind kind) noexcept
{
    BuiltinSemanticsDefaults defaults{};
    switch (kind)
    {
    case BuiltinElementKind::Root:
    case BuiltinElementKind::Panel:
        defaults.mode = UISemanticsMode::Automatic;
        break;
    case BuiltinElementKind::Tooltip:
        defaults.mode = UISemanticsMode::Exclude;
        break;
    case BuiltinElementKind::Label:
        defaults.role = UISemanticsRole::Label;
        defaults.useContentAsName = true;
        defaults.readOnly = true;
        break;
    case BuiltinElementKind::Button:
        defaults.role = UISemanticsRole::Button;
        defaults.actions = UISemanticsAction::Focus | UISemanticsAction::Activate;
        defaults.useContentAsName = true;
        break;
    case BuiltinElementKind::Checkbox:
        defaults.role = UISemanticsRole::Checkbox;
        defaults.actions = UISemanticsAction::Focus | UISemanticsAction::Activate |
                           UISemanticsAction::Toggle;
        break;
    case BuiltinElementKind::Slider:
        defaults.role = UISemanticsRole::Slider;
        defaults.actions = UISemanticsAction::Focus |
                           UISemanticsAction::SetRangeValue;
        break;
    case BuiltinElementKind::TextEdit:
        defaults.role = UISemanticsRole::TextEdit;
        defaults.actions = UISemanticsAction::Focus |
                           UISemanticsAction::SetTextValue;
        break;
    case BuiltinElementKind::ProgressBar:
        defaults.role = UISemanticsRole::ProgressBar;
        defaults.readOnly = true;
        break;
    case BuiltinElementKind::RadioButton:
        defaults.role = UISemanticsRole::RadioButton;
        defaults.actions = UISemanticsAction::Focus | UISemanticsAction::Activate |
                           UISemanticsAction::Toggle;
        defaults.useContentAsName = true;
        break;
    case BuiltinElementKind::Modal:
        defaults.role = UISemanticsRole::Dialog;
        break;
    case BuiltinElementKind::ScrollView:
        defaults.role = UISemanticsRole::ScrollView;
        break;
    case BuiltinElementKind::Dropdown:
        defaults.role = UISemanticsRole::ComboBox;
        defaults.actions = UISemanticsAction::Focus | UISemanticsAction::Activate;
        defaults.useContentAsName = true;
        break;
    case BuiltinElementKind::Popup:
        defaults.role = UISemanticsRole::List;
        break;
    case BuiltinElementKind::DropdownItem:
    case BuiltinElementKind::ListViewItem:
        defaults.role = UISemanticsRole::ListItem;
        defaults.actions = UISemanticsAction::Focus | UISemanticsAction::Activate;
        defaults.useContentAsName = true;
        break;
    case BuiltinElementKind::ListView:
        defaults.role = UISemanticsRole::List;
        defaults.actions = UISemanticsAction::Focus;
        break;
    case BuiltinElementKind::TreeView:
        defaults.role = UISemanticsRole::Tree;
        defaults.actions = UISemanticsAction::Focus;
        break;
    case BuiltinElementKind::TreeViewItem:
        defaults.role = UISemanticsRole::TreeItem;
        defaults.actions = UISemanticsAction::Focus | UISemanticsAction::Activate;
        defaults.useContentAsName = true;
        break;
    }
    return defaults;
}

[[nodiscard]] Core::Status
validateSemanticsContract(const UISemanticsDescriptor& descriptor,
                          UIElementBehavior behaviors);

} // namespace Tina::UI::Detail
