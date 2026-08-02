#include "UIButtonActionTestSupport.hpp"

#include <algorithm>

namespace Tina::Tests {
namespace {

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(UI::UICommittedSemanticsView semantics, UI::UINodeId node)
{
    const auto entry = std::ranges::find_if(
        semantics, [node](const UI::UISemanticsEntry& candidate) { return candidate.node == node; });
    return entry == semantics.end() ? nullptr : &*entry;
}

TEST(UIBehaviorCompositionTests, NonFocusableActivatePanelRoutesPointerWithoutTakingFocus)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window, {
                                             .nodeCapacity = 4,
                                             .rootCapacity = 1,
                                             .paintSnapshotCapacity = 16,
                                             .routePathCapacity = 4,
                                             .buttonActionCapacity = 2,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor descriptor = UI::makePanelElement();
    descriptor.behaviors = UI::UIElementBehavior::Activate;
    descriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .role = UI::UISemanticsRole::Button,
        .actions = UI::UISemanticsAction::Activate,
    };
    auto nodeResult = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(nodeResult) << (nodeResult ? "" : nodeResult.error().message);
    const UI::UINodeId node = *nodeResult;
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(node, fixedSize(40.0F, 40.0F)));

    ActionRecorder recorder;
    assertOk(updater.setButtonAction(node, makeAction(recorder, 7)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    const UI::UIPointerRouteResult down =
        route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(buttonPressed(updater, node));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    const UI::UIPointerRouteResult up =
        route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_TRUE(up.consumed);
    EXPECT_FALSE(buttonPressed(updater, node));
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].button, node);
    EXPECT_EQ(recorder.entries[0].source, UI::UIButtonActivationSource::PrimaryPointer);
    assertOk(context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Invoke,
        .node = node,
    }));
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].source, UI::UIButtonActivationSource::Accessibility);
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(updater.setChecked(node, true));

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.activateBehaviorCapacity, 4U);
    EXPECT_EQ(statistics.activeActivateBehaviorCount, 1U);
    EXPECT_EQ(statistics.activateBehaviorHighWater, 1U);
    EXPECT_EQ(statistics.activeToggleBehaviorCount, 0U);
}

TEST(UIBehaviorCompositionTests, FocusableTextToggleComposesPointerKeyboardAndSemantics)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window, {
                                             .nodeCapacity = 4,
                                             .rootCapacity = 1,
                                             .paintSnapshotCapacity = 16,
                                             .routePathCapacity = 4,
                                             .buttonActionCapacity = 2,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor descriptor = UI::makeLabelElement("Switch");
    descriptor.behaviors =
        UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle;
    descriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .role = UI::UISemanticsRole::Checkbox,
        .actions = UI::UISemanticsAction::Focus | UI::UISemanticsAction::Activate | UI::UISemanticsAction::Toggle,
        .useContentAsName = true,
    };
    auto nodeResult = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(nodeResult) << (nodeResult ? "" : nodeResult.error().message);
    const UI::UINodeId node = *nodeResult;
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(node, fixedSize(60.0F, 40.0F)));

    ActionRecorder recorder;
    assertOk(updater.setButtonAction(node, makeAction(recorder, 9)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_EQ(context->defaultActionFocus(), node);
    auto checked = updater.isChecked(node);
    ASSERT_TRUE(checked);
    EXPECT_TRUE(*checked);
    ASSERT_EQ(recorder.size, 1U);

    auto keyboard =
        context->routeDefaultActionActivate(Platform::PlatformFrameId{3}, 3, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(keyboard) << (keyboard ? "" : keyboard.error().message);
    EXPECT_TRUE(keyboard->consumed);
    EXPECT_TRUE(keyboard->activated);
    checked = updater.isChecked(node);
    ASSERT_TRUE(checked);
    EXPECT_FALSE(*checked);
    ASSERT_EQ(recorder.size, 2U);
    EXPECT_EQ(recorder.entries[1].source, UI::UIButtonActivationSource::Keyboard);

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UISemanticsEntry* semantics = findSemanticsEntry(context->committedSemantics(), node);
    ASSERT_NE(semantics, nullptr);
    EXPECT_FALSE(semantics->checked);
    EXPECT_TRUE(semantics->focused);
    EXPECT_FALSE(updater.setCheckboxPaint(node, {}));
}

TEST(UIBehaviorCompositionTests, ToggleOnlyPanelSupportsStateAndAccessibilityWithoutActivate)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window, {
                                             .nodeCapacity = 3,
                                             .rootCapacity = 1,
                                             .routePathCapacity = 3,
                                             .buttonActionCapacity = 1,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor descriptor = UI::makePanelElement();
    descriptor.behaviors = UI::UIElementBehavior::Toggle;
    descriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .role = UI::UISemanticsRole::Checkbox,
        .actions = UI::UISemanticsAction::Toggle,
    };
    auto nodeResult = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(nodeResult) << (nodeResult ? "" : nodeResult.error().message);
    const UI::UINodeId node = *nodeResult;
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(node, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setChecked(node, true));
    EXPECT_FALSE(
        updater.setButtonAction(node, UI::UIButtonActionCallback([](const UI::UIButtonActionEvent&) noexcept {})));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    assertOk(context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::Toggle,
        .node = node,
    }));
    auto checked = updater.isChecked(node);
    ASSERT_TRUE(checked);
    EXPECT_FALSE(*checked);
    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const UI::UISemanticsEntry* semantics = findSemanticsEntry(context->committedSemantics(), node);
    ASSERT_NE(semantics, nullptr);
    EXPECT_FALSE(semantics->checked);
}

