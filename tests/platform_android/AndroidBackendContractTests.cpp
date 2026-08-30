#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/android/AndroidPlatformFactory.hpp>

#include "WindowSurfaceLeaseAccess.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <variant>

namespace Tina::Platform {
namespace {

// A plausible mid-range phone: 1080x2400 at 3x density.
[[nodiscard]] AndroidPlatformBackendCreateParams validParams() noexcept
{
    return AndroidPlatformBackendCreateParams{
        .platform = {},
        .window = {.nativeWindow = 0x1000},
        .framebufferExtent = FramebufferExtent{1080, 2400},
        .contentScale = ContentScale{3.0F, 3.0F},
    };
}

[[nodiscard]] IAndroidPlatformBackend&
androidFacet(Integration::IWindowSurfacePlatformBackend& backend) noexcept
{
    // A dynamic_cast rather than new IPlatformBackend virtuals: no desktop backend could implement the
    // Android lifecycle or soft keyboard, and adding them to the shared interface would force GLFW,
    // Headless and every test double to carry methods they must reject.
    auto* facet = dynamic_cast<IAndroidPlatformBackend*>(&backend);
    return *facet;
}

TEST(AndroidPlatformBackendTest, RejectsAMissingNativeWindow)
{
    auto params = validParams();
    params.window.nativeWindow = 0;

    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_FALSE(backend.has_value());
    EXPECT_EQ(backend.error().code, Core::CoreErrorCode::InvalidArgument);
}

// A zero extent or scale is rejected rather than defaulted: either silently mis-sizes every UI
// element downstream, which is far harder to diagnose than a startup failure.
TEST(AndroidPlatformBackendTest, RejectsAnEmptyExtentOrNonPositiveScale)
{
    auto zeroWidth = validParams();
    zeroWidth.framebufferExtent = FramebufferExtent{0, 2400};
    EXPECT_FALSE(createAndroidWindowSurfacePlatformBackend(zeroWidth).has_value());

    auto zeroHeight = validParams();
    zeroHeight.framebufferExtent = FramebufferExtent{1080, 0};
    EXPECT_FALSE(createAndroidWindowSurfacePlatformBackend(zeroHeight).has_value());

    auto zeroScale = validParams();
    zeroScale.contentScale = ContentScale{0.0F, 3.0F};
    EXPECT_FALSE(createAndroidWindowSurfacePlatformBackend(zeroScale).has_value());

    auto negativeScale = validParams();
    negativeScale.contentScale = ContentScale{3.0F, -1.0F};
    EXPECT_FALSE(createAndroidWindowSurfacePlatformBackend(negativeScale).has_value());
}

TEST(AndroidPlatformBackendTest, PublishesTheHandedOverWindowMetrics)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value());
    ASSERT_TRUE(metrics->has_value());
    EXPECT_EQ((*metrics)->framebufferExtent.width, 1080U);
    EXPECT_EQ((*metrics)->framebufferExtent.height, 2400U);
    // Logical size is derived from density, not queried: Android reports physical pixels only.
    EXPECT_EQ((*metrics)->logicalExtent.width, 360U);
    EXPECT_EQ((*metrics)->logicalExtent.height, 800U);
    // An activity owning a live window is foreground and visible.
    EXPECT_TRUE((*metrics)->visible);
    EXPECT_FALSE((*metrics)->minimized);
}

// The lease must carry the Android binding kind with no display pointer -- ANativeWindow* is
// self-contained, and the bgfx decoder rejects a binding that carries one anyway.
TEST(AndroidPlatformBackendTest, LeaseCarriesAnAndroidBindingWithoutADisplay)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto lease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(lease.has_value());

    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*lease);
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->kind, Integration::Detail::NativeWindowBindingKind::Android);
    EXPECT_EQ(binding->nativeDisplay, 0U);
    EXPECT_EQ(binding->nativeWindow, 0x1000U);
}

// One lease pins the binding for the RenderDevice lifetime; a second would let two devices
// believe they own the same surface.
TEST(AndroidPlatformBackendTest, RejectsASecondSurfaceLease)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto first = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(first.has_value());

    auto second = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired);
}

TEST(AndroidPlatformBackendTest, SurfaceSnapshotMatchesTheWindowAndIsNotSuspended)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto snapshot = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->framebufferExtent.width, 1080U);
    EXPECT_EQ(snapshot->framebufferExtent.height, 2400U);
    EXPECT_FALSE(snapshot->suspended);
    EXPECT_NE(snapshot->surfaceRevision, 0U);
}

// Frames advance and carry the window, but no input: there is no JNI bridge yet, and reporting
// "no touches" as if observed would make a missing bridge look like an idle frame.
TEST(AndroidPlatformBackendTest, PolledFramesAdvanceAndCarryNoInput)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto first = (*backend)->pollFrame();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->isContinueFrame());
    ASSERT_NE(first->frame(), nullptr);
    const PlatformFrameId firstId = first->frame()->id();

    // The window is published, so downstream sees a real surface...
    const WindowFrameSnapshot* window = first->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->metrics.framebufferExtent.width, 1080U);
    // ...but no gamepad, and every pointer is absent rather than resting at 0,0. On touch there
    // is no position between taps, so a present-but-idle pointer would latch hover forever.
    EXPECT_TRUE(first->frame()->gamepads().empty());
    for (const auto& pointer : window->input.pointers)
    {
        EXPECT_FALSE(pointer.present);
        EXPECT_TRUE(pointer.heldButtons.none());
    }
    EXPECT_TRUE(window->input.heldKeys.none());

    auto second = (*backend)->pollFrame();
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(second->isContinueFrame());
    ASSERT_NE(second->frame(), nullptr);
    EXPECT_NE(firstId, second->frame()->id());
}

