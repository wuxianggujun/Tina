#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <tina/ui/UIAccessibility.hpp>

#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 32,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 32,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.authoring().rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.authoring().treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

TEST(UIAccessibilityTest, ProbeReadsRolesNamesAndStatesFromCommittedSemantics)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());

    auto button = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    auto checkbox = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeCheckboxElement());
    auto slider = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeSliderElement());
    auto progress = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeProgressBarElement());
    auto radio = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeRadioButtonElement());
    auto textEdit = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement());
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(slider.has_value());
    ASSERT_TRUE(progress.has_value());
    ASSERT_TRUE(radio.has_value());
    ASSERT_TRUE(textEdit.has_value());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(400.0F, 300.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*checkbox, fixedSize(24.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*slider, fixedSize(120.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(*progress, fixedSize(120.0F, 12.0F)));
    assertOk(updater.setLayoutStyle(*radio, fixedSize(100.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*textEdit, fixedSize(160.0F, 32.0F)));
    assertOk(updater.setText(*button, "Apply"));
    assertOk(updater.setChecked(*checkbox, true));
    assertOk(updater.setSliderRange(*slider, 0.0F, 1.0F, 0.1F));
    assertOk(updater.setSliderValue(*slider, 0.4F));
    assertOk(updater.setProgressBarRange(*progress, 0.0F, 100.0F));
    assertOk(updater.setProgressBarValue(*progress, 55.0F));
    assertOk(updater.setText(*radio, "Windowed"));
    assertOk(updater.setRadioButtonSelected(*radio, true));
    assertOk(updater.setText(*textEdit, "Player"));
    assertOk(context->publication().commitLayout({.width = 400.0F, .height = 300.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->publication().committedSemantics()));
    EXPECT_FALSE(tree.empty());
    EXPECT_EQ(tree.semanticsRevision(), context->publication().committedSemantics().semanticsRevision());

    UI::UIAccessibilityProbeProvider probe;
    assertOk(probe.publish(tree));
    EXPECT_TRUE(probe.hasPublishedTree());
    EXPECT_EQ(probe.publishCount(), 1U);

    auto buttonNode = probe.readNode(*button);
    ASSERT_TRUE(buttonNode.has_value()) << buttonNode.error().message;
    EXPECT_EQ(buttonNode->role, UI::UISemanticsRole::Button);
    EXPECT_EQ(buttonNode->name, "Apply");
    EXPECT_TRUE(UI::hasState(buttonNode->states, UI::UIAccessibilityState::Enabled));

    auto checkboxNode = probe.readNode(*checkbox);
    ASSERT_TRUE(checkboxNode.has_value());
    EXPECT_EQ(checkboxNode->role, UI::UISemanticsRole::Checkbox);
    EXPECT_TRUE(UI::hasState(checkboxNode->states, UI::UIAccessibilityState::Checked));

    auto sliderNode = probe.readNode(*slider);
    ASSERT_TRUE(sliderNode.has_value());
    EXPECT_EQ(sliderNode->role, UI::UISemanticsRole::Slider);
    EXPECT_TRUE(UI::hasState(sliderNode->states, UI::UIAccessibilityState::HasRange));
    EXPECT_FLOAT_EQ(sliderNode->value, 0.4F);
    EXPECT_FLOAT_EQ(sliderNode->minValue, 0.0F);
    EXPECT_FLOAT_EQ(sliderNode->maxValue, 1.0F);

    auto progressNode = probe.readNode(*progress);
    ASSERT_TRUE(progressNode.has_value());
    EXPECT_EQ(progressNode->role, UI::UISemanticsRole::ProgressBar);
    EXPECT_TRUE(UI::hasState(progressNode->states, UI::UIAccessibilityState::ReadOnly));
    EXPECT_FLOAT_EQ(progressNode->value, 55.0F);

    auto radioNode = probe.readNode(*radio);
    ASSERT_TRUE(radioNode.has_value());
    EXPECT_EQ(radioNode->role, UI::UISemanticsRole::RadioButton);
    EXPECT_EQ(radioNode->name, "Windowed");
    EXPECT_TRUE(UI::hasState(radioNode->states, UI::UIAccessibilityState::Checked));

    auto textNode = probe.readNode(*textEdit);
    ASSERT_TRUE(textNode.has_value());
    EXPECT_EQ(textNode->role, UI::UISemanticsRole::TextEdit);
    EXPECT_EQ(textNode->valueText, "Player");

    // Probe lookup by name/role (assistive tech style query).
    EXPECT_NE(tree.findByName("Apply"), nullptr);
    EXPECT_NE(tree.findByRole(UI::UISemanticsRole::ProgressBar), nullptr);
}

TEST(UIAccessibilityTest, StaleNodeAfterDestroyIsRejected)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());

    auto button = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setText(*button, "Doomed"));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->publication().committedSemantics()));
    UI::UIAccessibilityProbeProvider probe;
    assertOk(probe.publish(tree));
    const UI::UINodeId staleId = *button;
    ASSERT_TRUE(probe.readNode(staleId).has_value());

    assertOk(updater.destroy(*button));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    // Stale tree still held by probe must not claim live access without rebuild.
    UI::UIAccessibilityTree fresh;
    assertOk(fresh.rebuildFrom(context->publication().committedSemantics()));
    EXPECT_EQ(fresh.findNode(staleId), nullptr);
    assertOk(probe.publish(fresh));
    auto read = probe.readNode(staleId);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, UI::UIErrorCode::AccessibilityNodeStale);
}