TEST(UIBehaviorCompositionTests, TextRangeInputUsesCapabilityStateForKeyboardAccessibilityAndSemantics)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window, {
                                             .nodeCapacity = 4,
                                             .rootCapacity = 1,
                                             .paintSnapshotCapacity = 16,
                                             .routePathCapacity = 4,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor descriptor = UI::makeLabelElement("Volume");
    descriptor.behaviors =
        UI::UIElementBehavior::Focusable | UI::UIElementBehavior::Activate | UI::UIElementBehavior::RangeInput;
    descriptor.semantics = {
        .mode = UI::UISemanticsMode::Publish,
        .role = UI::UISemanticsRole::Slider,
        .actions =
            UI::UISemanticsAction::Focus | UI::UISemanticsAction::Activate | UI::UISemanticsAction::SetRangeValue,
        .useContentAsName = true,
    };
    auto nodeResult = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(nodeResult) << (nodeResult ? "" : nodeResult.error().message);
    const UI::UINodeId node = *nodeResult;
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(120.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(node, fixedSize(80.0F, 24.0F)));
    assertOk(updater.setSliderRange(node, 0.0F, 10.0F, 2.0F));
    assertOk(updater.setSliderValue(node, 4.0F));
    ActionRecorder recorder;
    assertOk(updater.setButtonAction(node, makeAction(recorder, 11)));
    EXPECT_FALSE(updater.setSliderPaint(node, {}));
    EXPECT_FALSE(updater.setSliderChangeCallback(
        node, UI::UISliderChangeCallback([](const UI::UISliderChangeEvent&) noexcept {})));
    assertOk(context->commitLayout({.width = 120.0F, .height = 80.0F}));
    const UI::UIPointerRouteResult down =
        route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    const UI::UIPointerRouteResult up =
        route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2));
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(up.consumed);
    ASSERT_EQ(recorder.size, 1U);
    EXPECT_EQ(recorder.entries[0].button, node);
    EXPECT_EQ(recorder.entries[0].source, UI::UIButtonActivationSource::PrimaryPointer);
    EXPECT_EQ(context->defaultActionFocus(), node);

    auto increased =
        context->routeRangeInputCommand(Platform::PlatformFrameId{4}, 4, UI::UIRangeInputCommand::Increase, true,
                                        Platform::KeyControlIdentity{.window = window, .key = Platform::Key::Right});
    ASSERT_TRUE(increased) << (increased ? "" : increased.error().message);
    EXPECT_TRUE(increased->targeted);
    EXPECT_TRUE(increased->consumed);
    EXPECT_TRUE(increased->changed);
    auto value = updater.sliderValue(node);
    ASSERT_TRUE(value);
    EXPECT_FLOAT_EQ(*value, 6.0F);

    assertOk(context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetRangeValue,
        .node = node,
        .rangeValue = 7.1,
    }));
    value = updater.sliderValue(node);
    ASSERT_TRUE(value);
    EXPECT_FLOAT_EQ(*value, 8.0F);
    assertOk(context->commitLayout({.width = 120.0F, .height = 80.0F}));
    const UI::UISemanticsEntry* semantics = findSemanticsEntry(context->committedSemantics(), node);
    ASSERT_NE(semantics, nullptr);
    EXPECT_TRUE(semantics->hasRange);
    EXPECT_FLOAT_EQ(semantics->minValue, 0.0F);
    EXPECT_FLOAT_EQ(semantics->maxValue, 10.0F);
    EXPECT_FLOAT_EQ(semantics->value, 8.0F);

    const UI::UIContextStatistics statistics = context->statistics();
    EXPECT_EQ(statistics.rangeInputBehaviorCapacity, 4U);
    EXPECT_EQ(statistics.activeRangeInputBehaviorCount, 1U);
    EXPECT_EQ(statistics.rangeInputBehaviorHighWater, 1U);
}

