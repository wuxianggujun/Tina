#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <memory_resource>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class ObservingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] usize allocationCount() const noexcept
    {
        return m_allocationCount;
    }

  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        void* storage = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        return storage;
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    usize m_allocationCount = 0;
};

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

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] std::unique_ptr<UI::UIContext>
createContext(Platform::WindowId window,
              UI::UIContextCapacityConfig capacities =
                  {
                      .nodeCapacity = 24,
                      .rootCapacity = 1,
                      .dirtyQueueCapacity = 24,
                      .paintSnapshotCapacity = 24,
                      .routePathCapacity = 16,
                      .routedPointerListenerCapacity = 16,
                      .buttonActionCapacity = 16,
                      .textByteCapacity = 1024,
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

[[nodiscard]] UI::UINodeId createPanel(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
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

[[nodiscard]] UI::UINodeId createTextEdit(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makeTextEditElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createModal(UI::UITreeUpdater& updater, UI::UINodeId parent)
{
    auto result = updater.createElement(parent, UI::makeModalElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UIRoutedPointerListenerToken
addListener(UI::UIContext& context, UI::UIRoutedPointerListenerDesc descriptor, UI::UIRoutedPointerCallback callback)
{
    auto result = context.addRoutedPointerListener(descriptor, std::move(callback));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRoutedPointerListenerToken{};
}

[[nodiscard]] UI::UIPointerInputEvent
pointerInput(Platform::WindowId window, UI::UIRoutedPointerEventKind kind, float x, float y, u64 sequence,
             Platform::PointerButton button = Platform::PointerButton::Primary) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = {.x = x, .y = y},
        .button = button,
    };
}

class UIPointerCaptureTest : public testing::Test {
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

TEST_F(UIPointerCaptureTest, ListenerCaptureSeparatesPhysicalAndRoutedTargetsThenReleases)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId panel = createPanel(updater, rootNode);
    const UI::UINodeId target = createButton(updater, panel);
    const UI::UINodeId physicalOther = createButton(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(panel, overlay(0.0F, 0.0F, 60.0F, 60.0F)));
    expectOk(updater.setLayoutStyle(target, fixedSize(40.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(physicalOther, overlay(100.0F, 0.0F, 40.0F, 40.0F)));
    expectOk(context->commitLayout({.width = 200.0F, .height = 100.0F}));

    auto captureToken = addListener(
        *context,
        {
            .node = panel,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .phases = UI::UIEventPhaseMask::Capture,
        },
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.capturePointer(); }});
    bool capturedMoveObserved = false;
    auto releaseToken =
        addListener(*context,
                    {
                        .node = panel,
                        .kind = UI::UIRoutedPointerEventKind::Move,
                        .phases = UI::UIEventPhaseMask::Target,
                    },
                    UI::UIRoutedPointerCallback{[&capturedMoveObserved](UI::UIRoutedPointerEvent& event) noexcept {
                        capturedMoveObserved =
                            event.isPointerCaptureRoute() && event.currentNode() == event.targetNode();
                        event.releasePointerCapture();
                    }});
    ASSERT_TRUE(captureToken && releaseToken);

    auto rejectedSyntheticCancel =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::PointerCancel, 10.0F, 10.0F, 1));
    ASSERT_FALSE(rejectedSyntheticCancel.has_value());
    EXPECT_EQ(rejectedSyntheticCancel.error().code, UI::UIErrorCode::InvalidPointerInput);

    auto down =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(down->pointQuery.target.node, target);
    EXPECT_EQ(down->routedTarget.node, target);
    EXPECT_FALSE(down->routedThroughPointerCapture);
    EXPECT_TRUE(down->pointerCaptureChanged);
    EXPECT_EQ(context->pointerCapture(), panel);

    auto move = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Move, 110.0F, 10.0F, 2));
    ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    EXPECT_EQ(move->pointQuery.target.node, physicalOther);
    EXPECT_EQ(move->routedTarget.node, panel);
    EXPECT_TRUE(move->routedThroughPointerCapture);
    EXPECT_TRUE(move->hasRoutedTarget());
    EXPECT_TRUE(move->pointerCaptureChanged);
    EXPECT_TRUE(capturedMoveObserved);
    EXPECT_FALSE(context->pointerCapture().hasValue());

    auto uncapturedMove =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Move, 110.0F, 10.0F, 3));
    ASSERT_TRUE(uncapturedMove.has_value()) << (uncapturedMove ? "" : uncapturedMove.error().message);
    EXPECT_EQ(uncapturedMove->pointQuery.target.node, physicalOther);
    EXPECT_EQ(uncapturedMove->routedTarget.node, physicalOther);
    EXPECT_FALSE(uncapturedMove->routedThroughPointerCapture);
}