TEST(UIAccessibilityTest, ClearRemovesPublishedTree)
{
    UI::UIAccessibilityProbeProvider probe;
    UI::UIAccessibilityTree empty;
    assertOk(probe.publish(empty));
    EXPECT_TRUE(probe.hasPublishedTree());
    probe.clear();
    EXPECT_FALSE(probe.hasPublishedTree());
    EXPECT_EQ(probe.clearCount(), 1U);
    auto read = probe.readNode(UI::UINodeId{});
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, UI::UIErrorCode::AccessibilityTreeMissing);
}

TEST(UIAccessibilityTest, DisabledStateMapsToDisabledFlag)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto button = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setText(*button, "Off"));
    assertOk(updater.setEnabled(*button, false));
    assertOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    UI::UIAccessibilityTree tree;
    assertOk(tree.rebuildFrom(context->publication().committedSemantics()));
    const UI::UIAccessibilityNode* node = tree.findNode(*button);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(UI::hasState(node->states, UI::UIAccessibilityState::Disabled));
    EXPECT_FALSE(UI::hasState(node->states, UI::UIAccessibilityState::Enabled));
}

TEST(UIAccessibilityTest, ActionsPreserveControlCallbacksAndRejectIncompatibleTargets)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());

    auto button = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    auto checkbox = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeCheckboxElement());
    auto slider = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeSliderElement());
    auto progress = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeProgressBarElement());
    auto textEdit = context->authoring().rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement());
    UI::UIElementDescriptor unpublishedInvokeDescriptor = UI::makeButtonElement("Not invokable");
    unpublishedInvokeDescriptor.semantics.actions = UI::UISemanticsAction::Focus;
    auto unpublishedInvoke =
        context->authoring().rootBuilder().createElement(root.rootNodeId(), unpublishedInvokeDescriptor);
    ASSERT_TRUE(button.has_value());
    ASSERT_TRUE(checkbox.has_value());
    ASSERT_TRUE(slider.has_value());
    ASSERT_TRUE(progress.has_value());
    ASSERT_TRUE(textEdit.has_value());
    ASSERT_TRUE(unpublishedInvoke.has_value());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(400.0F, 300.0F)));
    assertOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*checkbox, fixedSize(24.0F, 24.0F)));
    assertOk(updater.setLayoutStyle(*slider, fixedSize(120.0F, 20.0F)));
    assertOk(updater.setLayoutStyle(*progress, fixedSize(120.0F, 12.0F)));
    assertOk(updater.setLayoutStyle(*textEdit, fixedSize(160.0F, 32.0F)));
    assertOk(updater.setLayoutStyle(*unpublishedInvoke, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setSliderRange(*slider, 0.0F, 100.0F, 1.0F));

    u32 buttonActivations = 0;
    u32 checkboxActivations = 0;
    u32 sliderChanges = 0;
    UI::UISliderChangeEvent lastSliderChange{};
    u32 unpublishedActivations = 0;
    UI::UIButtonActivationSource lastSource = UI::UIButtonActivationSource::PrimaryPointer;
    assertOk(updater.setButtonAction(
        *button,
        UI::UIButtonActionCallback([&](const UI::UIButtonActionEvent& event) noexcept {
            ++buttonActivations;
            lastSource = event.source;
        })));
    assertOk(updater.setCheckboxAction(
        *checkbox,
        UI::UIButtonActionCallback([&](const UI::UIButtonActionEvent& event) noexcept {
            ++checkboxActivations;
            lastSource = event.source;
        })));
    assertOk(updater.setSliderChangeCallback(
        *slider,
        UI::UISliderChangeCallback([&](const UI::UISliderChangeEvent& event) noexcept {
            ++sliderChanges;
            lastSliderChange = event;
        })));
    assertOk(updater.setButtonAction(
        *unpublishedInvoke,
        UI::UIButtonActionCallback([&](const UI::UIButtonActionEvent&) noexcept {
            ++unpublishedActivations;
        })));
    assertOk(context->publication().commitLayout({.width = 400.0F, .height = 300.0F}));

    assertOk(context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Invoke,
        .node = *button,
    }));
    EXPECT_EQ(buttonActivations, 1U);
    EXPECT_EQ(lastSource, UI::UIButtonActivationSource::Accessibility);

    assertOk(context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Toggle,
        .node = *checkbox,
    }));
    EXPECT_EQ(checkboxActivations, 1U);
    EXPECT_EQ(lastSource, UI::UIButtonActivationSource::Accessibility);
    auto checked = updater.isChecked(*checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);

    assertOk(context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetRangeValue,
        .node = *slider,
        .rangeValue = 72.0,
    }));
    auto sliderValue = updater.sliderValue(*slider);
    ASSERT_TRUE(sliderValue.has_value());
    EXPECT_FLOAT_EQ(*sliderValue, 72.0F);
    EXPECT_EQ(sliderChanges, 1U);
    EXPECT_EQ(lastSliderChange.sliderNode, *slider);
    EXPECT_FLOAT_EQ(lastSliderChange.value, 72.0F);

    assertOk(context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetTextValue,
        .node = *textEdit,
        .textValue = "Accessible Player",
    }));
    auto text = updater.text(*textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Accessible Player");

    auto readOnly = context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetRangeValue,
        .node = *progress,
        .rangeValue = 50.0,
    });
    ASSERT_FALSE(readOnly.has_value());
    EXPECT_EQ(readOnly.error().code, UI::UIErrorCode::InvalidAccessibilityAction);

    auto unpublished = context->input().performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Invoke,
        .node = *unpublishedInvoke,
    });
    ASSERT_FALSE(unpublished.has_value());
    EXPECT_EQ(unpublished.error().code, UI::UIErrorCode::InvalidAccessibilityAction);
    EXPECT_EQ(unpublishedActivations, 0U);
}

} // namespace
} // namespace Tina::Tests