TEST(UIBehaviorCompositionTests, GenericRangeInputDoesNotEnterSliderPointerDragPath)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window, {
                                             .nodeCapacity = 3,
                                             .rootCapacity = 1,
                                             .paintSnapshotCapacity = 8,
                                             .routePathCapacity = 3,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor descriptor = UI::makeLabelElement("Volume");
    descriptor.behaviors = UI::UIElementBehavior::Focusable | UI::UIElementBehavior::RangeInput;
    auto nodeResult = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(nodeResult) << (nodeResult ? "" : nodeResult.error().message);
    const UI::UINodeId node = *nodeResult;
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(120.0F, 80.0F)));
    assertOk(updater.setLayoutStyle(node, fixedSize(80.0F, 24.0F)));
    assertOk(updater.setSliderRange(node, 0.0F, 10.0F, 1.0F));
    assertOk(updater.setSliderValue(node, 4.0F));
    assertOk(context->commitLayout({.width = 120.0F, .height = 80.0F}));

    const UI::UIPointerRouteResult down =
        route(*context, makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1));
    EXPECT_FALSE(down.consumed);
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    auto value = updater.sliderValue(node);
    ASSERT_TRUE(value);
    EXPECT_FLOAT_EQ(*value, 4.0F);
}

TEST(UIBehaviorCompositionTests, FailedCreateAndNodeReuseReleaseBehaviorSlotsWithoutAllocationGrowth)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    ObservingMemoryResource resource;
    auto context = createContext(window,
                                 {
                                     .nodeCapacity = 3,
                                     .rootCapacity = 1,
                                     .routePathCapacity = 3,
                                     .buttonActionCapacity = 1,
                                     .textByteCapacity = 1,
                                 },
                                 resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor invalid = UI::makeLabelElement("too long");
    invalid.behaviors =
        UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput;
    const auto failed = context->rootBuilder().createElement(root.rootNodeId(), invalid);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().activeActivateBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().activeToggleBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().activeRangeInputBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().liveNodeCount, 1U);

    UI::UIElementDescriptor descriptor = UI::makePanelElement();
    descriptor.behaviors =
        UI::UIElementBehavior::Activate | UI::UIElementBehavior::Toggle | UI::UIElementBehavior::RangeInput;
    auto first = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(first);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setChecked(*first, true));
    assertOk(updater.setSliderRange(*first, 0.0F, 10.0F, 1.0F));
    assertOk(updater.setSliderValue(*first, 7.0F));
    const usize allocationCount = resource.allocationCount();
    assertOk(updater.destroy(*first));
    EXPECT_EQ(context->statistics().activeActivateBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().activeToggleBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().activeRangeInputBehaviorCount, 0U);

    auto second = context->rootBuilder().createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(second);
    auto checked = updater.isChecked(*second);
    ASSERT_TRUE(checked);
    EXPECT_FALSE(*checked);
    auto rangeValue = updater.sliderValue(*second);
    ASSERT_TRUE(rangeValue);
    EXPECT_FLOAT_EQ(*rangeValue, 0.0F);
    EXPECT_EQ(resource.allocationCount(), allocationCount);
    EXPECT_EQ(context->statistics().activateBehaviorHighWater, 1U);
    EXPECT_EQ(context->statistics().toggleBehaviorHighWater, 1U);
    EXPECT_EQ(context->statistics().rangeInputBehaviorHighWater, 1U);
}

