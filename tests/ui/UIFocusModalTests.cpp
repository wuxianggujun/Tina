#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle overlay(float x, float y, float width, float height) noexcept
{
    UI::UILayoutStyle style = fixedSize(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.offset.x = UI::UILayoutLength::Px(x);
    style.overlay.offset.y = UI::UILayoutLength::Px(y);
    return style;
}

[[nodiscard]] UI::UIBoxPaint solidFill() noexcept
{
    return UI::UIBoxPaint{
        .solidFill =
            UI::UISolidFill{
                .color = UI::rgba8(31, 47, 63, 255),
            },
    };
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(Platform::WindowId window,
                                                           UI::UIContextCapacityConfig capacities = {
                                                               .nodeCapacity = 32,
                                                               .rootCapacity = 2,
                                                               .dirtyQueueCapacity = 32,
                                                               .paintSnapshotCapacity = 32,
                                                               .routePathCapacity = 16,
                                                               .routedPointerListenerCapacity = 16,
                                                               .buttonActionCapacity = 16,
                                                               .textByteCapacity = 1024,
                                                           })
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UINodeId createButton(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makeButtonElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createSlider(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makeSliderElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createPanel(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createLabel(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makeLabelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createModal(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makeModalElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UIPointerInputEvent pointerDown(Platform::WindowId window, float x, float y,
                                                  u64 sequence = 1) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::ButtonDown,
        .position = {.x = x, .y = y},
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(UI::UICommittedSemanticsView view,
                                                             UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries())
    {
        if (entry.node == node)
        {
            return &entry;
        }
    }
    return nullptr;
}

class UIFocusModalTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIFocusModalTest, ModalPublishesAtomicallyBlocksOutsideAndPreservesFocus)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId background = createButton(updater, rootNode);
    ASSERT_TRUE(background.hasValue());
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 200.0F)));
    expectOk(updater.setLayoutStyle(background, overlay(0.0F, 0.0F, 40.0F, 40.0F)));
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    expectOk(context->requestFocus(background));

    auto modalResult = context->rootBuilder().createElement(rootNode, UI::makeModalElement());
    ASSERT_TRUE(modalResult.has_value()) << (modalResult ? "" : modalResult.error().message);
    const UI::UINodeId modal = *modalResult;
    const UI::UINodeId modalSlider = createSlider(updater, modal);
    ASSERT_TRUE(modal.hasValue() && modalSlider.hasValue());
    expectOk(updater.setLayoutStyle(modal, overlay(80.0F, 80.0F, 100.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(modalSlider, fixedSize(60.0F, 24.0F)));

    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->queryPointerHit({.x = 10.0F, .y = 10.0F}).target.node, background);

    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    EXPECT_EQ(context->activeModal(), modal);
    EXPECT_EQ(context->committedHit().activeModalNode(), modal);
    EXPECT_EQ(context->defaultActionFocus(), modalSlider);
    EXPECT_EQ(context->activeFocusScope(), modal);
    auto modalScopeMode = updater.focusScopeMode(modal);
    ASSERT_TRUE(modalScopeMode.has_value());
    EXPECT_EQ(*modalScopeMode, UI::UIFocusScopeMode::Contain);

    const UI::UIPointerHitQueryResult blockedQuery = context->queryPointerHit({.x = 10.0F, .y = 10.0F});
    EXPECT_TRUE(blockedQuery.modalBarrierActive);
    EXPECT_FALSE(blockedQuery.hasTarget());

    auto blockedRoute = context->routePointerInput(pointerDown(window, 10.0F, 10.0F));
    ASSERT_TRUE(blockedRoute.has_value()) << (blockedRoute ? "" : blockedRoute.error().message);
    EXPECT_TRUE(blockedRoute->blockedByModal);
    EXPECT_TRUE(blockedRoute->consumed);
    EXPECT_FALSE(blockedRoute->hasRoutedTarget());
    EXPECT_TRUE(blockedRoute->claimedPointerButtons.test(static_cast<usize>(Platform::PointerButton::Primary)));
    EXPECT_EQ(context->defaultActionFocus(), modalSlider);

    const UI::UISemanticsEntry* modalSemantics = findSemanticsEntry(context->committedSemantics(), modal);
    ASSERT_NE(modalSemantics, nullptr);
    EXPECT_EQ(modalSemantics->role, UI::UISemanticsRole::Dialog);

    UI::UILayoutStyle hiddenModal = overlay(80.0F, 80.0F, 100.0F, 100.0F);
    hiddenModal.visibility = UI::UIVisibility::Hidden;
    expectOk(updater.setLayoutStyle(modal, hiddenModal));
    EXPECT_EQ(context->activeModal(), modal);
    EXPECT_TRUE(context->queryPointerHit({.x = 10.0F, .y = 10.0F}).modalBarrierActive);
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->queryPointerHit({.x = 10.0F, .y = 10.0F}).target.node, background);
}

TEST_F(UIFocusModalTest, NestedModalTraversalRestoresEachFocusLayer)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId background = createButton(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(300.0F, 240.0F)));
    expectOk(updater.setLayoutStyle(background, overlay(0.0F, 0.0F, 40.0F, 24.0F)));
    expectOk(context->commitLayout({.width = 300.0F, .height = 240.0F}));
    expectOk(context->requestFocus(background));

    const UI::UINodeId outerModal = createModal(updater, rootNode);
    const UI::UINodeId outerFirst = createButton(updater, outerModal);
    const UI::UINodeId outerSecond = createButton(updater, outerModal);
    const UI::UINodeId nestedHost = createPanel(updater, outerModal);
    expectOk(updater.setLayoutStyle(outerModal, overlay(40.0F, 30.0F, 220.0F, 180.0F)));
    for (const UI::UINodeId node : {outerFirst, outerSecond})
    {
        expectOk(updater.setLayoutStyle(node, fixedSize(80.0F, 24.0F)));
    }
    expectOk(updater.setLayoutStyle(nestedHost, fixedSize(180.0F, 100.0F)));
    expectOk(context->commitLayout({.width = 300.0F, .height = 240.0F}));
    EXPECT_EQ(context->activeModal(), outerModal);
    EXPECT_EQ(context->defaultActionFocus(), outerFirst);

    auto outerStep = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(outerStep.has_value());
    EXPECT_EQ(outerStep->focus, outerSecond);

    const UI::UINodeId innerModal = createModal(updater, nestedHost);
    const UI::UINodeId innerFirst = createButton(updater, innerModal);
    const UI::UINodeId innerSecond = createButton(updater, innerModal);
    const UI::UINodeId hiddenSiblingModal = createModal(updater, nestedHost);
    expectOk(updater.setLayoutStyle(innerModal, overlay(10.0F, 10.0F, 140.0F, 80.0F)));
    for (const UI::UINodeId node : {innerFirst, innerSecond})
    {
        expectOk(updater.setLayoutStyle(node, fixedSize(70.0F, 20.0F)));
    }
    UI::UILayoutStyle hiddenSibling = overlay(0.0F, 0.0F, 20.0F, 20.0F);
    hiddenSibling.visibility = UI::UIVisibility::Hidden;
    expectOk(updater.setLayoutStyle(hiddenSiblingModal, hiddenSibling));
    expectOk(context->commitLayout({.width = 300.0F, .height = 240.0F}));
    EXPECT_EQ(context->activeModal(), innerModal);
    EXPECT_EQ(context->activeFocusScope(), innerModal);
    EXPECT_EQ(context->defaultActionFocus(), innerFirst);

    auto innerStep = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(innerStep.has_value());
    EXPECT_EQ(innerStep->focus, innerSecond);
    auto wrappedStep = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(wrappedStep.has_value());
    EXPECT_EQ(wrappedStep->focus, innerFirst);
    auto reverseStep = context->routeDefaultActionFocusStep(true);
    ASSERT_TRUE(reverseStep.has_value());
    EXPECT_EQ(reverseStep->focus, innerSecond);

    expectOk(updater.destroy(nestedHost));
    expectOk(context->commitLayout({.width = 300.0F, .height = 240.0F}));
    EXPECT_EQ(context->activeModal(), outerModal);
    EXPECT_EQ(context->activeFocusScope(), outerModal);
    EXPECT_EQ(context->defaultActionFocus(), outerSecond);

    UI::UILayoutStyle hiddenOuter = overlay(40.0F, 30.0F, 220.0F, 180.0F);
    hiddenOuter.visibility = UI::UIVisibility::Collapsed;
    expectOk(updater.setLayoutStyle(outerModal, hiddenOuter));
    expectOk(context->commitLayout({.width = 300.0F, .height = 240.0F}));
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), background);
}