// With a bridge attached, a queued touch must become both a transition and pointer state, and the
// physical pixels Android reports must arrive converted by the content scale.
TEST(AndroidPlatformBackendTest, ATouchDownBecomesATransitionAndAPresentPointer)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;

    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // 300 physical pixels at 3x density is 100 logical units.
    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{.action = AndroidTouchAction::Down,
                                                 .pointerSlot = 0,
                                                 .physicalX = 300.0F,
                                                 .physicalY = 600.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    ASSERT_TRUE(poll->isContinueFrame());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);

    const PointerSnapshot& pointer = window->input.pointers[0];
    EXPECT_TRUE(pointer.present) << "a finger that is down must be present";
    EXPECT_DOUBLE_EQ(pointer.logicalX, 100.0);
    EXPECT_DOUBLE_EQ(pointer.logicalY, 200.0);
    EXPECT_TRUE(pointer.heldButtons.test(static_cast<usize>(PointerButton::Primary)));

    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* button = std::get_if<PointerButtonTransition>(&transitions[0].payload);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->state, DigitalTransition::Down);
    EXPECT_EQ(button->pointer, 0);
    EXPECT_DOUBLE_EQ(button->logicalX, 100.0);
}

// A finger stays down across polls: the snapshot is carried state, not something rebuilt per poll.
TEST(AndroidPlatformBackendTest, APressedPointerStaysPresentAcrossPolls)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 0, .physicalX = 30.0F, .physicalY = 30.0F}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    // Second poll drains nothing, yet the finger must still be reported down.
    auto idle = (*backend)->pollFrame();
    ASSERT_TRUE(idle.has_value());
    const WindowFrameSnapshot* window = idle->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.pointers[0].present);
    EXPECT_TRUE(window->input.pointers[0].heldButtons.test(static_cast<usize>(PointerButton::Primary)));
    EXPECT_TRUE(idle->frame()->inputTransitions().empty()) << "an idle poll must produce no transitions";
}

// Lifting keeps the position but clears presence: the Up's own coordinates are what a tap is judged
// by, while `present` is the single thing that says the finger is gone.
TEST(AndroidPlatformBackendTest, LiftingAFingerClearsPresenceButKeepsItsPosition)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 0, .physicalX = 30.0F, .physicalY = 30.0F}));
    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Up, .pointerSlot = 0, .physicalX = 60.0F, .physicalY = 90.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);

    const PointerSnapshot& pointer = window->input.pointers[0];
    EXPECT_FALSE(pointer.present);
    EXPECT_TRUE(pointer.heldButtons.none()) << "an absent pointer must not still hold a button";
    EXPECT_DOUBLE_EQ(pointer.logicalX, 20.0) << "the Up position must survive the release";
    EXPECT_EQ(poll->frame()->inputTransitions().size(), 2U);
}

// Two fingers must stay independent. Cancelling one may not disturb the other -- that is the exact
// multi-touch defect ADR 0032 cites from cocos2d-x.
TEST(AndroidPlatformBackendTest, CancellingOneFingerLeavesTheOtherHolding)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 0, .physicalX = 30.0F, .physicalY = 30.0F}));
    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 1, .physicalX = 90.0F, .physicalY = 90.0F}));
    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Cancel, .pointerSlot = 1, .physicalX = 90.0F, .physicalY = 90.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);

    EXPECT_TRUE(window->input.pointers[0].present) << "the surviving finger must keep holding";
    EXPECT_TRUE(window->input.pointers[0].heldButtons.test(static_cast<usize>(PointerButton::Primary)));
    EXPECT_FALSE(window->input.pointers[1].present);
    EXPECT_TRUE(window->input.pointers[1].heldButtons.none());

    // The cancel must name its pointer; nullopt would mean all eight.
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 3U);
    const auto* cancel = std::get_if<InputCancelTransition>(&transitions[2].payload);
    ASSERT_NE(cancel, nullptr);
    ASSERT_TRUE(cancel->pointer.has_value());
    EXPECT_EQ(*cancel->pointer, 1);
}

TEST(AndroidPlatformBackendTest, AMoveReportsADeltaFromThePreviousPosition)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 0, .physicalX = 30.0F, .physicalY = 30.0F}));
    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Move, .pointerSlot = 0, .physicalX = 90.0F, .physicalY = 30.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* move = std::get_if<PointerMoveTransition>(&transitions[1].payload);
    ASSERT_NE(move, nullptr);
    // 90 - 30 physical pixels at 3x is 20 logical units.
    EXPECT_DOUBLE_EQ(move->deltaX, 20.0);
    EXPECT_DOUBLE_EQ(move->deltaY, 0.0);
    EXPECT_DOUBLE_EQ(move->logicalX, 30.0);
}

