#include "UIElementContractResolver.hpp"

#include <tina/ui/UIErrors.hpp>

#include <cmath>

namespace Tina::UI::Detail {

Core::Result<BuiltinElementKind>
resolveElementBuiltinKind(const UIElementDescriptor& descriptor)
{
    const bool hasText = descriptor.text.has_value();
    const bool hasImage = descriptor.image.has_value();

    if (descriptor.menu.has_value() && descriptor.menuItem.has_value())
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI element cannot be both Menu and MenuItem");
    }
    if (descriptor.menu.has_value())
    {
        const UIMenuConfig& config = *descriptor.menu;
        const bool validPlacement = config.placement >= UIMenuPlacement::Auto &&
                                    config.placement <= UIMenuPlacement::Right;
        if (!validPlacement || !std::isfinite(config.anchorGap) ||
            config.anchorGap < 0.0F || !std::isfinite(config.viewportMargin) ||
            config.viewportMargin < 0.0F || hasText || hasImage ||
            descriptor.tooltip.has_value() || descriptor.splitView.has_value() ||
            descriptor.splitter.has_value() || descriptor.tabView.has_value() ||
            descriptor.tab.has_value() || descriptor.behaviors != UIElementBehavior::None ||
            descriptor.layout.placement != UILayoutPlacement::Overlay ||
            descriptor.semantics.mode != UISemanticsMode::Publish ||
            descriptor.semantics.role != UISemanticsRole::Menu ||
            descriptor.semantics.actions != UISemanticsAction::None ||
            (descriptor.pointerHitPolicy.has_value() &&
             *descriptor.pointerHitPolicy != UIPointerHitPolicy::Ignore) ||
            !descriptor.focusScopeMode.has_value() ||
            *descriptor.focusScopeMode != UIFocusScopeMode::Contain)
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI Menu configuration or contract is invalid");
        }
        return BuiltinElementKind::Menu;
    }
    if (descriptor.menuItem.has_value())
    {
        const UIMenuItemConfig& config = *descriptor.menuItem;
        const bool validKind = config.kind >= UIMenuItemKind::Command &&
                               config.kind <= UIMenuItemKind::Submenu;
        if (!validKind)
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI MenuItem kind is invalid");
        }
        const bool separator = config.kind == UIMenuItemKind::Separator;
        const bool check = config.kind == UIMenuItemKind::Check;
        const bool radio = config.kind == UIMenuItemKind::Radio;
        const UIElementBehavior expectedBehaviors =
            separator ? UIElementBehavior::None
                      : UIElementBehavior::Focusable | UIElementBehavior::Activate;
        const UISemanticsAction expectedActions =
            separator ? UISemanticsAction::None
                      : UISemanticsAction::Focus | UISemanticsAction::Activate |
                            ((check || radio) ? UISemanticsAction::Toggle
                                              : UISemanticsAction::None);
        if (hasImage || descriptor.tooltip.has_value() ||
            descriptor.splitView.has_value() || descriptor.splitter.has_value() ||
            descriptor.tabView.has_value() || descriptor.tab.has_value() ||
            descriptor.behaviors != expectedBehaviors ||
            (separator ? hasText : !hasText) ||
            (!check && !radio && config.checked) || (!radio && config.radioGroup != 0U) ||
            descriptor.semantics.mode !=
                (separator ? UISemanticsMode::Exclude : UISemanticsMode::Publish) ||
            (!separator && descriptor.semantics.role != UISemanticsRole::MenuItem) ||
            descriptor.semantics.actions != expectedActions ||
            (!separator && !descriptor.semantics.useContentAsName) ||
            (descriptor.pointerHitPolicy.has_value() &&
             *descriptor.pointerHitPolicy !=
                 (separator ? UIPointerHitPolicy::Ignore
                            : UIPointerHitPolicy::Targetable)) ||
            (descriptor.focusScopeMode.has_value() &&
             *descriptor.focusScopeMode != UIFocusScopeMode::None))
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI MenuItem configuration or contract is invalid");
        }
        return BuiltinElementKind::MenuItem;
    }

    if (descriptor.tabView.has_value() && descriptor.tab.has_value())
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI element cannot be both TabView and Tab");
    }
    if (descriptor.tabView.has_value())
    {
        const UITabViewConfig& config = *descriptor.tabView;
        const bool validPlacement = config.placement >= UITabViewPlacement::Top &&
                                    config.placement <= UITabViewPlacement::Right;
        const bool validActivation = config.activationMode >= UITabActivationMode::Automatic &&
                                     config.activationMode <= UITabActivationMode::Manual;
        if (!validPlacement || !validActivation || !std::isfinite(config.tabGap) || config.tabGap < 0.0F ||
            !std::isfinite(config.contentGap) || config.contentGap < 0.0F || hasText || hasImage ||
            descriptor.tooltip.has_value() || descriptor.splitView.has_value() ||
            descriptor.splitter.has_value() || descriptor.tab.has_value() ||
            descriptor.behaviors != UIElementBehavior::None ||
            descriptor.semantics.mode != UISemanticsMode::Publish ||
            descriptor.semantics.role != UISemanticsRole::TabList ||
            descriptor.semantics.actions != UISemanticsAction::None ||
            (descriptor.pointerHitPolicy.has_value() &&
             *descriptor.pointerHitPolicy != UIPointerHitPolicy::Ignore) ||
            (descriptor.focusScopeMode.has_value() &&
             *descriptor.focusScopeMode != UIFocusScopeMode::None))
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI TabView configuration or contract is invalid");
        }
        return BuiltinElementKind::TabView;
    }
    if (descriptor.tab.has_value())
    {
        constexpr UIElementBehavior RequiredBehaviors =
            UIElementBehavior::Focusable | UIElementBehavior::Activate;
        constexpr UISemanticsAction RequiredActions =
            UISemanticsAction::Focus | UISemanticsAction::Activate;
        if (!hasText || hasImage || descriptor.tooltip.has_value() ||
            descriptor.splitView.has_value() || descriptor.splitter.has_value() ||
            descriptor.behaviors != RequiredBehaviors ||
            descriptor.semantics.mode != UISemanticsMode::Publish ||
            descriptor.semantics.role != UISemanticsRole::Tab ||
            descriptor.semantics.actions != RequiredActions ||
            !descriptor.semantics.useContentAsName ||
            (descriptor.pointerHitPolicy.has_value() &&
             *descriptor.pointerHitPolicy != UIPointerHitPolicy::Targetable) ||
            (descriptor.focusScopeMode.has_value() &&
             *descriptor.focusScopeMode != UIFocusScopeMode::None))
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI Tab configuration or contract is invalid");
        }
        return BuiltinElementKind::Tab;
    }

    if (descriptor.splitView.has_value() && descriptor.splitter.has_value())
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI element cannot be both SplitView and Splitter");
    }
    if (descriptor.splitView.has_value())
    {
        const UISplitViewConfig& config = *descriptor.splitView;
        const bool validOrientation =
            config.orientation >= UISplitViewOrientation::Horizontal &&
            config.orientation <= UISplitViewOrientation::Vertical;
        if (!validOrientation || !std::isfinite(config.initialFraction) ||
            config.initialFraction < 0.0F || config.initialFraction > 1.0F ||
            !std::isfinite(config.minPrimarySize) || config.minPrimarySize < 0.0F ||
            !std::isfinite(config.minSecondarySize) || config.minSecondarySize < 0.0F ||
            !std::isfinite(config.splitterExtent) || config.splitterExtent < 0.0F ||
            hasText || hasImage || descriptor.tooltip.has_value() ||
            descriptor.behaviors != UIElementBehavior::None ||
            descriptor.semantics.actions != UISemanticsAction::None)
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI SplitView configuration or contract is invalid");
        }
        return BuiltinElementKind::SplitView;
    }
    if (descriptor.splitter.has_value())
    {
        const UISplitterConfig& config = *descriptor.splitter;
        constexpr UIElementBehavior RequiredBehaviors =
            UIElementBehavior::Focusable | UIElementBehavior::RangeInput;
        if (!std::isfinite(config.keyboardStep) || !(config.keyboardStep > 0.0F) ||
            config.keyboardStep > 1.0F || hasText || hasImage ||
            descriptor.tooltip.has_value() || descriptor.behaviors != RequiredBehaviors ||
            descriptor.semantics.role != UISemanticsRole::Slider ||
            descriptor.semantics.actions !=
                (UISemanticsAction::Focus | UISemanticsAction::SetRangeValue))
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI Splitter configuration or contract is invalid");
        }
        return BuiltinElementKind::Splitter;
    }

    if (descriptor.tooltip.has_value())
    {
        const UITooltipConfig& tooltip = *descriptor.tooltip;
        constexpr u8 AllTooltipTriggers =
            static_cast<u8>(UITooltipTrigger::PointerHover) |
            static_cast<u8>(UITooltipTrigger::KeyboardFocus) |
            static_cast<u8>(UITooltipTrigger::Manual);
        const bool validPlacement =
            tooltip.placement >= UITooltipPlacement::Auto &&
            tooltip.placement <= UITooltipPlacement::Right;
        const bool validTriggers =
            (static_cast<u8>(tooltip.triggers) & static_cast<u8>(~AllTooltipTriggers)) == 0;
        const bool validDelays = std::isfinite(tooltip.initialDelay.count()) &&
                                 std::isfinite(tooltip.reshowDelay.count()) &&
                                 std::isfinite(tooltip.dismissDelay.count()) &&
                                 tooltip.initialDelay.count() >= 0.0 &&
                                 tooltip.reshowDelay.count() >= 0.0 &&
                                 tooltip.dismissDelay.count() >= 0.0;
        if (!validPlacement || !validTriggers || !validDelays ||
            !std::isfinite(tooltip.anchorGap) || tooltip.anchorGap < 0.0F ||
            !std::isfinite(tooltip.viewportMargin) || tooltip.viewportMargin < 0.0F)
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI Tooltip configuration is invalid");
        }
        if (!hasText || hasImage || descriptor.behaviors != UIElementBehavior::None ||
            descriptor.layout.placement != UILayoutPlacement::Overlay ||
            descriptor.semantics.mode != UISemanticsMode::Exclude ||
            descriptor.semantics.actions != UISemanticsAction::None ||
            (descriptor.pointerHitPolicy.has_value() &&
             *descriptor.pointerHitPolicy != UIPointerHitPolicy::Ignore) ||
            (descriptor.focusScopeMode.has_value() &&
             *descriptor.focusScopeMode != UIFocusScopeMode::None))
        {
            return Core::failure(
                UIErrorCode::InvalidElementDescriptor,
                "UI Tooltip requires text, Overlay placement, Ignore hit policy, excluded semantics, and no behavior or focus scope");
        }
        return BuiltinElementKind::Tooltip;
    }
    const auto requireText = [hasText](BuiltinElementKind kind)
        -> Core::Result<BuiltinElementKind> {
        if (!hasText)
        {
            return Core::failure(
                UIErrorCode::InvalidElementDescriptor,
                "UI element behavior requires intrinsic text content");
        }
        return kind;
    };
    const auto rejectIntrinsicContent = [hasText, hasImage](BuiltinElementKind kind)
        -> Core::Result<BuiltinElementKind> {
        if (hasText || hasImage)
        {
            return Core::failure(
                UIErrorCode::InvalidElementDescriptor,
                "UI element behavior does not accept intrinsic content");
        }
        return kind;
    };

    if (!isValidElementBehaviors(descriptor.behaviors))
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UI element behaviors contain an unknown capability");
    }

    const auto specialized = static_cast<UIElementBehavior>(
        static_cast<u32>(descriptor.behaviors) &
        ~static_cast<u32>(UIElementBehavior::Focusable));
    if (specialized == UIElementBehavior::None)
    {
        return hasText ? BuiltinElementKind::Label : BuiltinElementKind::Panel;
    }
    if (specialized == UIElementBehavior::Activate)
    {
        return hasText || hasImage ? BuiltinElementKind::Button
                                   : BuiltinElementKind::Panel;
    }
    if (specialized ==
        (UIElementBehavior::Activate | UIElementBehavior::ExclusiveChoice))
    {
        return hasText || hasImage
                   ? Core::Result<BuiltinElementKind>{BuiltinElementKind::RadioButton}
                   : requireText(BuiltinElementKind::RadioButton);
    }
    if (hasImage)
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UI image content is only supported by behavior-neutral, Button, and RadioButton Elements");
    }
    constexpr auto ComposableBehaviors = static_cast<UIElementBehavior>(
        static_cast<u32>(UIElementBehavior::Activate) |
        static_cast<u32>(UIElementBehavior::Toggle) |
        static_cast<u32>(UIElementBehavior::RangeInput));
    const auto nonComposable = static_cast<UIElementBehavior>(
        static_cast<u32>(specialized) & ~static_cast<u32>(ComposableBehaviors));
    if (nonComposable == UIElementBehavior::None &&
        hasBehavior(specialized, UIElementBehavior::RangeInput))
    {
        return hasText ? BuiltinElementKind::Label : BuiltinElementKind::Slider;
    }
    if (specialized ==
        (UIElementBehavior::Activate | UIElementBehavior::Toggle))
    {
        return hasText ? BuiltinElementKind::Label
                       : rejectIntrinsicContent(BuiltinElementKind::Checkbox);
    }
    if (specialized == UIElementBehavior::Toggle)
    {
        return hasText ? BuiltinElementKind::Label : BuiltinElementKind::Panel;
    }
    if (specialized == UIElementBehavior::TextInput)
    {
        return requireText(BuiltinElementKind::TextEdit);
    }
    if (specialized == UIElementBehavior::ProgressValue)
    {
        return rejectIntrinsicContent(BuiltinElementKind::ProgressBar);
    }
    if (specialized == UIElementBehavior::ModalBarrier)
    {
        return rejectIntrinsicContent(BuiltinElementKind::Modal);
    }
    if (specialized == UIElementBehavior::Scroll)
    {
        return rejectIntrinsicContent(BuiltinElementKind::ScrollView);
    }
    if (specialized ==
        (UIElementBehavior::Activate | UIElementBehavior::Select))
    {
        return requireText(BuiltinElementKind::Dropdown);
    }
    if (specialized == UIElementBehavior::Popup)
    {
        if (descriptor.layout.placement != UILayoutPlacement::Overlay)
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI Popup elements require Overlay placement");
        }
        return rejectIntrinsicContent(BuiltinElementKind::Popup);
    }
    if (specialized ==
        (UIElementBehavior::Activate | UIElementBehavior::SelectOption))
    {
        return requireText(BuiltinElementKind::DropdownItem);
    }
    if (specialized == UIElementBehavior::VirtualList)
    {
        return rejectIntrinsicContent(BuiltinElementKind::ListView);
    }
    if (specialized == UIElementBehavior::VirtualTree)
    {
        return rejectIntrinsicContent(BuiltinElementKind::TreeView);
    }
    if (specialized == UIElementBehavior::VirtualGrid)
    {
        return rejectIntrinsicContent(BuiltinElementKind::VirtualGridView);
    }
    if (specialized == UIElementBehavior::DataGrid)
    {
        return rejectIntrinsicContent(BuiltinElementKind::DataGrid);
    }
    return Core::failure(
        UIErrorCode::InvalidElementDescriptor,
        "UI element behavior composition has no retained built-in storage contract");
}