TEST(UIBehaviorCompositionTests, TextInputPublicationRollbackStatisticsAndReuseAreAllocationStable)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    ObservingMemoryResource resource;
    auto context = createContext(window, {
                                             .nodeCapacity = 4,
                                             .rootCapacity = 1,
                                             .textByteCapacity = 12,
                                         },
                                 resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    auto failed =
        context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement("this is too long"));
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().activeTextInputBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().liveNodeCount, 1U);

    auto first = context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement("first"));
    ASSERT_TRUE(first);
    EXPECT_EQ(context->statistics().activeTextInputBehaviorCount, 1U);
    EXPECT_EQ(context->statistics().textInputBehaviorCapacity, 4U);

    auto second = context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement("second"));
    ASSERT_TRUE(second);
    EXPECT_EQ(context->statistics().activeTextInputBehaviorCount, 2U);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult);
    auto updater = std::move(*updaterResult);
    assertOk(updater.setTextSelection(*first, {.anchorCodepoint = 1, .caretCodepoint = 4}));
    const usize allocationCount = resource.allocationCount();
    assertOk(updater.destroy(*first));
    EXPECT_EQ(context->statistics().activeTextInputBehaviorCount, 1U);
    auto reused = context->rootBuilder().createElement(root.rootNodeId(), UI::makeTextEditElement("again"));
    ASSERT_TRUE(reused);
    auto selection = updater.textSelection(*reused);
    ASSERT_TRUE(selection);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
    EXPECT_EQ(context->statistics().textInputBehaviorHighWater, 2U);
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

TEST(UIBehaviorCompositionTests, ScrollPublicationRollbackStatisticsAndReuseAreAllocationStable)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows);
    const Platform::WindowId window = *windows->tryEmplace(1);
    ObservingMemoryResource resource;
    auto context = createContext(window, {
                                             .nodeCapacity = 4,
                                             .rootCapacity = 1,
                                             .textByteCapacity = 4,
                                         },
                                 resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);

    UI::UIElementDescriptor invalid = UI::makeScrollViewElement();
    invalid.semantics.name = "too long";
    auto failed = context->rootBuilder().createElement(root.rootNodeId(), invalid);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().activeScrollBehaviorCount, 0U);
    EXPECT_EQ(context->statistics().scrollBehaviorHighWater, 1U);
    EXPECT_EQ(context->statistics().liveNodeCount, 1U);

    auto first = context->rootBuilder().createElement(root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(first);
    auto second = context->rootBuilder().createElement(root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(second);
    auto updater = createUpdater(*context, root);
    UI::UIScrollViewStyle style{};
    style.axes = UI::UIScrollAxes::Horizontal;
    style.wheelStep = 17.0F;
    assertOk(updater.setScrollViewStyle(*first, style));
    assertOk(updater.setScrollViewOffset(*first, {.x = 7.0F, .y = 9.0F}));
    EXPECT_EQ(context->statistics().activeScrollBehaviorCount, 2U);
    EXPECT_EQ(context->statistics().scrollBehaviorCapacity, 4U);

    const usize allocationCount = resource.allocationCount();
    assertOk(updater.destroy(*first));
    EXPECT_EQ(context->statistics().activeScrollBehaviorCount, 1U);
    auto reused = context->rootBuilder().createElement(root.rootNodeId(), UI::makeScrollViewElement());
    ASSERT_TRUE(reused);
    auto reusedStyle = updater.scrollViewStyle(*reused);
    ASSERT_TRUE(reusedStyle);
    EXPECT_EQ(*reusedStyle, (UI::UIScrollViewStyle{}));
    auto reusedOffset = updater.scrollViewOffset(*reused);
    ASSERT_TRUE(reusedOffset);
    EXPECT_EQ(*reusedOffset, (UI::UIScrollOffset{}));
    auto reusedMetrics = updater.scrollViewMetrics(*reused);
    ASSERT_TRUE(reusedMetrics);
    EXPECT_EQ(*reusedMetrics, (UI::UIScrollViewMetrics{}));
    EXPECT_EQ(context->statistics().scrollBehaviorHighWater, 2U);
    EXPECT_EQ(resource.allocationCount(), allocationCount);
}

} // namespace
} // namespace Tina::Tests