// --- Key input ---

TEST(AndroidPlatformBackendTest, AKeyDownBecomesATransitionAndAHeldKey)
{
    auto keys = std::make_shared<AndroidKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // KEYCODE_DPAD_UP. Translated in C++, so the queue carries the raw Android code.
    ASSERT_TRUE(keys->tryPush(AndroidKeyEvent{.action = AndroidKeyAction::Down, .androidKeyCode = 19}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.heldKeys.test(static_cast<usize>(Key::Up)));

    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* keyTransition = std::get_if<KeyTransition>(&transitions[0].payload);
    ASSERT_NE(keyTransition, nullptr);
    EXPECT_EQ(keyTransition->key, Key::Up);
    EXPECT_EQ(keyTransition->state, DigitalTransition::Down);
    EXPECT_FALSE(keyTransition->repeat);
}

// A held key stays held across polls, like a pressed finger: the snapshot is carried state.
TEST(AndroidPlatformBackendTest, AHeldKeyStaysHeldUntilItsUpArrives)
{
    auto keys = std::make_shared<AndroidKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(keys->tryPush(AndroidKeyEvent{.action = AndroidKeyAction::Down, .androidKeyCode = 66}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    auto idle = (*backend)->pollFrame();
    ASSERT_TRUE(idle.has_value());
    EXPECT_TRUE(idle->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Enter)));
    EXPECT_TRUE(idle->frame()->inputTransitions().empty());

    ASSERT_TRUE(keys->tryPush(AndroidKeyEvent{.action = AndroidKeyAction::Up, .androidKeyCode = 66}));
    auto released = (*backend)->pollFrame();
    ASSERT_TRUE(released.has_value());
    EXPECT_FALSE(released->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Enter)));
}

// Android reports key-hold repeats. They are forwarded rather than dropped, because held-key navigation
// depends on them -- but a repeat must not look like a fresh press to a consumer that ignores the flag.
TEST(AndroidPlatformBackendTest, ForwardsKeyRepeatsWithTheFlagSet)
{
    auto keys = std::make_shared<AndroidKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(keys->tryPush(
        AndroidKeyEvent{.action = AndroidKeyAction::Down, .androidKeyCode = 20, .repeat = false}));
    ASSERT_TRUE(
        keys->tryPush(AndroidKeyEvent{.action = AndroidKeyAction::Down, .androidKeyCode = 20, .repeat = true}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    EXPECT_FALSE(std::get_if<KeyTransition>(&transitions[0].payload)->repeat);
    EXPECT_TRUE(std::get_if<KeyTransition>(&transitions[1].payload)->repeat);
    // The key stays held throughout; a repeat is not a release-then-press.
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Down)));
}

// An unmapped code is dropped rather than published as Key::Unknown, which is never actionable.
TEST(AndroidPlatformBackendTest, DropsUnmappedKeyCodes)
{
    auto keys = std::make_shared<AndroidKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // KEYCODE_VOLUME_UP belongs to the system.
    ASSERT_TRUE(keys->tryPush(AndroidKeyEvent{.action = AndroidKeyAction::Down, .androidKeyCode = 24}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->inputTransitions().empty());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.none());
}

// A key held when the window dies never delivers an Up, so leaving the bit set latches it for the rest
// of the run -- the same failure mode as a stranded finger.
TEST(AndroidPlatformBackendTest, LosingTheWindowReleasesHeldKeys)
{
    auto keys = std::make_shared<AndroidKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    ASSERT_TRUE(keys->tryPush(AndroidKeyEvent{.action = AndroidKeyAction::Down, .androidKeyCode = 19}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.none())
        << "no key may survive the window it was pressed on";
}

// --- Committed text input ---

TEST(AndroidPlatformBackendTest, CommittedTextBecomesATransition)
{
    auto texts = std::make_shared<AndroidTextEventQueue>();
    auto params = validParams();
    params.textEvents = texts;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    AndroidTextEvent commit{};
    ASSERT_TRUE(makeAndroidTextEvent("hi", commit));
    ASSERT_TRUE(texts->tryPush(commit));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* text = std::get_if<TextInputTransition>(&transitions[0].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, "hi");

    EXPECT_EQ(androidFacet(**backend).publishedTextCommitCount(), 1U);
}

// Committed text carries no held state, unlike a key: it is a one-shot event, so nothing may linger in
// the snapshot and an idle poll must produce nothing.
TEST(AndroidPlatformBackendTest, CommittedTextLeavesNoHeldStateBehind)
{
    auto texts = std::make_shared<AndroidTextEventQueue>();
    auto params = validParams();
    params.textEvents = texts;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    AndroidTextEvent commit{};
    ASSERT_TRUE(makeAndroidTextEvent("x", commit));
    ASSERT_TRUE(texts->tryPush(commit));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    auto idle = (*backend)->pollFrame();
    ASSERT_TRUE(idle.has_value());
    EXPECT_TRUE(idle->frame()->inputTransitions().empty());
    EXPECT_TRUE(idle->frame()->primaryWindow()->input.heldKeys.none());
    EXPECT_EQ(androidFacet(**backend).publishedTextCommitCount(), 1U)
        << "an idle poll must not re-publish a commit";
}

// Multi-byte text must survive intact. The transition borrows a view, so the event has to outlive the
// append -- getting that wrong produces garbage rather than a compile error.
TEST(AndroidPlatformBackendTest, PublishesMultipleCommitsWithTheirBytesIntact)
{
    auto texts = std::make_shared<AndroidTextEventQueue>();
    auto params = validParams();
    params.textEvents = texts;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    AndroidTextEvent first{};
    AndroidTextEvent second{};
    ASSERT_TRUE(makeAndroidTextEvent("中文", first));
    ASSERT_TRUE(makeAndroidTextEvent("ok", second));
    ASSERT_TRUE(texts->tryPush(first));
    ASSERT_TRUE(texts->tryPush(second));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    EXPECT_EQ(std::get_if<TextInputTransition>(&transitions[0].payload)->committedUtf8, "中文");
    EXPECT_EQ(std::get_if<TextInputTransition>(&transitions[1].payload)->committedUtf8, "ok");
    EXPECT_EQ(androidFacet(**backend).publishedTextCommitCount(), 2U);
}

// A configuration with no text queue is valid: a backend used only for touch should not require one.
TEST(AndroidPlatformBackendTest, RunsWithoutATextQueue)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    ASSERT_TRUE((*backend)->pollFrame().has_value());
    EXPECT_EQ(androidFacet(**backend).publishedTextCommitCount(), 0U);
}

