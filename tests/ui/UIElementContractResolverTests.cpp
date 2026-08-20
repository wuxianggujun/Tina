#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>

#include "detail/UIElementContractResolver.hpp"

#include <array>
#include <utility>

namespace Tina::Tests {
namespace {

using UI::Detail::BuiltinElementKind;

TEST(UIElementContractResolverTests, PublicRecipesResolveToTheirBuiltinContracts)
{
    const std::array cases{
        std::pair{BuiltinElementKind::Panel, UI::makePanelElement()},
        std::pair{BuiltinElementKind::Label, UI::makeLabelElement("Label")},
        std::pair{BuiltinElementKind::Button, UI::makeButtonElement("Button")},
        std::pair{BuiltinElementKind::Checkbox, UI::makeCheckboxElement()},
        std::pair{BuiltinElementKind::Slider, UI::makeSliderElement()},
        std::pair{BuiltinElementKind::TextEdit, UI::makeTextEditElement("Text")},
        std::pair{BuiltinElementKind::ProgressBar, UI::makeProgressBarElement()},
        std::pair{BuiltinElementKind::RadioButton,
                  UI::makeRadioButtonElement("Radio")},
        std::pair{BuiltinElementKind::Modal, UI::makeModalElement()},
        std::pair{BuiltinElementKind::ScrollView, UI::makeScrollViewElement()},
        std::pair{BuiltinElementKind::Dropdown,
                  UI::makeDropdownElement("Dropdown")},
        std::pair{BuiltinElementKind::Popup, UI::makePopupElement()},
        std::pair{BuiltinElementKind::DropdownItem,
                  UI::makeDropdownItemElement("Item")},
        std::pair{BuiltinElementKind::ListView, UI::makeListViewElement()},
        std::pair{BuiltinElementKind::TreeView, UI::makeTreeViewElement()},
    };

    for (const auto& [expectedKind, descriptor] : cases)
    {
        const auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
        ASSERT_TRUE(kind);
        EXPECT_EQ(*kind, expectedKind);
        EXPECT_EQ(UI::Detail::defaultBehaviorsForKind(expectedKind),
                  descriptor.behaviors);
        EXPECT_EQ(UI::Detail::defaultStyleRoleForKind(expectedKind),
                  descriptor.visual.styleRole);

        const auto semantics = UI::Detail::defaultSemanticsForKind(expectedKind);
        EXPECT_EQ(semantics.mode, descriptor.semantics.mode);
        EXPECT_EQ(semantics.role, descriptor.semantics.role);
        EXPECT_EQ(semantics.actions, descriptor.semantics.actions);
        EXPECT_EQ(semantics.useContentAsName,
                  descriptor.semantics.useContentAsName);
        EXPECT_EQ(semantics.readOnly, descriptor.semantics.readOnly);
    }
}

TEST(UIElementContractResolverTests, InternalKindsPublishStableDefaults)
{
    const auto root =
        UI::Detail::defaultSemanticsForKind(BuiltinElementKind::Root);
    EXPECT_EQ(root.mode, UI::UISemanticsMode::Automatic);
    EXPECT_EQ(UI::Detail::defaultBehaviorsForKind(BuiltinElementKind::Root),
              UI::UIElementBehavior::None);
    EXPECT_EQ(UI::Detail::defaultStyleRoleForKind(BuiltinElementKind::Root),
              UI::UIStyleRoleId::None);

    const std::array internalItems{
        std::pair{BuiltinElementKind::ListViewItem,
                  UI::UIElementBehavior::VirtualListItem},
        std::pair{BuiltinElementKind::TreeViewItem,
                  UI::UIElementBehavior::VirtualTreeItem},
    };
    for (const auto& [kind, virtualBehavior] : internalItems)
    {
        const auto semantics = UI::Detail::defaultSemanticsForKind(kind);
        EXPECT_EQ(UI::Detail::defaultStyleRoleForKind(kind),
                  UI::UIStyleRoleId::CollectionItem);
        EXPECT_TRUE(UI::hasBehavior(
            UI::Detail::defaultBehaviorsForKind(kind), virtualBehavior));
        EXPECT_TRUE(semantics.useContentAsName);
        EXPECT_TRUE(UI::hasSemanticsAction(
            semantics.actions, UI::UISemanticsAction::Activate));
    }
}

TEST(UIElementContractResolverTests, RejectsUnsupportedBehaviorAndContentContracts)
{
    UI::UIElementDescriptor descriptor{};
    descriptor.behaviors = static_cast<UI::UIElementBehavior>(1U << 20U);
    EXPECT_FALSE(UI::Detail::resolveElementBuiltinKind(descriptor));

    descriptor = UI::makeButtonElement("Button");
    descriptor.text.reset();
    auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Panel);

    descriptor = UI::makeSliderElement();
    descriptor.text = "Slider";
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Label);

    descriptor = UI::makePopupElement();
    descriptor.layout.placement = UI::UILayoutPlacement::Flow;
    EXPECT_FALSE(UI::Detail::resolveElementBuiltinKind(descriptor));