TEST_F(UIFocusModalTest, ReleasingModalRootRestoresFocusToAnotherRoot)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto backgroundRoot = createRoot(*context);
    auto backgroundUpdater = createUpdater(*context, backgroundRoot);
    const UI::UINodeId background = createButton(backgroundUpdater, backgroundRoot.rootNodeId());
    expectOk(backgroundUpdater.setLayoutStyle(backgroundRoot.rootNodeId(), fixedSize(200.0F, 120.0F)));
    expectOk(backgroundUpdater.setLayoutStyle(background, overlay(0.0F, 0.0F, 40.0F, 30.0F)));
    expectOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    expectOk(context->requestFocus(background));

    auto modalRoot = createRoot(*context);
    auto modalUpdater = createUpdater(*context, modalRoot);
    const UI::UINodeId modal = createModal(modalUpdater, modalRoot.rootNodeId());
    const UI::UINodeId modalButton = createButton(modalUpdater, modal);
    expectOk(modalUpdater.setLayoutStyle(modalRoot.rootNodeId(), fixedSize(200.0F, 120.0F)));
    expectOk(modalUpdater.setLayoutStyle(modal, overlay(40.0F, 20.0F, 120.0F, 80.0F)));
    expectOk(modalUpdater.setLayoutStyle(modalButton, fixedSize(60.0F, 24.0F)));
    expectOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_EQ(context->activeModal(), modal);
    EXPECT_EQ(context->defaultActionFocus(), modalButton);

    modalRoot.reset();
    EXPECT_FALSE(context->contains(modal));
    EXPECT_EQ(context->committedHit().activeModalNode(), modal);
    expectOk(context->commitLayout({.width = 200.0F, .height = 120.0F}));
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->liveRootCount(), 1U);
}

