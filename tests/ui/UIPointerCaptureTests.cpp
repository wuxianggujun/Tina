#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <memory_resource>
#include <tuple>
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
    auto result = context.input().addRoutedPointerListener(descriptor, std::move(callback));
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

[[nodiscard]] UI::UIPointerInputEvent
pointerInputFor(Platform::WindowId window, Platform::PointerId pointer,
                UI::UIRoutedPointerEventKind kind, float x, float y, u64 sequence,
                Platform::PointerButton button = Platform::PointerButton::Primary) noexcept
{
    auto input = pointerInput(window, kind, x, y, sequence, button);
    input.pointer = pointer;
    return input;
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
    expectOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

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
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::PointerCancel, 10.0F, 10.0F, 1));
    ASSERT_FALSE(rejectedSyntheticCancel.has_value());
    EXPECT_EQ(rejectedSyntheticCancel.error().code, UI::UIErrorCode::InvalidPointerInput);

    auto down =
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(down->pointQuery.target.node, target);
    EXPECT_EQ(down->routedTarget.node, target);
    EXPECT_FALSE(down->routedThroughPointerCapture);
    EXPECT_TRUE(down->pointerCaptureChanged);
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), panel);

    auto move = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Move, 110.0F, 10.0F, 2));
    ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    EXPECT_EQ(move->pointQuery.target.node, physicalOther);
    EXPECT_EQ(move->routedTarget.node, panel);
    EXPECT_TRUE(move->routedThroughPointerCapture);
    EXPECT_TRUE(move->hasRoutedTarget());
    EXPECT_TRUE(move->pointerCaptureChanged);
    EXPECT_TRUE(capturedMoveObserved);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());

    auto uncapturedMove =
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Move, 110.0F, 10.0F, 3));
    ASSERT_TRUE(uncapturedMove.has_value()) << (uncapturedMove ? "" : uncapturedMove.error().message);
    EXPECT_EQ(uncapturedMove->pointQuery.target.node, physicalOther);
    EXPECT_EQ(uncapturedMove->routedTarget.node, physicalOther);
    EXPECT_FALSE(uncapturedMove->routedThroughPointerCapture);
}