    descriptor = UI::makeButtonElement("Mixed");
    descriptor.behaviors |= UI::UIElementBehavior::RangeInput;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Label);
}

TEST(UIElementContractResolverTests, ActivateAndToggleComposeOnGenericPanelAndLabelKinds)
{
    UI::UIElementDescriptor descriptor = UI::makePanelElement();
    descriptor.behaviors = UI::UIElementBehavior::Activate;
    auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Panel);

    descriptor.behaviors = UI::UIElementBehavior::Toggle;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Panel);

    descriptor = UI::makeLabelElement("Toggle label");
    descriptor.behaviors = UI::UIElementBehavior::Focusable |
                           UI::UIElementBehavior::Activate |
                           UI::UIElementBehavior::Toggle;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Label);
}

TEST(UIElementContractResolverTests, ImageContentResolvesOnlyForImageCapableControls)
{
    const UI::UIImageContent image{
        .source = {
            .sourcePixels = {.width = 16, .height = 16},
            .texturePixelExtent = {.width = 16, .height = 16},
            .intrinsicLogicalSize = {.width = 16.0F, .height = 16.0F},
        },
    };

    UI::UIElementDescriptor descriptor = UI::makeButtonElement();
    descriptor.text.reset();
    descriptor.image = image;
    auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind) << kind.error().message;
    EXPECT_EQ(*kind, BuiltinElementKind::Button);

    descriptor = UI::makeRadioButtonElement();
    descriptor.text.reset();
    descriptor.image = image;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind) << kind.error().message;
    EXPECT_EQ(*kind, BuiltinElementKind::RadioButton);

    descriptor = UI::makeSliderElement();
    descriptor.image = image;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_FALSE(kind);
    EXPECT_EQ(kind.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST(UIElementContractResolverTests, RangeInputComposesWithActivateAndToggleWithoutConcreteKindEquality)
{
    UI::UIElementDescriptor descriptor = UI::makePanelElement();
    descriptor.behaviors = UI::UIElementBehavior::RangeInput;
    auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Slider);

    descriptor.behaviors = UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle |
                           UI::UIElementBehavior::RangeInput;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Slider);

    descriptor = UI::makeLabelElement("Range label");
    descriptor.behaviors = UI::UIElementBehavior::Focusable | UI::UIElementBehavior::RangeInput;
    kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_TRUE(kind);
    EXPECT_EQ(*kind, BuiltinElementKind::Label);
}

TEST(UIElementContractResolverTests, MixedTextInputCompositionRemainsRejectedUntilInputRoutingIsCapabilityBased)
{
    UI::UIElementDescriptor descriptor = UI::makeTextEditElement("Input");
    descriptor.behaviors |= UI::UIElementBehavior::Activate;

    const auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_FALSE(kind);
    EXPECT_EQ(kind.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST(UIElementContractResolverTests, MixedScrollCompositionRemainsRejectedUntilRoutingIsCapabilityBased)
{
    UI::UIElementDescriptor descriptor = UI::makeScrollViewElement();
    descriptor.behaviors |= UI::UIElementBehavior::Activate;

    const auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_FALSE(kind);
    EXPECT_EQ(kind.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST(UIElementContractResolverTests, MixedSelectCompositionRemainsRejectedUntilRoutingIsCapabilityBased)
{
    UI::UIElementDescriptor descriptor = UI::makeDropdownElement("Dropdown");
    descriptor.behaviors |= UI::UIElementBehavior::Toggle;

    const auto kind = UI::Detail::resolveElementBuiltinKind(descriptor);
    ASSERT_FALSE(kind);
    EXPECT_EQ(kind.error().code, UI::UIErrorCode::InvalidElementDescriptor);
}

TEST(UIElementContractResolverTests, SemanticsActionsRequireMatchingBehaviors)
{
    const UI::UIElementDescriptor checkbox = UI::makeCheckboxElement();
    EXPECT_TRUE(UI::Detail::validateSemanticsContract(
        checkbox.semantics, checkbox.behaviors));

    UI::UISemanticsDescriptor invalid = checkbox.semantics;
    invalid.role = static_cast<UI::UISemanticsRole>(255);
    EXPECT_FALSE(UI::Detail::validateSemanticsContract(
        invalid, checkbox.behaviors));

    invalid = checkbox.semantics;
    invalid.actions = static_cast<UI::UISemanticsAction>(1U << 7U);
    EXPECT_FALSE(UI::Detail::validateSemanticsContract(
        invalid, checkbox.behaviors));

    invalid = {};
    invalid.actions = UI::UISemanticsAction::SetTextValue;
    EXPECT_FALSE(UI::Detail::validateSemanticsContract(
        invalid, UI::UIElementBehavior::Focusable));

    const UI::UIElementDescriptor radio = UI::makeRadioButtonElement("Radio");
    EXPECT_TRUE(UI::Detail::validateSemanticsContract(
        radio.semantics, radio.behaviors));
}

} // namespace
} // namespace Tina::Tests