TEST_F(UIFocusModalTest, ContainScopeCyclesWithoutDependingOnRouteDepthCapacity)
{
    auto context = createContext(window, UI::UIContextCapacityConfig{
                                             .nodeCapacity = 16,
                                             .rootCapacity = 1,
                                             .dirtyQueueCapacity = 16,
                                             .paintSnapshotCapacity = 16,
                                             .routePathCapacity = 2,
                                             .buttonActionCapacity = 8,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId scope = createPanel(updater, root.rootNodeId());
    const UI::UINodeId first = createButton(updater, scope);
    const UI::UINodeId second = createButton(updater, scope);
    const UI::UINodeId third = createButton(updater, scope);
    const UI::UINodeId outside = createButton(updater, root.rootNodeId());
    expectOk(updater.setFocusScopeMode(scope, UI::UIFocusScopeMode::Contain));
    auto scopeMode = updater.focusScopeMode(scope);
    ASSERT_TRUE(scopeMode.has_value());
    EXPECT_EQ(*scopeMode, UI::UIFocusScopeMode::Contain);
    expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 200.0F)));
    for (const UI::UINodeId node : {first, second, third, outside})
    {
        expectOk(updater.setLayoutStyle(node, fixedSize(40.0F, 20.0F)));
    }
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    expectOk(updater.requestFocus(first));
    EXPECT_EQ(context->activeFocusScope(), scope);

    auto step = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step.has_value());
    EXPECT_EQ(step->focus, second);
    step = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step.has_value());
    EXPECT_EQ(step->focus, third);
    step = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(step.has_value());
    EXPECT_EQ(step->focus, first);
    step = context->routeDefaultActionFocusStep(true);
    ASSERT_TRUE(step.has_value());
    EXPECT_EQ(step->focus, third);

    expectOk(updater.requestFocus(outside));
    EXPECT_FALSE(context->activeFocusScope().hasValue());
}