TEST_F(UIPointerCaptureTest, MultiplePointersKeepCaptureAndArmsIndependent)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId first = createButton(updater, rootNode);
    const UI::UINodeId second = createButton(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(first, overlay(0.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(second, overlay(100.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    auto firstDown = context->input().routePointerInput(
        pointerInputFor(window, 1, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(firstDown.has_value()) << (firstDown ? "" : firstDown.error().message);
    EXPECT_EQ(firstDown->pointQuery.target.node, first);
    EXPECT_TRUE(firstDown->pointerCaptureChanged);

    auto secondDown = context->input().routePointerInput(
        pointerInputFor(window, 2, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 2));
    ASSERT_TRUE(secondDown.has_value()) << (secondDown ? "" : secondDown.error().message);
    EXPECT_EQ(secondDown->pointQuery.target.node, second);
    EXPECT_TRUE(secondDown->pointerCaptureChanged);
    EXPECT_EQ(context->input().pointerCapture(1), first);
    EXPECT_EQ(context->input().pointerCapture(2), second);

    auto firstMove = context->input().routePointerInput(
        pointerInputFor(window, 1, UI::UIRoutedPointerEventKind::Move, 90.0F, 90.0F, 3));
    ASSERT_TRUE(firstMove.has_value()) << (firstMove ? "" : firstMove.error().message);
    EXPECT_EQ(firstMove->routedTarget.node, first);
    EXPECT_TRUE(firstMove->routedThroughPointerCapture);

    auto secondUp = context->input().routePointerInput(
        pointerInputFor(window, 2, UI::UIRoutedPointerEventKind::ButtonUp, 110.0F, 10.0F, 4));
    ASSERT_TRUE(secondUp.has_value()) << (secondUp ? "" : secondUp.error().message);
    EXPECT_TRUE(secondUp->routedThroughPointerCapture);
    EXPECT_FALSE(context->input().pointerCapture(2).hasValue());
    EXPECT_EQ(context->input().pointerCapture(1), first);

    auto firstUp = context->input().routePointerInput(
        pointerInputFor(window, 1, UI::UIRoutedPointerEventKind::ButtonUp, 90.0F, 90.0F, 5));
    ASSERT_TRUE(firstUp.has_value()) << (firstUp ? "" : firstUp.error().message);
    EXPECT_FALSE(context->input().pointerCapture(1).hasValue());
}

// A single pointer lifting is routine and independent on a touch device. Cancelling
// it must not disturb any other pointer -- releasing all of them would let one finger
// drop the control another is holding, which is the multi-touch defect ADR 0032 cites
// from cocos2d-x. Before the scoped cancel existed, the only cancel entry point
// cleared all eight slots, so the platform could report "pointer 2 is gone" and UI
// could only forget everything.
TEST_F(UIPointerCaptureTest, CancellingOnePointerLeavesTheOthersHolding)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId first = createButton(updater, rootNode);
    const UI::UINodeId second = createButton(updater, rootNode);
    const UI::UINodeId third = createButton(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(300.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(first, overlay(0.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(second, overlay(100.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(third, overlay(200.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(context->publication().commitLayout({.width = 300.0F, .height = 100.0F}));

    // Three fingers, each holding its own control.
    for (const auto& [pointer, node, x] : {std::tuple{Platform::PointerId{1}, first, 10.0F},
                                           std::tuple{Platform::PointerId{2}, second, 110.0F},
                                           std::tuple{Platform::PointerId{3}, third, 210.0F}})
    {
        auto down = context->input().routePointerInput(pointerInputFor(
            window, pointer, UI::UIRoutedPointerEventKind::ButtonDown, x, 10.0F, pointer));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
        ASSERT_EQ(context->input().pointerCapture(pointer), node);
    }

    // The middle finger lifts.
    ASSERT_TRUE(context->input().cancelPointerInteraction(window, 2).has_value());
    EXPECT_FALSE(context->input().pointerCapture(2).hasValue());
    EXPECT_EQ(context->input().pointerCapture(1), first)
        << "cancelling pointer 2 released pointer 1's capture";
    EXPECT_EQ(context->input().pointerCapture(3), third)
        << "cancelling pointer 2 released pointer 3's capture";

    // The surviving pointers still route through their own capture afterwards.
    auto firstMove = context->input().routePointerInput(
        pointerInputFor(window, 1, UI::UIRoutedPointerEventKind::Move, 250.0F, 90.0F, 10));
    ASSERT_TRUE(firstMove.has_value()) << (firstMove ? "" : firstMove.error().message);
    EXPECT_EQ(firstMove->routedTarget.node, first);
    EXPECT_TRUE(firstMove->routedThroughPointerCapture);

    // And the cancelled slot is reusable: a new touch there captures normally.
    auto reuse = context->input().routePointerInput(
        pointerInputFor(window, 2, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 11));
    ASSERT_TRUE(reuse.has_value()) << (reuse ? "" : reuse.error().message);
    EXPECT_EQ(context->input().pointerCapture(2), second);
}

// The window-wide form must keep working: focus loss and stream resets are not about
// one device, and every pointer has to let go.
TEST_F(UIPointerCaptureTest, CancellingWithoutAPointerStillReleasesEveryPointer)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    const UI::UINodeId rootNode = root.rootNodeId();
    const UI::UINodeId first = createButton(updater, rootNode);
    const UI::UINodeId second = createButton(updater, rootNode);
    expectOk(updater.setLayoutStyle(rootNode, fixedSize(200.0F, 100.0F)));
    expectOk(updater.setLayoutStyle(first, overlay(0.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(updater.setLayoutStyle(second, overlay(100.0F, 0.0F, 60.0F, 40.0F)));
    expectOk(context->publication().commitLayout({.width = 200.0F, .height = 100.0F}));

    ASSERT_TRUE(context->input()
                    .routePointerInput(pointerInputFor(
                        window, 1, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1))
                    .has_value());
    ASSERT_TRUE(context->input()
                    .routePointerInput(pointerInputFor(
                        window, 2, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 2))
                    .has_value());
    ASSERT_EQ(context->input().pointerCapture(1), first);
    ASSERT_EQ(context->input().pointerCapture(2), second);

    ASSERT_TRUE(context->input().cancelPointerInteraction(window).has_value());
    EXPECT_FALSE(context->input().pointerCapture(1).hasValue());
    EXPECT_FALSE(context->input().pointerCapture(2).hasValue());
}

// A slot outside the fixed capacity is refused rather than silently ignored or used
// to index the table.
TEST_F(UIPointerCaptureTest, CancellingAnOutOfRangePointerFailsClosed)
{
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    expectOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));

    auto status = context->input().cancelPointerInteraction(
        window, static_cast<Platform::PointerId>(Platform::PointerCapacity));
    EXPECT_FALSE(status);
    if (!status)
    {
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidPointerInput);
    }
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
    expectOk(context->publication().commitLayout({.width = 320.0F, .height = 100.0F}));

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
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), button);
    auto moveOutside =
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::Move, 310.0F, 90.0F, 2));
    ASSERT_TRUE(moveOutside.has_value());
    EXPECT_FALSE(moveOutside->pointQuery.hasTarget());
    EXPECT_EQ(moveOutside->routedTarget.node, button);
    auto up =
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 310.0F, 90.0F, 3));
    ASSERT_TRUE(up.has_value());
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_EQ(buttonCancelCount, 0);

    down = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 4));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), slider);
    expectOk(context->input().cancelPointerInteraction(window));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_EQ(sliderCancelCount, 0);

    down = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 110.0F, 10.0F, 5));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), slider);
    expectOk(updater.destroy(slider));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_EQ(sliderCancelCount, 1);
    EXPECT_TRUE(cancelListenerDestroyedTarget);
    expectOk(context->publication().commitLayout({.width = 320.0F, .height = 100.0F}));

    down = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 210.0F, 10.0F, 6));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), textEdit);
    expectOk(updater.setEnabled(textEdit, false));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_EQ(textEditCancelCount, 1);
    expectOk(updater.setEnabled(textEdit, true));
    expectOk(context->publication().commitLayout({.width = 320.0F, .height = 100.0F}));

    down = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 210.0F, 10.0F, 7));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), textEdit);
    UI::UILayoutStyle hiddenTextEdit = overlay(200.0F, 0.0F, 60.0F, 40.0F);
    hiddenTextEdit.visibility = UI::UIVisibility::Hidden;
    expectOk(updater.setLayoutStyle(textEdit, hiddenTextEdit));
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), textEdit);
    expectOk(context->publication().commitLayout({.width = 320.0F, .height = 100.0F}));
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_EQ(textEditCancelCount, 2);

    down = context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 8));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(context->input().pointerCapture(Platform::PrimaryPointerId), button);
    const UI::UINodeId modal = createModal(updater, rootNode);
    const UI::UINodeId modalButton = createButton(updater, modal);
    expectOk(updater.setLayoutStyle(modal, overlay(80.0F, 10.0F, 120.0F, 70.0F)));
    expectOk(updater.setLayoutStyle(modalButton, fixedSize(50.0F, 20.0F)));
    expectOk(context->publication().commitLayout({.width = 320.0F, .height = 100.0F}));
    EXPECT_EQ(context->input().activeModal(), modal);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
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
    expectOk(context->publication().commitLayout({.width = 100.0F, .height = 100.0F}));
    const usize allocationsBeforeRoutes = resource.allocationCount();

    auto down =
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 10.0F, 10.0F, 1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    for (u64 sequence = 2; sequence < 302; ++sequence)
    {
        auto move = context->input().routePointerInput(
            pointerInput(window, UI::UIRoutedPointerEventKind::Move, 90.0F, 90.0F, sequence));
        ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
        EXPECT_TRUE(move->routedThroughPointerCapture);
        EXPECT_EQ(move->routedTarget.node, button);
    }
    auto up =
        context->input().routePointerInput(pointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 90.0F, 90.0F, 302));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_FALSE(context->input().pointerCapture(Platform::PrimaryPointerId).hasValue());
    EXPECT_EQ(resource.allocationCount(), allocationsBeforeRoutes);
}

} // namespace
} // namespace Tina::Tests