// An out-of-range slot means a producer bug, not a busy device -- the queue already reports
// pressure by dropping. Rejecting stops it corrupting an unrelated pointer's state.
TEST(AndroidPlatformBackendTest, RejectsAnOutOfRangePointerSlot)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{.action = AndroidTouchAction::Down,
                                                 .pointerSlot = static_cast<u8>(PointerCapacity),
                                                 .physicalX = 0.0F,
                                                 .physicalY = 0.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_FALSE(poll.has_value());
    EXPECT_EQ(poll.error().code, Core::CoreErrorCode::InvalidArgument);
}

// --- Surface rebind (ADR 0034 C3) ---

// Losing the window suspends the surface without ending the run: the GPU resources are genuinely
// gone, but ADR 0034 is explicit that a suspended surface is not a lost device.
TEST(AndroidPlatformBackendTest, LosingTheWindowSuspendsTheSurfaceAndKeepsPolling)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());
    ASSERT_FALSE(before->suspended);

    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->suspended);
    EXPECT_GT(after->surfaceRevision, before->surfaceRevision);
    // Only the backbuffer died; the binding itself has not been replaced yet.
    EXPECT_EQ(after->nativeBindingRevision, before->nativeBindingRevision);
    // The run continues -- the engine keeps ticking, it just must not draw.
    EXPECT_TRUE((*backend)->pollFrame().has_value());
}

// Android delivers TERM_WINDOW without a preceding INIT during teardown, so a second call must be a
// no-op rather than turning a normal lifecycle into an error.
TEST(AndroidPlatformBackendTest, LosingTheWindowTwiceIsIdempotent)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    const auto first = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(first.has_value());

    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    const auto second = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->surfaceRevision, first->surfaceRevision) << "a repeated destroy must not churn revisions";
}

// A drag interrupted by a task switch must not strand its finger -- the cocos2d-x defect ADR 0032
// cites, where a backgrounded gesture kept its slot until process exit.
TEST(AndroidPlatformBackendTest, LosingTheWindowReleasesEveryHeldFinger)
{
    auto queue = std::make_shared<AndroidTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 0, .physicalX = 30.0F, .physicalY = 30.0F}));
    ASSERT_TRUE(queue->tryPush(AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = 3, .physicalX = 60.0F, .physicalY = 60.0F}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    for (const auto& pointer : window->input.pointers)
    {
        EXPECT_FALSE(pointer.present);
        EXPECT_TRUE(pointer.heldButtons.none()) << "no finger may survive the window it was touching";
    }
}

// The rebind itself: a replacement window must advance all three revisions together, because the
// tracker rejects a binding change that arrives without new surface and metrics revisions.
TEST(AndroidPlatformBackendTest, AReplacementWindowAdvancesAllThreeRevisionsTogether)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());

    // Rotated: the activity commonly comes back with different geometry, which is often why it was
    // recreated at all.
    ASSERT_TRUE(android
                    .onNativeWindowCreated(AndroidNativeWindowHandle{.nativeWindow = 0x2000},
                                           FramebufferExtent{2400, 1080}, ContentScale{3.0F, 3.0F})
                    .has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->suspended);
    EXPECT_EQ(after->framebufferExtent.width, 2400U);
    EXPECT_GT(after->nativeBindingRevision, before->nativeBindingRevision);
    EXPECT_GT(after->surfaceRevision, before->surfaceRevision);
    EXPECT_GT(after->sourceMetricsRevision, before->sourceMetricsRevision);
}