TEST_F(UIFocusModalTest, ExplicitFocusRejectsUncommittedHiddenDisabledAndModalOutsideTargets)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId background = createButton(updater, rootNode);
    const UI::UINodeId label = createLabel(updater, rootNode);
    const UI::UINodeId hidden = createButton(updater, rootNode);
    expectOk(updater.setPointerHitPolicy(label, UI::UIPointerHitPolicy::Targetable));
    UI::UILayoutStyle hiddenStyle = fixedSize(40.0F, 20.0F);
    hiddenStyle.visibility = UI::UIVisibility::Hidden;
    expectOk(updater.setLayoutStyle(hidden, hiddenStyle));
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 200.0F)));

    const UI::UINodeId uncommitted = createButton(updater, rootNode);
    Core::Status rejected = context->requestFocus(uncommitted);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFocusTarget);

    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    for (const UI::UINodeId invalidTarget : {label, hidden})
    {
        rejected = updater.requestFocus(invalidTarget);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFocusTarget);
    }

    expectOk(updater.setEnabled(uncommitted, false));
    rejected = updater.requestFocus(uncommitted);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFocusTarget);

    const UI::UINodeId modal = createModal(updater, rootNode);
    const UI::UINodeId modalButton = createButton(updater, modal);
    expectOk(updater.setLayoutStyle(modal, overlay(60.0F, 60.0F, 100.0F, 80.0F)));
    expectOk(updater.setLayoutStyle(modalButton, fixedSize(50.0F, 20.0F)));
    rejected = updater.setFocusScopeMode(modal, UI::UIFocusScopeMode::None);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFocusScope);
    rejected = updater.setFocusScopeMode(rootNode, static_cast<UI::UIFocusScopeMode>(255));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFocusScope);

    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    EXPECT_EQ(context->defaultActionFocus(), modalButton);
    rejected = context->requestFocus(background);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFocusTarget);
    expectOk(context->clearFocus());
    expectOk(context->clearFocus());
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_EQ(context->activeFocusScope(), modal);
}

TEST_F(UIFocusModalTest, FailedPaintCommitDoesNotPublishModalFocusOrCapture)
{
    auto context = createContext(window, UI::UIContextCapacityConfig{
                                             .nodeCapacity = 8,
                                             .rootCapacity = 1,
                                             .dirtyQueueCapacity = 8,
                                             .paintSnapshotCapacity = 3,
                                             .routePathCapacity = 8,
                                             .buttonActionCapacity = 4,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId background = createButton(updater, rootNode);
    const UI::UINodeId firstDecorator = createPanel(updater, rootNode);
    const UI::UINodeId secondDecorator = createPanel(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 200.0F)));
    expectOk(updater.setLayoutStyle(background, overlay(0.0F, 0.0F, 40.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(firstDecorator, overlay(50.0F, 0.0F, 20.0F, 20.0F)));
    expectOk(updater.setLayoutStyle(secondDecorator, overlay(80.0F, 0.0F, 20.0F, 20.0F)));
    expectOk(updater.setBoxPaint(background, solidFill()));
    expectOk(updater.setBoxPaint(firstDecorator, solidFill()));
    expectOk(updater.setBoxPaint(secondDecorator, solidFill()));
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));

    auto armed = context->routePointerInput(pointerDown(window, 10.0F, 10.0F));
    ASSERT_TRUE(armed.has_value()) << (armed ? "" : armed.error().message);
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->pointerCapture(), background);
    usize pointerCancelCount = 0;
    auto pointerCancelListener = context->addRoutedPointerListener(
        {
            .node = background,
            .kind = UI::UIRoutedPointerEventKind::PointerCancel,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{
            [&pointerCancelCount](UI::UIRoutedPointerEvent&) noexcept { ++pointerCancelCount; }});
    ASSERT_TRUE(pointerCancelListener.has_value())
        << (pointerCancelListener ? "" : pointerCancelListener.error().message);
    auto pointerCancelToken = std::move(*pointerCancelListener);
    ASSERT_TRUE(pointerCancelToken);
    const u64 hitRevision = context->committedHit().hitRevision();

    const UI::UINodeId modal = createModal(updater, rootNode);
    const UI::UINodeId modalButton = createButton(updater, modal);
    expectOk(updater.setLayoutStyle(modal, overlay(70.0F, 70.0F, 100.0F, 80.0F)));
    expectOk(updater.setLayoutStyle(modalButton, fixedSize(50.0F, 20.0F)));
    expectOk(updater.setBoxPaint(modal, solidFill()));

    const Core::Status failed = context->commitLayout({.width = 200.0F, .height = 200.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedHit().hitRevision(), hitRevision);
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->pointerCapture(), background);
    EXPECT_EQ(pointerCancelCount, 0);

    expectOk(updater.setBoxPaint(modal, UI::UIBoxPaint{}));
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    EXPECT_EQ(context->activeModal(), modal);
    EXPECT_EQ(context->defaultActionFocus(), modalButton);
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(pointerCancelCount, 1);
}