Core::Status validateSemanticsContract(const UISemanticsDescriptor& descriptor,
                                       UIElementBehavior behaviors,
                                       BuiltinElementKind kind)
{
    if (!isValidSemanticsMode(descriptor.mode) ||
        !isValidSemanticsRole(descriptor.role) ||
        !isValidSemanticsLiveSetting(descriptor.liveSetting) ||
        !isValidSemanticsActions(descriptor.actions))
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UI element semantics contain an unknown mode, role, live setting, or action");
    }
    if ((hasSemanticsAction(descriptor.actions, UISemanticsAction::Focus) &&
         !hasBehavior(behaviors, UIElementBehavior::Focusable)) ||
        (hasSemanticsAction(descriptor.actions, UISemanticsAction::Activate) &&
         !hasBehavior(behaviors, UIElementBehavior::Activate)) ||
        (hasSemanticsAction(descriptor.actions, UISemanticsAction::Toggle) &&
         !hasBehavior(behaviors, UIElementBehavior::Toggle) &&
         !hasBehavior(behaviors, UIElementBehavior::ExclusiveChoice) &&
         kind != BuiltinElementKind::MenuItem) ||
        (hasSemanticsAction(descriptor.actions,
                            UISemanticsAction::SetRangeValue) &&
         !hasBehavior(behaviors, UIElementBehavior::RangeInput)) ||
        (hasSemanticsAction(descriptor.actions,
                            UISemanticsAction::SetTextValue) &&
         !hasBehavior(behaviors, UIElementBehavior::TextInput)))
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UI element semantics actions require matching behavior capabilities");
    }
    return Core::success();
}

} // namespace Tina::UI::Detail