// The replacement window must be leasable: allowing a fresh lease is the entire point of a rebind,
// and the old lease no longer describes reality.
TEST(AndroidPlatformBackendTest, AReplacementWindowCanBeLeasedAgainWithItsNewHandle)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    auto firstLease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(firstLease.has_value());
    // Dropped, as the render device would on teardown, so the lease control has no active lease.
    firstLease = Core::Result<Integration::NativeWindowSurfaceLease>{Integration::NativeWindowSurfaceLease{}};

    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    ASSERT_TRUE(android
                    .onNativeWindowCreated(AndroidNativeWindowHandle{.nativeWindow = 0x3000},
                                           FramebufferExtent{1080, 2400}, ContentScale{3.0F, 3.0F})
                    .has_value());

    auto secondLease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(secondLease.has_value());
    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*secondLease);
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->nativeWindow, 0x3000U) << "the lease must describe the replacement window";
}

// A background/foreground cycle delivers TERM_WINDOW and INIT_WINDOW back to back with no poll in
// between. The revision must still advance exactly once, because RenderSurfaceStateTracker rejects a
// jump of two -- and that rejection is invisible on screen: it just goes black.
//
// Found on the emulator, not here: the first implementation incremented on every lifecycle call and
// every frame after a resume was refused with "Render surface revision must advance exactly once for
// each committed state change".
TEST(AndroidPlatformBackendTest, ALifecycleCycleWithoutAPollAdvancesTheRevisionOnlyOnce)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    // Observe the starting state, as a frame would.
    ASSERT_TRUE((*backend)->pollFrame().has_value());
    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());

    // No poll between these two, exactly as Android sequences them.
    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    ASSERT_TRUE(android
                    .onNativeWindowCreated(AndroidNativeWindowHandle{.nativeWindow = 0x5000},
                                           FramebufferExtent{1080, 2400}, ContentScale{3.0F, 3.0F})
                    .has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->surfaceRevision, before->surfaceRevision + 1)
        << "two lifecycle events with no poll between them must still be one revision step";
    EXPECT_FALSE(after->suspended);

    // And once observed, the next change gets its own step.
    ASSERT_TRUE((*backend)->pollFrame().has_value());
    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    const auto next = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->surfaceRevision, after->surfaceRevision + 1);
}

TEST(AndroidPlatformBackendTest, AReplacementWindowRejectsInvalidGeometry)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);
    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());

    // Same validation as creation: a rebind accepting what the factory rejects would be a hole
    // straight into bgfx::reset.
    EXPECT_FALSE(android
                     .onNativeWindowCreated(AndroidNativeWindowHandle{.nativeWindow = 0},
                                            FramebufferExtent{1080, 2400}, ContentScale{3.0F, 3.0F})
                     .has_value());
    EXPECT_FALSE(android
                     .onNativeWindowCreated(AndroidNativeWindowHandle{.nativeWindow = 0x4000},
                                            FramebufferExtent{0, 2400}, ContentScale{3.0F, 3.0F})
                     .has_value());
    EXPECT_FALSE(android
                     .onNativeWindowCreated(AndroidNativeWindowHandle{.nativeWindow = 0x4000},
                                            FramebufferExtent{1080, 2400}, ContentScale{0.0F, 3.0F})
                     .has_value());
}

// --- C6: soft keyboard ---

// Requests are latched intent, not actions: only Java can call InputMethodManager. Reading clears,
// so one request produces exactly one IME call instead of being re-applied every frame.
TEST(AndroidPlatformBackendTest, SoftKeyboardRequestsLatchOnceAndClearOnRead)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    EXPECT_EQ(android.takePendingSoftKeyboardRequest(), AndroidSoftKeyboardRequest::None);

    ASSERT_TRUE(android.requestShowSoftKeyboard().has_value());
    EXPECT_EQ(android.takePendingSoftKeyboardRequest(), AndroidSoftKeyboardRequest::Show);
    EXPECT_EQ(android.takePendingSoftKeyboardRequest(), AndroidSoftKeyboardRequest::None)
        << "a consumed request must not be re-applied on the next poll";

    ASSERT_TRUE(android.requestHideSoftKeyboard().has_value());
    EXPECT_EQ(android.takePendingSoftKeyboardRequest(), AndroidSoftKeyboardRequest::Hide);
}

// The last request wins. A show immediately followed by a hide must not leave the keyboard up.
TEST(AndroidPlatformBackendTest, TheLatestSoftKeyboardRequestSupersedesTheEarlierOne)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    ASSERT_TRUE(android.requestShowSoftKeyboard().has_value());
    ASSERT_TRUE(android.requestHideSoftKeyboard().has_value());
    EXPECT_EQ(android.takePendingSoftKeyboardRequest(), AndroidSoftKeyboardRequest::Hide);
}