TEST_F(UIFocusModalTest, FailedSemanticsCommitDoesNotPublishModalFocusOrCapture)
{
    auto context = createContext(window, UI::UIContextCapacityConfig{
                                             .nodeCapacity = 6,
                                             .rootCapacity = 1,
                                             .dirtyQueueCapacity = 6,
                                             .paintSnapshotCapacity = 2,
                                             .routePathCapacity = 6,
                                             .buttonActionCapacity = 4,
                                             .textByteCapacity = 2,
                                         });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId background = createButton(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 200.0F)));
    expectOk(updater.setLayoutStyle(background, overlay(0.0F, 0.0F, 40.0F, 40.0F)));
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));

    auto armed = context->routePointerInput(pointerDown(window, 10.0F, 10.0F));
    ASSERT_TRUE(armed.has_value()) << (armed ? "" : armed.error().message);
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->pointerCapture(), background);
    usize pointerCancelCount = 0;
    auto pointerCancelListener = context->addRoutedPointerListener(
        {
            .node = background,
            .kind = UI::UIRoutedPointerEventKind::PointerCancel,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{
            [&pointerCancelCount](UI::UIRoutedPointerEvent&) noexcept { ++pointerCancelCount; }});
    ASSERT_TRUE(pointerCancelListener.has_value())
        << (pointerCancelListener ? "" : pointerCancelListener.error().message);
    auto pointerCancelToken = std::move(*pointerCancelListener);
    ASSERT_TRUE(pointerCancelToken);
    const u64 semanticsRevision = context->committedSemantics().semanticsRevision();

    UI::UIElementDescriptor modalDescriptor = UI::makeModalElement();
    modalDescriptor.semantics.mode = UI::UISemanticsMode::MergeDescendants;
    modalDescriptor.semantics.name = "a";
    auto modalResult = updater.createElement(rootNode, modalDescriptor);
    ASSERT_TRUE(modalResult.has_value()) << modalResult.error().message;
    const UI::UINodeId modal = *modalResult;
    const UI::UINodeId modalButton = createButton(updater, modal);
    expectOk(updater.setText(modalButton, "b"));
    expectOk(updater.setLayoutStyle(modal, overlay(70.0F, 70.0F, 100.0F, 80.0F)));
    expectOk(updater.setLayoutStyle(modalButton, fixedSize(50.0F, 20.0F)));

    const Core::Status failed = context->commitLayout({.width = 200.0F, .height = 200.0F});
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedSemantics().semanticsRevision(), semanticsRevision);
    EXPECT_FALSE(context->activeModal().hasValue());
    EXPECT_EQ(context->defaultActionFocus(), background);
    EXPECT_EQ(context->pointerCapture(), background);
    EXPECT_EQ(pointerCancelCount, 0);

    expectOk(updater.setText(modalButton, ""));
    expectOk(context->commitLayout({.width = 200.0F, .height = 200.0F}));
    EXPECT_EQ(context->activeModal(), modal);
    EXPECT_EQ(context->defaultActionFocus(), modalButton);
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(pointerCancelCount, 1);
}

TEST_F(UIFocusModalTest, FocusTraversalIsRejectedDuringPointerDispatch)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId button = createButton(updater, root.rootNodeId());
    expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    Core::ErrorCode nestedError{};
    auto tokenResult = context->addRoutedPointerListener(
        {
            .node = button,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{[&context, &nestedError](UI::UIRoutedPointerEvent&) noexcept {
            auto nested = context->routeDefaultActionFocusStep(false);
            if (!nested)
            {
                nestedError = nested.error().code;
            }
        }});
    ASSERT_TRUE(tokenResult.has_value()) << (tokenResult ? "" : tokenResult.error().message);
    auto token = std::move(*tokenResult);

    auto routed = context->routePointerInput(pointerDown(window, 10.0F, 10.0F));
    ASSERT_TRUE(routed.has_value()) << (routed ? "" : routed.error().message);
    EXPECT_TRUE(token.isActive());
    EXPECT_EQ(nestedError, UI::UIErrorCode::PointerRouteAlreadyInProgress);
    EXPECT_EQ(routed->routedTarget.node, button);
}

} // namespace
} // namespace Tina::Tests
