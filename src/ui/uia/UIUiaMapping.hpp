#pragma once

// Private pure mapping from UIAccessibilityNode to UIA-shaped properties.
// ControlType / ToggleState numeric values match UI Automation constants
// (UIAutomationClient.h) without including COM headers here.

#include <tina/ui/UIAccessibility.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace Tina::UI::Uia {

// UIA ControlTypeId values (MSDN / UIAutomationClient.h).
inline constexpr u32 kControlTypeButton = 50000U;
inline constexpr u32 kControlTypeCalendar = 50001U;
inline constexpr u32 kControlTypeCheckBox = 50002U;
inline constexpr u32 kControlTypeComboBox = 50003U;
inline constexpr u32 kControlTypeEdit = 50004U;
inline constexpr u32 kControlTypeHyperlink = 50005U;
inline constexpr u32 kControlTypeImage = 50006U;
inline constexpr u32 kControlTypeListItem = 50007U;
inline constexpr u32 kControlTypeList = 50008U;
inline constexpr u32 kControlTypeMenu = 50009U;
inline constexpr u32 kControlTypeMenuBar = 50010U;
inline constexpr u32 kControlTypeMenuItem = 50011U;
inline constexpr u32 kControlTypeProgressBar = 50012U;
inline constexpr u32 kControlTypeRadioButton = 50013U;
inline constexpr u32 kControlTypeScrollBar = 50014U;
inline constexpr u32 kControlTypeSlider = 50015U;
inline constexpr u32 kControlTypeSpinner = 50016U;
inline constexpr u32 kControlTypeStatusBar = 50017U;
inline constexpr u32 kControlTypeTab = 50018U;
inline constexpr u32 kControlTypeTabItem = 50019U;
inline constexpr u32 kControlTypeText = 50020U;
inline constexpr u32 kControlTypeToolBar = 50021U;
inline constexpr u32 kControlTypeToolTip = 50022U;
inline constexpr u32 kControlTypeTree = 50023U;
inline constexpr u32 kControlTypeTreeItem = 50024U;
inline constexpr u32 kControlTypeCustom = 50025U;
inline constexpr u32 kControlTypeGroup = 50026U;
inline constexpr u32 kControlTypeThumb = 50027U;
inline constexpr u32 kControlTypeDataGrid = 50028U;
inline constexpr u32 kControlTypeDataItem = 50029U;
inline constexpr u32 kControlTypeDocument = 50030U;
inline constexpr u32 kControlTypeSplitButton = 50031U;
inline constexpr u32 kControlTypeWindow = 50032U;
inline constexpr u32 kControlTypePane = 50033U;
inline constexpr u32 kControlTypeHeader = 50034U;
inline constexpr u32 kControlTypeHeaderItem = 50035U;
inline constexpr u32 kControlTypeTable = 50036U;
inline constexpr u32 kControlTypeTitleBar = 50037U;
inline constexpr u32 kControlTypeSeparator = 50038U;

// ToggleState (UIAutomationClient.h).
inline constexpr u32 kToggleStateOff = 0U;
inline constexpr u32 kToggleStateOn = 1U;
inline constexpr u32 kToggleStateIndeterminate = 2U;

// AutomationLiveSetting values (UIAutomationClient.h).
inline constexpr u32 kLiveSettingOff = 0U;
inline constexpr u32 kLiveSettingPolite = 1U;
inline constexpr u32 kLiveSettingAssertive = 2U;

struct UIUiaRangeValue final {
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    bool isReadOnly = false;
};

struct UIUiaValuePattern final {
    std::string value{};
    bool isReadOnly = false;
};

// In-process mirror of properties a UIA provider would expose for one node.
struct UIUiaMappedNode final {
    UINodeId node{};
    UINodeId parent{};
    u32 controlTypeId = kControlTypeCustom;
    std::string name{};
    std::string description{};
    bool isEnabled = false;
    bool isKeyboardFocusable = false;
    bool hasKeyboardFocus = false;
    bool isSelected = false;
    bool invokeSupported = false;
    u32 liveSetting = kLiveSettingOff;
    std::optional<UIUiaRangeValue> rangeValue{};
    std::optional<u32> toggleState{};
    std::optional<UIUiaValuePattern> value{};
};