// Occlusion is reported by the host, never derived: Android's IME height depends on the keyboard
// app, the language, the suggestion strip and split-screen geometry.
TEST(AndroidPlatformBackendTest, SoftKeyboardOcclusionIsReportedInPhysicalAndReadInLogicalUnits)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    EXPECT_FLOAT_EQ(android.softKeyboardOccludedLogicalHeight(), 0.0F) << "hidden means zero";

    // 900 physical pixels at 3x density is 300 logical units.
    ASSERT_TRUE(android.onSoftKeyboardOcclusionChanged(900).has_value());
    EXPECT_FLOAT_EQ(android.softKeyboardOccludedLogicalHeight(), 300.0F);

    ASSERT_TRUE(android.onSoftKeyboardOcclusionChanged(0).has_value());
    EXPECT_FLOAT_EQ(android.softKeyboardOccludedLogicalHeight(), 0.0F);
}

// A keyboard taller than the window means the host measured against different geometry than the
// backend holds. Clamping would hide that disagreement.
TEST(AndroidPlatformBackendTest, RejectsAnOcclusionTallerThanTheWindow)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);

    EXPECT_FALSE(android.onSoftKeyboardOcclusionChanged(2401).has_value());
    EXPECT_TRUE(android.onSoftKeyboardOcclusionChanged(2400).has_value())
        << "exactly the window height must remain usable";
}

// --- Composing (preedit) text ---

namespace {

[[nodiscard]] AndroidCompositionEvent composingText(std::u16string_view utf16, i32 cursor = 0) noexcept
{
    AndroidCompositionEvent event{};
    const bool built = makeAndroidCompositionEventFromUtf16(
        utf16, cursor, AndroidCompositionAction::SetText, event);
    // Not an assertion macro: this helper is used in the middle of expressions, and a failed build here
    // would be a bug in the test rather than in the code under test.
    return built ? event : AndroidCompositionEvent{};
}

[[nodiscard]] AndroidCompositionEvent committedText(std::u16string_view utf16) noexcept
{
    AndroidCompositionEvent event{};
    const bool built =
        makeAndroidCompositionEventFromUtf16(utf16, 0, AndroidCompositionAction::Commit, event);
    return built ? event : AndroidCompositionEvent{};
}

[[nodiscard]] AndroidCompositionEvent finishComposing() noexcept
{
    AndroidCompositionEvent event{};
    event.action = AndroidCompositionAction::Finish;
    return event;
}

} // namespace

// The first composing text of a pass is Started, and subsequent ones are Updated. Android's call is
// identical in both cases, so the distinction exists only in the session's memory -- getting it wrong
// means a consumer either never sees a composition begin, or sees it begin twice.
TEST(AndroidPlatformBackendTest, TheFirstComposingTextStartsAndLaterOnesUpdate)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"ni", 2)));
    ASSERT_TRUE(compositions->tryPush(composingText(u"nihao", 5)));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);

    const auto* first = std::get_if<TextCompositionTransition>(&transitions[0].payload);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->stage, TextCompositionStage::Started);
    EXPECT_EQ(first->preeditUtf8, "ni");
    EXPECT_EQ(first->cursorCodepoint, 2U);

    const auto* second = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->stage, TextCompositionStage::Updated);
    EXPECT_EQ(second->preeditUtf8, "nihao");

    IAndroidPlatformBackend& android = androidFacet(**backend);
    EXPECT_EQ(android.publishedCompositionStartCount(), 1U);
    EXPECT_EQ(android.publishedCompositionUpdateCount(), 1U);
}

// The ordering case, and the reason commits share the composition queue at all. Ended must precede the
// text it resolved into: reversing them still "works" because UI clears the composition on commit, but
// the frame then reads as "text appeared, then the composition ended", which is wrong for anything
// rebuilding IME state from the stage sequence.
TEST(AndroidPlatformBackendTest, ACommitEndsTheCompositionBeforePublishingItsText)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"nihao", 5)));
    ASSERT_TRUE(compositions->tryPush(committedText(u"你好")));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 3U);

    ASSERT_NE(std::get_if<TextCompositionTransition>(&transitions[0].payload), nullptr);
    const auto* ended = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(ended, nullptr) << "the stage must come before the text";
    EXPECT_EQ(ended->stage, TextCompositionStage::Ended);
    EXPECT_TRUE(ended->preeditUtf8.empty()) << "Ended carries no preedit";

    const auto* text = std::get_if<TextInputTransition>(&transitions[2].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, "你好");

    IAndroidPlatformBackend& android = androidFacet(**backend);
    EXPECT_EQ(android.publishedCompositionEndCount(), 1U);
    EXPECT_EQ(android.publishedTextCommitCount(), 1U);
}

// A commit with nothing in flight is the pre-preedit behaviour and must stay exactly that: an ASCII
// keyboard, a paste and an autocomplete pick all arrive this way, and inventing an Ended for them would
// announce the end of a composition that never happened.
TEST(AndroidPlatformBackendTest, ACommitWithNoCompositionPublishesTextAlone)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(committedText(u"hi")));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* text = std::get_if<TextInputTransition>(&transitions[0].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, "hi");
    EXPECT_EQ(androidFacet(**backend).publishedCompositionEndCount(), 0U);
}

