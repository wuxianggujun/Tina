#include "UIElementContractResolver.hpp"

#include <tina/ui/UIErrors.hpp>

namespace Tina::UI::Detail {

Core::Result<BuiltinElementKind>
resolveElementBuiltinKind(const UIElementDescriptor& descriptor)
{
    const bool hasText = descriptor.text.has_value();
    const bool hasImage = descriptor.image.has_value();
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
    const auto rejectText = [hasText](BuiltinElementKind kind)
        -> Core::Result<BuiltinElementKind> {
        if (hasText)
        {
            return Core::failure(
                UIErrorCode::InvalidElementDescriptor,
                "UI element behavior does not accept intrinsic text content");
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
    if (hasImage)
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UI image content requires a behavior-neutral Element");
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
    if (specialized == UIElementBehavior::Activate)
    {
        return hasText ? BuiltinElementKind::Button : BuiltinElementKind::Panel;
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
    if (specialized ==
        (UIElementBehavior::Activate | UIElementBehavior::ExclusiveChoice))
    {
        return requireText(BuiltinElementKind::RadioButton);
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
    return Core::failure(
        UIErrorCode::InvalidElementDescriptor,
        "UI element behavior composition has no retained built-in storage contract");
}

Core::Status validateSemanticsContract(const UISemanticsDescriptor& descriptor,
                                       UIElementBehavior behaviors)
{
    if (!isValidSemanticsMode(descriptor.mode) ||
        !isValidSemanticsRole(descriptor.role) ||
        !isValidSemanticsActions(descriptor.actions))
    {
        return Core::failure(
            UIErrorCode::InvalidElementDescriptor,
            "UI element semantics contain an unknown mode, role, or action");
    }
    if ((hasSemanticsAction(descriptor.actions, UISemanticsAction::Focus) &&
         !hasBehavior(behaviors, UIElementBehavior::Focusable)) ||
        (hasSemanticsAction(descriptor.actions, UISemanticsAction::Activate) &&
         !hasBehavior(behaviors, UIElementBehavior::Activate)) ||
        (hasSemanticsAction(descriptor.actions, UISemanticsAction::Toggle) &&
         !hasBehavior(behaviors, UIElementBehavior::Toggle) &&
         !hasBehavior(behaviors, UIElementBehavior::ExclusiveChoice)) ||
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