TEST_F(UIPointerCaptureTest, ControlsAutoCaptureAndReleaseOnUpCancelDestroyDisableHideAndModal)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId button = createButton(updater, rootNode);
    const UI::UINodeId slider = createSlider(updater, rootNode);
    const UI::UINodeId textEdit = createTextEdit(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(320.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(button, overlay(0.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(slider, overlay(100.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(textEdit, overlay(200.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(context->commitLayout({.width = 320.0F, .height = 100.0F}));

    int buttonCancelCount = 0;
    int sliderCancelCount = 0;
    int textEditCancelCount = 0;
    bool cancelContractObserved = true;
    const auto observeCancel = [&cancelContractObserved](UI::UINodeId expectedTarget, int& count) noexcept {
        return UI::UIRoutedPointerCallback{
            [expectedTarget, &count, &cancelContractObserved](UI::UIRoutedPointerEvent& event) noexcept {
                ++count;
                cancelContractObserved =
                    cancelContractObserved && event.input().kind == UI::UIRoutedPointerEventKind::PointerCancel &&
                    event.isPointerCaptureRoute() && event.currentPhase() == UI::UIEventPhase::Target &&
                    event.currentNode() == expectedTarget && event.targetNode() == expectedTarget;
            }};
    };
    auto buttonCancelToken = addListener(*context,
                                         {
                                             .node = button,
                                             .kind = UI::UIRoutedPointerEventKind::PointerCancel,
                                             .phases = UI::UIEventPhaseMask::Target,
                                         },
                                         observeCancel(button, buttonCancelCount));
    auto sliderCancelToken = addListener(*context,
                                         {
                                             .node = slider,
                                             .kind = UI::UIRoutedPointerEventKind::PointerCancel,
                                             .phases = UI::UIEventPhaseMask::Target,
                                         },
                                         observeCancel(slider, sliderCancelCount));
    auto textEditCancelToken = addListener(*context,
                                           {
                                               .node = textEdit,
                                               .kind = UI::UIRoutedPointerEventKind::PointerCancel,
                                               .phases = UI::UIEventPhaseMask::Target,
                                           },
                                           observeCancel(textEdit, textEditCancelCount));
    bool cancelListenerDestroyedTarget = false;
    auto destroyFromCancelToken =
        addListener(*context,
                    {
                        .node = slider,
                        .kind = UI::UIRoutedPointerEventKind::PointerCancel,
                        .phases = UI::UIEventPhaseMask::Target,
                    },
                    UI::UIRoutedPointerCallback{
                        [&updater, slider, &cancelListenerDestroyedTarget](UI::UIRoutedPointerEvent&) noexcept {
                            cancelListenerDestroyedTarget = updater.destroy(slider).has_value();
                        }});
    ASSERT_TRUE(buttonCancelToken && sliderCancelToken && textEditCancelToken && destroyFromCancelToken);

    auto down =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->pointerCapture(), button);
    auto moveOutside =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Move, 310.0F, 90.0F, 2));
    ASSERT_TRUE(moveOutside.has_value());
    EXPECT_FALSE(moveOutside->pointQuery.hasTarget());
    EXPECT_EQ(moveOutside->routedTarget.node, button);
    auto up =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 310.0F, 90.0F, 3));
    ASSERT_TRUE(up.has_value());
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(buttonCancelCount, 0);

    down = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 4));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->pointerCapture(), slider);
    expectOk(context->cancelPointerInteraction(window));
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(sliderCancelCount, 0);

    down = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 5));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->pointerCapture(), slider);
    expectOk(updater.destroy(slider));
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(sliderCancelCount, 1);
    EXPECT_TRUE(cancelListenerDestroyedTarget);
    expectOk(context->commitLayout({.width = 320.0F, .height = 100.0F}));

    down = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 210.0F, 10.0F, 6));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->pointerCapture(), textEdit);
    expectOk(updater.setEnabled(textEdit, false));
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(textEditCancelCount, 1);
    expectOk(updater.setEnabled(textEdit, true));
    expectOk(context->commitLayout({.width = 320.0F, .height = 100.0F}));

    down = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 210.0F, 10.0F, 7));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->pointerCapture(), textEdit);
    UI::UILayoutStyle hiddenTextEdit = overlay(200.0F, 0.0F, 60.0F, 40.0F);
    hiddenTextEdit.visibility = UI::UIVisibility::Hidden;
    expectOk(updater.setLayoutStyle(textEdit, hiddenTextEdit));
    EXPECT_EQ(context->pointerCapture(), textEdit);
    expectOk(context->commitLayout({.width = 320.0F, .height = 100.0F}));
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(textEditCancelCount, 2);

    down = context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 8));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->pointerCapture(), button);
    const UI::UINodeId modal = createModal(updater, rootNode);
    const UI::UINodeId modalButton = createButton(updater, modal);
    expectOk(updater.setLayoutStyle(modal, overlay(80.0F, 10.0F, 120.0F, 70.0F)));
    expectOk(updater.setLayoutStyle(modalButton, fixedSize(50.0F, 20.0F)));
    expectOk(context->commitLayout({.width = 320.0F, .height = 100.0F}));
    EXPECT_EQ(context->activeModal(), modal);
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(buttonCancelCount, 1);
    EXPECT_TRUE(cancelContractObserved);
}

TEST_F(UIPointerCaptureTest, CaptureRoutingUsesNoAdditionalSuppliedPmrAllocations)
{
    ObservingMemoryResource resource;
    auto context = createContext(window, {}, resource);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId button = createButton(updater, root.rootNodeId());
    expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    expectOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    const usize allocationsBeforeRoutes = resource.allocationCount();

    auto down =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    for (u64 sequence = 2; sequence < 302; ++sequence)
    {
        auto move = context->routePointerInput(
            pointerInput(window, UI::UIRoutedPointerEventKind::Move, 90.0F, 90.0F, sequence));
        ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
        EXPECT_TRUE(move->routedThroughPointerCapture);
        EXPECT_EQ(move->routedTarget.node, button);
    }
    auto up =
        context->routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 90.0F, 90.0F, 302));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_FALSE(context->pointerCapture().hasValue());
    EXPECT_EQ(resource.allocationCount(), allocationsBeforeRoutes);
}

} // namespace
} // namespace Tina::Tests