[[nodiscard]] constexpr u32 controlTypeFromRole(UISemanticsRole role) noexcept
{
    switch (role)
    {
    case UISemanticsRole::Button:
        return kControlTypeButton;
    case UISemanticsRole::Checkbox:
    case UISemanticsRole::Switch:
        return kControlTypeCheckBox;
    case UISemanticsRole::Slider:
        return kControlTypeSlider;
    case UISemanticsRole::TextEdit:
        return kControlTypeEdit;
    case UISemanticsRole::Label:
        return kControlTypeText;
    case UISemanticsRole::ProgressBar:
        return kControlTypeProgressBar;
    case UISemanticsRole::RadioButton:
        return kControlTypeRadioButton;
    case UISemanticsRole::Dialog:
        return kControlTypeWindow;
    case UISemanticsRole::List:
        return kControlTypeList;
    case UISemanticsRole::ListItem:
        return kControlTypeListItem;
    case UISemanticsRole::ScrollView:
        return kControlTypePane;
    case UISemanticsRole::ComboBox:
        return kControlTypeComboBox;
    case UISemanticsRole::Tree:
        return kControlTypeTree;
    case UISemanticsRole::TreeItem:
        return kControlTypeTreeItem;
    case UISemanticsRole::Image:
        return kControlTypeImage;
    case UISemanticsRole::TabList:
        return kControlTypeTab;
    case UISemanticsRole::Tab:
        return kControlTypeTabItem;
    case UISemanticsRole::TabPanel:
        return kControlTypePane;
    case UISemanticsRole::Menu:
        return kControlTypeMenu;
    case UISemanticsRole::MenuItem:
        return kControlTypeMenuItem;
    case UISemanticsRole::Group:
        return kControlTypeGroup;
    }
    return kControlTypeCustom;
}

[[nodiscard]] inline UIUiaMappedNode mapAccessibilityNode(const UIAccessibilityNode& source)
{
    UIUiaMappedNode mapped{
        .node = source.node,
        .parent = source.parent,
        .controlTypeId = controlTypeFromRole(source.role),
        .name = std::string(source.name),
        .description = std::string(source.description),
        .isEnabled = hasState(source.states, UIAccessibilityState::Enabled),
        .isKeyboardFocusable = false,
        .hasKeyboardFocus = hasState(source.states, UIAccessibilityState::Focused),
        .isSelected = hasState(source.states, UIAccessibilityState::Selected),
        .invokeSupported =
            hasSemanticsAction(source.actions, UISemanticsAction::Activate) &&
            (source.role == UISemanticsRole::Button ||
             source.role == UISemanticsRole::MenuItem),
        .liveSetting = source.liveSetting == UISemanticsLiveSetting::Polite
                           ? kLiveSettingPolite
                       : source.liveSetting == UISemanticsLiveSetting::Assertive
                           ? kLiveSettingAssertive
                           : kLiveSettingOff,
    };

    mapped.isKeyboardFocusable =
        mapped.isEnabled && hasSemanticsAction(source.actions, UISemanticsAction::Focus);
    if (!mapped.isKeyboardFocusable)
    {
        mapped.hasKeyboardFocus = false;
    }

    if (hasState(source.states, UIAccessibilityState::HasRange) || source.role == UISemanticsRole::Slider ||
        source.role == UISemanticsRole::ProgressBar)
    {
        mapped.rangeValue = UIUiaRangeValue{
            .value = static_cast<double>(source.value),
            .minimum = static_cast<double>(source.minValue),
            .maximum = static_cast<double>(source.maxValue),
            .isReadOnly =
                source.role == UISemanticsRole::ProgressBar || hasState(source.states, UIAccessibilityState::ReadOnly),
        };
    }

    if (source.role == UISemanticsRole::Checkbox || source.role == UISemanticsRole::Switch ||
        source.role == UISemanticsRole::RadioButton ||
        (source.role == UISemanticsRole::MenuItem &&
         hasSemanticsAction(source.actions, UISemanticsAction::Toggle)))
    {
        if (hasState(source.states, UIAccessibilityState::Checked))
        {
            mapped.toggleState = kToggleStateOn;
        } else
        {
            mapped.toggleState = kToggleStateOff;
        }
    }

    if (source.role == UISemanticsRole::TextEdit || source.role == UISemanticsRole::Label ||
        source.role == UISemanticsRole::ComboBox)
    {
        std::string valueText = source.valueText.empty() ? std::string(source.name) : std::string(source.valueText);
        mapped.value = UIUiaValuePattern{
            .value = std::move(valueText),
            .isReadOnly = source.role == UISemanticsRole::Label || source.role == UISemanticsRole::ComboBox ||
                          hasState(source.states, UIAccessibilityState::ReadOnly),
        };
    }

    return mapped;
}

} // namespace Tina::UI::Uia