// Deleting the last character of a preedit arrives as setComposingText(""), which Android does not
// distinguish from abandoning the region -- it has no cancel call. Cancelled rather than Ended because
// nothing was produced, and the distinction is load-bearing: UIInputRouteProducer excludes Cancelled
// from flow device observation, so backing out of a composition must not read as user activity.
TEST(AndroidPlatformBackendTest, AnEmptiedPreeditCancelsRatherThanEnds)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"ni", 2)));
    ASSERT_TRUE(compositions->tryPush(composingText(u"")));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* cancelled = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->stage, TextCompositionStage::Cancelled);
    EXPECT_EQ(androidFacet(**backend).publishedCompositionCancelCount(), 1U);
    EXPECT_EQ(androidFacet(**backend).publishedCompositionEndCount(), 0U);
}

// finishComposingText: the region was given up without producing text, so Cancelled for the same
// reason. Not Ended -- that would claim the composition resolved into something.
TEST(AndroidPlatformBackendTest, FinishingComposingTextCancelsTheComposition)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"ni", 2)));
    ASSERT_TRUE(compositions->tryPush(finishComposing()));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* cancelled = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->stage, TextCompositionStage::Cancelled);
}

// Both silent rows of the mapping table. IMEs clear and finish the composing region routinely as they
// attach and detach, and announcing the end of a composition that never started is noise every consumer
// would have to filter -- so nothing is published at all.
TEST(AndroidPlatformBackendTest, ClearingOrFinishingWithNoCompositionPublishesNothing)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"")));
    ASSERT_TRUE(compositions->tryPush(finishComposing()));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->inputTransitions().empty());
    IAndroidPlatformBackend& android = androidFacet(**backend);
    EXPECT_EQ(android.publishedCompositionCancelCount(), 0U);
    EXPECT_EQ(android.publishedCompositionStartCount(), 0U);
}

// A preedit spanning polls keeps its session: the second poll must report Updated, not a second Started.
// The session is state carried across frames, like held keys, not something rebuilt per poll.
TEST(AndroidPlatformBackendTest, ACompositionSurvivesAcrossPolls)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"ni", 2)));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"nihao", 5)));
    auto second = (*backend)->pollFrame();
    ASSERT_TRUE(second.has_value());
    const InputTransitionBatch transitions = second->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* updated = std::get_if<TextCompositionTransition>(&transitions[0].payload);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->stage, TextCompositionStage::Updated);
}

// Losing the window delivers no InputConnection call at all, so without an explicit cancel the UI keeps
// drawing a preedit the IME has long forgotten -- and the next pass would report Updated for a session
// the consumer never saw start. Published on the next poll rather than where it happens, because that
// path runs outside a frame and appending with no frame begun is a failure.
TEST(AndroidPlatformBackendTest, LosingTheWindowCancelsAnInFlightComposition)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(composingText(u"ni", 2)));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    IAndroidPlatformBackend& android = androidFacet(**backend);
    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* cancelled = std::get_if<TextCompositionTransition>(&transitions[0].payload);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->stage, TextCompositionStage::Cancelled);

    // And the session is genuinely clear afterwards: the next composing text starts a new pass.
    ASSERT_TRUE(compositions->tryPush(composingText(u"hao", 3)));
    auto next = (*backend)->pollFrame();
    ASSERT_TRUE(next.has_value());
    const auto* restarted =
        std::get_if<TextCompositionTransition>(&next->frame()->inputTransitions()[0].payload);
    ASSERT_NE(restarted, nullptr);
    EXPECT_EQ(restarted->stage, TextCompositionStage::Started);
}

// Losing the window with nothing in flight must publish nothing, or every background/foreground cycle
// would inject a spurious Cancelled.
TEST(AndroidPlatformBackendTest, LosingTheWindowWithNoCompositionPublishesNothing)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(androidFacet(**backend).onNativeWindowDestroyed().has_value());
    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->inputTransitions().empty());
}

// Astral-plane text must survive the preedit path too, not just commits: an IME predicting an emoji
// shows it in the composing region first. This is the GetStringChars-vs-GetStringUTFChars lesson applied
// to preedit -- and the cursor is in codepoints, so a surrogate pair counts once.
TEST(AndroidPlatformBackendTest, APreeditCarriesAstralPlaneTextWithACodepointCursor)
{
    auto compositions = std::make_shared<AndroidCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // U+1F600 as the surrogate pair Java stores, then 'a'. Cursor offset 3 UTF-16 units = 2 codepoints.
    ASSERT_TRUE(compositions->tryPush(composingText(u"\U0001F600a", 3)));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const auto* started =
        std::get_if<TextCompositionTransition>(&poll->frame()->inputTransitions()[0].payload);
    ASSERT_NE(started, nullptr);
    EXPECT_EQ(started->preeditUtf8, "\U0001F600a");
    EXPECT_EQ(started->cursorCodepoint, 2U)
        << "a surrogate pair is two UTF-16 units but one codepoint";
}

// A backend without a composition queue must still poll. Same contract as the other queues: empty is a
// valid configuration, not a broken one -- an ASCII keyboard never composes.
TEST(AndroidPlatformBackendTest, RunsWithoutACompositionQueue)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->inputTransitions().empty());
    EXPECT_EQ(androidFacet(**backend).publishedCompositionStartCount(), 0U);
}

// --- Caret placement ---

// This case exists because rejecting a placement broke the whole engine, not because accepting one is
// interesting. The Runtime publishes a caret every frame a TextEdit is focused, the coordinator turns a
// rejection into a LifecycleInvariantViolation, and EngineHost latches a terminal outcome -- so the app
// simply stopped producing frames the moment a text field took focus, with the caret as the only cause.
TEST(AndroidPlatformBackendTest, LatchesTheCaretInPhysicalPixels)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IAndroidPlatformBackend& android = androidFacet(**backend);
    EXPECT_FALSE(android.caretPixels().has_value()) << "no caret before one is published";

    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value());
    ASSERT_TRUE(metrics->has_value());

    // 3x density, so logical 10,20 2x16 becomes 30,60 6x48.
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 10.0, .y = 20.0, .width = 2.0, .height = 16.0},
                    })
                    .has_value());
    const auto caret = android.caretPixels();
    ASSERT_TRUE(caret.has_value());
    EXPECT_EQ(*caret, (AndroidCaretPixels{.x = 30, .y = 60, .width = 6, .height = 48}));
}

// Reading must NOT consume, unlike the soft-keyboard request. An IME that asked for cursor updates
// expects the current caret every time it looks; consume-on-read would leave the second query blind.
TEST(AndroidPlatformBackendTest, ReadingTheCaretDoesNotClearIt)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 1.0, .y = 2.0, .width = 0.0, .height = 4.0},
                    })
                    .has_value());

    IAndroidPlatformBackend& android = androidFacet(**backend);
    const auto first = android.caretPixels();
    const auto second = android.caretPixels();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
}

// Clearing stays a successful no-op so callers need no platform branch, and it must actually drop the
// latched value -- a stale caret would have the host anchor candidates to a field that lost focus.
TEST(AndroidPlatformBackendTest, ClearingThePlacementDropsTheCaret)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 4.0},
                    })
                    .has_value());
    ASSERT_TRUE(androidFacet(**backend).caretPixels().has_value());

    EXPECT_TRUE((*backend)->updateTextInputPlacement(std::nullopt).has_value());
    EXPECT_FALSE(androidFacet(**backend).caretPixels().has_value());
}

// Invalid geometry is rejected rather than clamped: substituting a plausible rectangle would place the
// candidate window somewhere arbitrary with nothing to trace it back to. A default-constructed
// placement fails on both counts -- unset window and zero height.
TEST(AndroidPlatformBackendTest, RejectsCaretGeometryItCannotUse)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    const WindowId window = (*metrics)->window;

    EXPECT_FALSE((*backend)->updateTextInputPlacement(TextInputPlacement{}).has_value())
        << "a placement for another window";
    EXPECT_FALSE((*backend)
                     ->updateTextInputPlacement(TextInputPlacement{
                         .window = window,
                         .caret = {.x = 0.0, .y = 0.0, .width = 2.0, .height = 0.0},
                     })
                     .has_value())
        << "a zero-height caret is not a caret";
    EXPECT_FALSE((*backend)
                     ->updateTextInputPlacement(TextInputPlacement{
                         .window = window,
                         .caret = {.x = std::numeric_limits<double>::quiet_NaN(),
                                   .y = 0.0,
                                   .width = 2.0,
                                   .height = 16.0},
                     })
                     .has_value())
        << "non-finite geometry";
    EXPECT_FALSE(androidFacet(**backend).caretPixels().has_value())
        << "a rejected placement must not latch anything";
}

// The caret described a window that no longer exists. Keeping it would have the host anchor the next
// surface's candidates to the old geometry, which a rotation or multi-window resize changes entirely.
TEST(AndroidPlatformBackendTest, LosingTheWindowClearsTheCaret)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 4.0},
                    })
                    .has_value());

    IAndroidPlatformBackend& android = androidFacet(**backend);
    ASSERT_TRUE(android.onNativeWindowDestroyed().has_value());
    EXPECT_FALSE(android.caretPixels().has_value());
}

TEST(AndroidPlatformBackendTest, EveryEntryPointFailsAfterShutdown)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    (*backend)->shutdown();

    EXPECT_FALSE((*backend)->initialPrimaryWindowMetrics().has_value());
    EXPECT_FALSE((*backend)->pollFrame().has_value());
    EXPECT_FALSE((*backend)->publishPrimaryWindow().has_value());
    EXPECT_FALSE((*backend)->acquirePrimaryWindowSurfaceLease().has_value());
    EXPECT_FALSE((*backend)->primaryWindowSurfaceSnapshot().has_value());
    EXPECT_FALSE((*backend)->updateTextInputPlacement(std::nullopt).has_value());
}

// Thread affinity is the contract every production backend shares; ADR 0032's D3 chose external
// frame driving precisely so this owner need not be the platform's own UI thread.
TEST(AndroidPlatformBackendTest, RejectsUseFromAnotherThread)
{
    auto backend = createAndroidWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    Core::ErrorCode observed{};
    std::thread other([&] {
        auto poll = (*backend)->pollFrame();
        ASSERT_FALSE(poll.has_value());
        observed = poll.error().code;
    });
    other.join();
    EXPECT_EQ(observed, PlatformErrorCode::WrongOwnerThread);
}

} // namespace
} // namespace Tina::Platform
