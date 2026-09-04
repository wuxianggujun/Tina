#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/ios/IosPlatformFactory.hpp>

#include "WindowSurfaceLeaseAccess.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace Tina::Platform {
namespace {

// A plausible current iPhone: 1170x2532 drawable at 3x native scale, so 390x844 points.
constexpr std::uintptr_t InitialLayer = 0x1000;

[[nodiscard]] IosPlatformBackendCreateParams validParams() noexcept
{
    return IosPlatformBackendCreateParams{
        .platform = {},
        .layer = {.metalLayer = InitialLayer},
        .framebufferExtent = FramebufferExtent{1170, 2532},
        .contentScale = ContentScale{3.0F, 3.0F},
    };
}

[[nodiscard]] IIosPlatformBackend& iosFacet(Integration::IWindowSurfacePlatformBackend& backend) noexcept
{
    auto* facet = dynamic_cast<IIosPlatformBackend*>(&backend);
    return *facet;
}

TEST(IosPlatformBackendTest, RejectsAMissingLayer)
{
    auto params = validParams();
    params.layer.metalLayer = 0;

    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_FALSE(backend.has_value());
    EXPECT_EQ(backend.error().code, Core::CoreErrorCode::InvalidArgument);
}

TEST(IosPlatformBackendTest, RejectsAnEmptyExtentOrNonPositiveScale)
{
    auto zeroWidth = validParams();
    zeroWidth.framebufferExtent = FramebufferExtent{0, 2532};
    EXPECT_FALSE(createIosWindowSurfacePlatformBackend(zeroWidth).has_value());

    auto zeroHeight = validParams();
    zeroHeight.framebufferExtent = FramebufferExtent{1170, 0};
    EXPECT_FALSE(createIosWindowSurfacePlatformBackend(zeroHeight).has_value());

    auto zeroScale = validParams();
    zeroScale.contentScale = ContentScale{0.0F, 3.0F};
    EXPECT_FALSE(createIosWindowSurfacePlatformBackend(zeroScale).has_value());

    auto negativeScale = validParams();
    negativeScale.contentScale = ContentScale{3.0F, -1.0F};
    EXPECT_FALSE(createIosWindowSurfacePlatformBackend(negativeScale).has_value());
}

TEST(IosPlatformBackendTest, DerivesLogicalPointsFromTheDrawableAndNativeScale)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value());
    ASSERT_TRUE(metrics->has_value());
    EXPECT_EQ((*metrics)->framebufferExtent.width, 1170U);
    EXPECT_EQ((*metrics)->framebufferExtent.height, 2532U);
    // CAMetalLayer.drawableSize is in pixels; points are the only conversion available.
    EXPECT_EQ((*metrics)->logicalExtent.width, 390U);
    EXPECT_EQ((*metrics)->logicalExtent.height, 844U);
    EXPECT_TRUE((*metrics)->visible);
    EXPECT_FALSE((*metrics)->minimized);
}

// The binding must carry the Ios kind with the layer and no display. bgfx's iOS Metal path casts nwh
// straight to a CAMetalLayer, so the kind is what tells the decoder that is safe -- and it rejects a
// binding that carries a display pointer at all.
TEST(IosPlatformBackendTest, LeaseCarriesAnIosBindingWithTheLayerAndNoDisplay)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto lease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(lease.has_value());

    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*lease);
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->kind, Integration::Detail::NativeWindowBindingKind::Ios);
    EXPECT_EQ(binding->nativeDisplay, 0U);
    EXPECT_EQ(binding->nativeWindow, InitialLayer);
}

TEST(IosPlatformBackendTest, RejectsASecondSurfaceLease)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto first = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(first.has_value());

    auto second = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired);
}

TEST(IosPlatformBackendTest, SurfaceSnapshotMatchesTheLayerAndIsNotSuspended)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto snapshot = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->framebufferExtent.width, 1170U);
    EXPECT_FALSE(snapshot->suspended);
    EXPECT_NE(snapshot->surfaceRevision, 0U);
    EXPECT_NE(snapshot->nativeBindingRevision, 0U);
}

TEST(IosPlatformBackendTest, PolledFramesAdvanceAndCarryNoInputWithoutQueues)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    auto first = (*backend)->pollFrame();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->isContinueFrame());
    ASSERT_NE(first->frame(), nullptr);
    const PlatformFrameId firstId = first->frame()->id();

    const WindowFrameSnapshot* window = first->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->metrics.framebufferExtent.width, 1170U);
    EXPECT_TRUE(first->frame()->gamepads().empty());
    // Absent rather than resting at the origin: on touch a finger has no position between taps, so a
    // present-but-idle pointer would latch hover on whatever was last touched (ADR 0032 C2).
    for (const auto& pointer : window->input.pointers)
    {
        EXPECT_FALSE(pointer.present);
        EXPECT_TRUE(pointer.heldButtons.none());
    }
    EXPECT_TRUE(window->input.heldKeys.none());

    auto second = (*backend)->pollFrame();
    ASSERT_TRUE(second.has_value());
    ASSERT_NE(second->frame(), nullptr);
    EXPECT_NE(firstId, second->frame()->id());
}

// --- Touch input ---

// The unit difference from Android, and the one a copied test would get wrong: UITouch reports
// locationInView in *points*, which is already the logical space. Dividing by the content scale here
// would shrink every gesture by 3x on a Retina device, with no error anywhere.
TEST(IosPlatformBackendTest, TouchPointsAreLogicalUnitsAndAreNotScaled)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(IosTouchEvent{
        .phase = IosTouchPhase::Began, .pointerSlot = 0, .pointX = 100.0F, .pointY = 200.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);

    const PointerSnapshot& pointer = window->input.pointers[0];
    EXPECT_TRUE(pointer.present);
    EXPECT_DOUBLE_EQ(pointer.logicalX, 100.0) << "points are already logical; dividing by 3x is the bug";
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

TEST(IosPlatformBackendTest, APressedPointerStaysPresentAcrossPolls)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 0, .pointX = 10.0F, .pointY = 10.0F}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    auto idle = (*backend)->pollFrame();
    ASSERT_TRUE(idle.has_value());
    const WindowFrameSnapshot* window = idle->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.pointers[0].present);
    EXPECT_TRUE(window->input.pointers[0].heldButtons.test(static_cast<usize>(PointerButton::Primary)));
    EXPECT_TRUE(idle->frame()->inputTransitions().empty());
}

TEST(IosPlatformBackendTest, LiftingAFingerClearsPresenceButKeepsItsPosition)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 0, .pointX = 10.0F, .pointY = 10.0F}));
    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Ended, .pointerSlot = 0, .pointX = 20.0F, .pointY = 30.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const PointerSnapshot& pointer = poll->frame()->primaryWindow()->input.pointers[0];
    EXPECT_FALSE(pointer.present);
    EXPECT_TRUE(pointer.heldButtons.none());
    EXPECT_DOUBLE_EQ(pointer.logicalX, 20.0) << "the lift position is what a tap is judged by";
    EXPECT_EQ(poll->frame()->inputTransitions().size(), 2U);
}

// UIKit cancels individual touches when a system gesture claims them. Cancelling one must not disturb
// the others -- the multi-touch defect ADR 0032 cites, where one finger leaving made the rest drop
// whatever they were holding.
TEST(IosPlatformBackendTest, CancellingOneFingerLeavesTheOtherHolding)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 0, .pointX = 10.0F, .pointY = 10.0F}));
    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 1, .pointX = 30.0F, .pointY = 30.0F}));
    ASSERT_TRUE(queue->tryPush(IosTouchEvent{
        .phase = IosTouchPhase::Cancelled, .pointerSlot = 1, .pointX = 30.0F, .pointY = 30.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->input.pointers[0].present) << "the surviving finger must keep holding";
    EXPECT_FALSE(window->input.pointers[1].present);

    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 3U);
    const auto* cancel = std::get_if<InputCancelTransition>(&transitions[2].payload);
    ASSERT_NE(cancel, nullptr);
    ASSERT_TRUE(cancel->pointer.has_value()) << "nullopt would cancel all eight pointers";
    EXPECT_EQ(*cancel->pointer, 1);
}

TEST(IosPlatformBackendTest, AMoveReportsADeltaInPoints)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 0, .pointX = 10.0F, .pointY = 10.0F}));
    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Moved, .pointerSlot = 0, .pointX = 30.0F, .pointY = 10.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* move = std::get_if<PointerMoveTransition>(&transitions[1].payload);
    ASSERT_NE(move, nullptr);
    EXPECT_DOUBLE_EQ(move->deltaX, 20.0);
    EXPECT_DOUBLE_EQ(move->deltaY, 0.0);
    EXPECT_DOUBLE_EQ(move->logicalX, 30.0);
}

TEST(IosPlatformBackendTest, RejectsAnOutOfRangePointerSlot)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(IosTouchEvent{.phase = IosTouchPhase::Began,
                                             .pointerSlot = static_cast<u8>(PointerCapacity),
                                             .pointX = 0.0F,
                                             .pointY = 0.0F}));

    auto poll = (*backend)->pollFrame();
    ASSERT_FALSE(poll.has_value());
    EXPECT_EQ(poll.error().code, Core::CoreErrorCode::InvalidArgument);
}

// --- Hardware keyboard ---

TEST(IosPlatformBackendTest, AKeyDownBecomesATransitionAndAHeldKey)
{
    auto keys = std::make_shared<IosKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // UIKeyboardHIDUsageKeyboardUpArrow, passed through untranslated.
    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Down, .hidUsage = 0x52}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Up)));

    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* key = std::get_if<KeyTransition>(&transitions[0].payload);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->key, Key::Up);
    EXPECT_EQ(key->state, DigitalTransition::Down);
    EXPECT_FALSE(key->repeat);
}

TEST(IosPlatformBackendTest, AHeldKeyStaysHeldUntilItsUpArrives)
{
    auto keys = std::make_shared<IosKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // ReturnOrEnter.
    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Down, .hidUsage = 0x28}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    auto idle = (*backend)->pollFrame();
    ASSERT_TRUE(idle.has_value());
    EXPECT_TRUE(idle->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Enter)));
    EXPECT_TRUE(idle->frame()->inputTransitions().empty());

    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Up, .hidUsage = 0x28}));
    auto released = (*backend)->pollFrame();
    ASSERT_TRUE(released.has_value());
    EXPECT_FALSE(released->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Enter)));
}

TEST(IosPlatformBackendTest, ForwardsKeyRepeatsWithTheFlagSet)
{
    auto keys = std::make_shared<IosKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // DownArrow, pressed then repeating.
    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Down, .hidUsage = 0x51, .repeat = false}));
    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Down, .hidUsage = 0x51, .repeat = true}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    EXPECT_FALSE(std::get_if<KeyTransition>(&transitions[0].payload)->repeat);
    EXPECT_TRUE(std::get_if<KeyTransition>(&transitions[1].payload)->repeat);
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.test(static_cast<usize>(Key::Down)));
}

TEST(IosPlatformBackendTest, DropsUnmappedHidUsages)
{
    auto keys = std::make_shared<IosKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // KeyboardVolumeUp belongs to the system.
    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Down, .hidUsage = 0x80}));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->inputTransitions().empty());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.none());
}

// UIKit stops delivering key events the moment the view resigns first responder, so the Up for a held
// key can simply never arrive. Leaving the bit set latches it for the rest of the run.
TEST(IosPlatformBackendTest, LosingTheLayerReleasesHeldKeys)
{
    auto keys = std::make_shared<IosKeyEventQueue>();
    auto params = validParams();
    params.keyEvents = keys;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(keys->tryPush(IosKeyEvent{.action = IosKeyAction::Down, .hidUsage = 0x52}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(iosFacet(**backend).onNativeLayerReleased().has_value());
    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->primaryWindow()->input.heldKeys.none());
}

// --- Committed text ---

TEST(IosPlatformBackendTest, CommittedTextBecomesATransition)
{
    auto texts = std::make_shared<IosTextEventQueue>();
    auto params = validParams();
    params.textEvents = texts;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    IosTextEvent commit{};
    ASSERT_TRUE(makeIosTextEvent("hi", commit));
    ASSERT_TRUE(texts->tryPush(commit));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    const auto* text = std::get_if<TextInputTransition>(&transitions[0].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, "hi");
    EXPECT_EQ(iosFacet(**backend).publishedTextCommitCount(), 1U);
}

TEST(IosPlatformBackendTest, CommittedTextLeavesNoHeldStateBehind)
{
    auto texts = std::make_shared<IosTextEventQueue>();
    auto params = validParams();
    params.textEvents = texts;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    IosTextEvent commit{};
    ASSERT_TRUE(makeIosTextEvent("x", commit));
    ASSERT_TRUE(texts->tryPush(commit));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    auto idle = (*backend)->pollFrame();
    ASSERT_TRUE(idle.has_value());
    EXPECT_TRUE(idle->frame()->inputTransitions().empty());
    EXPECT_EQ(iosFacet(**backend).publishedTextCommitCount(), 1U);
}

TEST(IosPlatformBackendTest, PublishesMultipleCommitsWithTheirBytesIntact)
{
    auto texts = std::make_shared<IosTextEventQueue>();
    auto params = validParams();
    params.textEvents = texts;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    IosTextEvent first{};
    IosTextEvent second{};
    ASSERT_TRUE(makeIosTextEvent("中文", first));
    ASSERT_TRUE(makeIosTextEvent("ok", second));
    ASSERT_TRUE(texts->tryPush(first));
    ASSERT_TRUE(texts->tryPush(second));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    EXPECT_EQ(std::get_if<TextInputTransition>(&transitions[0].payload)->committedUtf8, "中文");
    EXPECT_EQ(std::get_if<TextInputTransition>(&transitions[1].payload)->committedUtf8, "ok");
    EXPECT_EQ(iosFacet(**backend).publishedTextCommitCount(), 2U);
}

TEST(IosPlatformBackendTest, RunsWithoutAnyQueues)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    ASSERT_TRUE((*backend)->pollFrame().has_value());
    EXPECT_EQ(iosFacet(**backend).publishedTextCommitCount(), 0U);
    EXPECT_EQ(iosFacet(**backend).droppedTouchEventCount(), 0U);
}

// --- Drawable lifecycle (ADR 0034 C3) ---

TEST(IosPlatformBackendTest, LosingTheLayerSuspendsTheSurfaceAndKeepsPolling)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());
    ASSERT_FALSE(before->suspended);

    ASSERT_TRUE(iosFacet(**backend).onNativeLayerReleased().has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->suspended);
    EXPECT_GT(after->surfaceRevision, before->surfaceRevision);
    // Only the drawable died. The Metal device survives, so the binding has not been replaced.
    EXPECT_EQ(after->nativeBindingRevision, before->nativeBindingRevision);
    EXPECT_TRUE((*backend)->pollFrame().has_value());
}

TEST(IosPlatformBackendTest, LosingTheLayerTwiceIsIdempotent)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());
    const auto first = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(first.has_value());

    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());
    const auto second = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->surfaceRevision, first->surfaceRevision);
}

// Worse on iOS than on Android: UIKit delivers no touchesCancelled: at all when a scene is
// disconnected, so nothing else would ever release these fingers.
TEST(IosPlatformBackendTest, LosingTheLayerReleasesEveryHeldFinger)
{
    auto queue = std::make_shared<IosTouchEventQueue>();
    auto params = validParams();
    params.touchEvents = queue;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 0, .pointX = 10.0F, .pointY = 10.0F}));
    ASSERT_TRUE(queue->tryPush(
        IosTouchEvent{.phase = IosTouchPhase::Began, .pointerSlot = 3, .pointX = 20.0F, .pointY = 20.0F}));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(iosFacet(**backend).onNativeLayerReleased().has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    for (const auto& pointer : poll->frame()->primaryWindow()->input.pointers)
    {
        EXPECT_FALSE(pointer.present);
        EXPECT_TRUE(pointer.heldButtons.none());
    }
}

TEST(IosPlatformBackendTest, AReplacementLayerAdvancesAllThreeRevisionsTogether)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());

    // Rotated on the way back, which is often why the layer was recreated at all.
    ASSERT_TRUE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x2000},
                                          FramebufferExtent{2532, 1170}, ContentScale{3.0F, 3.0F})
                    .has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->suspended);
    EXPECT_EQ(after->framebufferExtent.width, 2532U);
    EXPECT_GT(after->nativeBindingRevision, before->nativeBindingRevision);
    EXPECT_GT(after->surfaceRevision, before->surfaceRevision);
    EXPECT_GT(after->sourceMetricsRevision, before->sourceMetricsRevision);
}

TEST(IosPlatformBackendTest, AReplacementLayerCanBeLeasedAgainWithItsNewHandle)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    auto firstLease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(firstLease.has_value());
    // Dropped, as the render device would on teardown, so the lease control has no active lease.
    firstLease = Core::Result<Integration::NativeWindowSurfaceLease>{Integration::NativeWindowSurfaceLease{}};

    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());
    ASSERT_TRUE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x3000},
                                          FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F})
                    .has_value());

    auto secondLease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(secondLease.has_value());
    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*secondLease);
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->nativeWindow, 0x3000U);
}

// The black-screen case. RenderSurfaceStateTracker rejects a revision that jumps by two, and the only
// symptom is that every frame after a resume is refused -- iOS is more exposed than Android because a
// scene reconnection can pair a release, an acquire and a resize before the next CADisplayLink runs.
TEST(IosPlatformBackendTest, ALifecycleCycleWithoutAPollAdvancesTheRevisionOnlyOnce)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE((*backend)->pollFrame().has_value());
    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());

    // Three lifecycle events, no poll between them.
    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());
    ASSERT_TRUE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x5000},
                                          FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F})
                    .has_value());
    ASSERT_TRUE(ios.onDrawableResized(FramebufferExtent{2532, 1170}, ContentScale{3.0F, 3.0F}).has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->surfaceRevision, before->surfaceRevision + 1)
        << "three lifecycle events with no poll between them must still be one revision step";
    EXPECT_FALSE(after->suspended);

    ASSERT_TRUE((*backend)->pollFrame().has_value());
    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());
    const auto next = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->surfaceRevision, after->surfaceRevision + 1);
}

// The iOS-specific split, and the reason onDrawableResized exists at all: the CAMetalLayer object is
// unchanged, so bgfx needs a reset against the new drawable size, not a rebind. Advancing
// nativeBindingRevision here would have the render device throw away a swapchain that is still valid,
// which on Metal is a visible hitch on every rotation.
TEST(IosPlatformBackendTest, AResizeWithoutALayerSwapDoesNotAdvanceTheBindingRevision)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE((*backend)->pollFrame().has_value());
    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(ios.onDrawableResized(FramebufferExtent{2532, 1170}, ContentScale{3.0F, 3.0F}).has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->framebufferExtent.width, 2532U);
    EXPECT_EQ(after->framebufferExtent.height, 1170U);
    EXPECT_EQ(after->nativeBindingRevision, before->nativeBindingRevision)
        << "the layer object did not change, so the binding must not either";
    EXPECT_GT(after->surfaceRevision, before->surfaceRevision);
    EXPECT_GT(after->sourceMetricsRevision, before->sourceMetricsRevision);
    EXPECT_FALSE(after->suspended);

    // And the lease still describes the same layer, so a rebind never happened.
    auto lease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(lease.has_value());
    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*lease);
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->nativeWindow, InitialLayer);
}

// layoutSubviews fires constantly without changing the layer or its drawable size. Churning revisions
// on every layout pass would reset the swapchain far more often on iOS than the Android equivalent.
TEST(IosPlatformBackendTest, ARedundantResizeOrReacquisitionChangesNothing)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE((*backend)->pollFrame().has_value());
    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(ios.onDrawableResized(FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F}).has_value());
    ASSERT_TRUE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = InitialLayer},
                                          FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F})
                    .has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->surfaceRevision, before->surfaceRevision);
    EXPECT_EQ(after->nativeBindingRevision, before->nativeBindingRevision);
    EXPECT_EQ(after->sourceMetricsRevision, before->sourceMetricsRevision);
}

// A rotation while the scene is disconnected must not make the surface look alive: the next
// acquisition carries the final geometry.
TEST(IosPlatformBackendTest, AResizeWithNoDrawableIsASuccessfulNoOp)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE(ios.onNativeLayerReleased().has_value());
    ASSERT_TRUE((*backend)->pollFrame().has_value());
    const auto before = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(before->suspended);

    EXPECT_TRUE(ios.onDrawableResized(FramebufferExtent{2532, 1170}, ContentScale{3.0F, 3.0F}).has_value());

    const auto after = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->suspended);
    EXPECT_EQ(after->surfaceRevision, before->surfaceRevision);
    EXPECT_EQ(after->framebufferExtent.width, before->framebufferExtent.width);
}

TEST(IosPlatformBackendTest, RebindAndResizeRejectTheGeometryTheFactoryRejects)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    EXPECT_FALSE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0},
                                           FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F})
                     .has_value());
    EXPECT_FALSE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x4000},
                                           FramebufferExtent{0, 2532}, ContentScale{3.0F, 3.0F})
                     .has_value());
    EXPECT_FALSE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x4000},
                                           FramebufferExtent{1170, 2532}, ContentScale{0.0F, 3.0F})
                     .has_value());
    EXPECT_FALSE(ios.onDrawableResized(FramebufferExtent{0, 2532}, ContentScale{3.0F, 3.0F}).has_value());
    EXPECT_FALSE(ios.onDrawableResized(FramebufferExtent{1170, 2532}, ContentScale{0.0F, 3.0F}).has_value());
}

// iOS touch is a sequence of discrete contacts with no persisted cursor entity, so locking one is a
// category error -- there is nothing to lock.
TEST(IosPlatformBackendTest, AcceptsOnlyFreePointerCapture)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    EXPECT_TRUE((*backend)->setPointerCaptureMode(PointerCaptureMode::Free).has_value());
    EXPECT_FALSE((*backend)->setPointerCaptureMode(PointerCaptureMode::Locked).has_value());
}

// --- C6: soft keyboard ---

// Requests, not commands: only UIKit can make a view first responder. Reading does NOT clear, unlike
// Android's take-style API -- becomeFirstResponder can fail while a view is off-screen or
// mid-transition, and consuming at read time would lose the intent permanently.
TEST(IosPlatformBackendTest, SoftKeyboardRequestsLatchAndSurviveReading)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::None);

    ASSERT_TRUE(ios.requestShowSoftKeyboard().has_value());
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::Show);
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::Show)
        << "reading must not consume: a failed becomeFirstResponder has to be retried";

    ASSERT_TRUE(ios.acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest::Show).has_value());
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::None);
}

// A late acknowledgement of an older read must not erase a newer opposite request, or a show
// immediately followed by a hide would leave the keyboard up.
TEST(IosPlatformBackendTest, AnAcknowledgementClearsOnlyTheRequestItMatches)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE(ios.requestShowSoftKeyboard().has_value());
    ASSERT_TRUE(ios.requestHideSoftKeyboard().has_value());
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::Hide) << "the latest request wins";

    // The host applied the Show it read a moment ago; the Hide must survive that acknowledgement.
    ASSERT_TRUE(ios.acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest::Show).has_value());
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::Hide);

    ASSERT_TRUE(ios.acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest::Hide).has_value());
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::None);
}

TEST(IosPlatformBackendTest, RejectsAnAcknowledgementThatNamesNoRequest)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE(ios.requestShowSoftKeyboard().has_value());
    auto status = ios.acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest::None);
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(ios.pendingSoftKeyboardRequest(), IosSoftKeyboardRequest::Show);
}

// Reported by the host, never derived: the height depends on the keyboard type, the language, whether
// the QuickType row is showing, whether a hardware keyboard is attached, and Split View geometry.
TEST(IosPlatformBackendTest, SoftKeyboardOcclusionIsReportedInPixelsAndReadInPoints)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    EXPECT_FLOAT_EQ(ios.softKeyboardOccludedLogicalHeight(), 0.0F);

    // 900 physical pixels at 3x is 300 points.
    ASSERT_TRUE(ios.onSoftKeyboardOcclusionChanged(900).has_value());
    EXPECT_FLOAT_EQ(ios.softKeyboardOccludedLogicalHeight(), 300.0F);

    ASSERT_TRUE(ios.onSoftKeyboardOcclusionChanged(0).has_value());
    EXPECT_FLOAT_EQ(ios.softKeyboardOccludedLogicalHeight(), 0.0F);
}

TEST(IosPlatformBackendTest, RejectsAnOcclusionTallerThanTheWindow)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    EXPECT_FALSE(ios.onSoftKeyboardOcclusionChanged(2533).has_value());
    EXPECT_TRUE(ios.onSoftKeyboardOcclusionChanged(2532).has_value())
        << "exactly the window height must remain usable";
}

// A rotation changes both the window height and the keyboard height, so carrying the old occlusion
// over would report a value that matched neither geometry.
TEST(IosPlatformBackendTest, ARotationClearsTheStaleKeyboardOcclusion)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    ASSERT_TRUE(ios.onSoftKeyboardOcclusionChanged(900).has_value());
    ASSERT_FLOAT_EQ(ios.softKeyboardOccludedLogicalHeight(), 300.0F);

    ASSERT_TRUE(ios.onDrawableResized(FramebufferExtent{2532, 1170}, ContentScale{3.0F, 3.0F}).has_value());
    EXPECT_FLOAT_EQ(ios.softKeyboardOccludedLogicalHeight(), 0.0F);
}

// --- Marked (preedit) text ---

[[nodiscard]] IosCompositionEvent markedText(std::u16string_view utf16, i32 cursor = 0) noexcept
{
    IosCompositionEvent event{};
    const bool built =
        makeIosCompositionEventFromUtf16(utf16, cursor, IosCompositionAction::SetMarkedText, event);
    return built ? event : IosCompositionEvent{};
}

[[nodiscard]] IosCompositionEvent committedText(std::u16string_view utf16) noexcept
{
    IosCompositionEvent event{};
    const bool built = makeIosCompositionEventFromUtf16(utf16, 0, IosCompositionAction::Commit, event);
    return built ? event : IosCompositionEvent{};
}

[[nodiscard]] IosCompositionEvent unmarkText() noexcept
{
    IosCompositionEvent event{};
    event.action = IosCompositionAction::Unmark;
    return event;
}

TEST(IosPlatformBackendTest, TheFirstMarkedTextStartsAndLaterOnesUpdate)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"ni", 2)));
    ASSERT_TRUE(compositions->tryPush(markedText(u"nihao", 5)));

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

    IIosPlatformBackend& ios = iosFacet(**backend);
    EXPECT_EQ(ios.publishedCompositionStartCount(), 1U);
    EXPECT_EQ(ios.publishedCompositionUpdateCount(), 1U);
}

// The ordering case, and the whole reason commits share the composition ring: Ended must precede the
// text it resolved into, and two independent rings could not express that.
TEST(IosPlatformBackendTest, ACommitEndsTheCompositionBeforePublishingItsText)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"nihao", 5)));
    ASSERT_TRUE(compositions->tryPush(committedText(u"你好")));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 3U);

    ASSERT_NE(std::get_if<TextCompositionTransition>(&transitions[0].payload), nullptr);
    const auto* ended = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(ended, nullptr) << "the stage must come before the text";
    EXPECT_EQ(ended->stage, TextCompositionStage::Ended);
    EXPECT_TRUE(ended->preeditUtf8.empty());

    const auto* text = std::get_if<TextInputTransition>(&transitions[2].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, "你好");

    IIosPlatformBackend& ios = iosFacet(**backend);
    EXPECT_EQ(ios.publishedCompositionEndCount(), 1U);
    EXPECT_EQ(ios.publishedTextCommitCount(), 1U);
}

TEST(IosPlatformBackendTest, ACommitWithNoMarkedTextPublishesTextAlone)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(committedText(u"hi")));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    EXPECT_EQ(std::get_if<TextInputTransition>(&transitions[0].payload)->committedUtf8, "hi");
    EXPECT_EQ(iosFacet(**backend).publishedCompositionEndCount(), 0U);
}

// Deleting the last character of a marked region arrives as an empty setMarkedText:, which UIKit does
// not distinguish from abandoning it. Cancelled rather than Ended because nothing was produced, and
// the distinction is load-bearing: UIInputRouteProducer excludes Cancelled from flow observation.
TEST(IosPlatformBackendTest, AnEmptiedMarkedRegionCancelsRatherThanEnds)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"ni", 2)));
    ASSERT_TRUE(compositions->tryPush(markedText(u"")));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* cancelled = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->stage, TextCompositionStage::Cancelled);
    EXPECT_EQ(iosFacet(**backend).publishedCompositionCancelCount(), 1U);
    EXPECT_EQ(iosFacet(**backend).publishedCompositionEndCount(), 0U);
}

TEST(IosPlatformBackendTest, UnmarkingCancelsTheComposition)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"ni", 2)));
    ASSERT_TRUE(compositions->tryPush(unmarkText()));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 2U);
    const auto* cancelled = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->stage, TextCompositionStage::Cancelled);
}

// Both silent rows. UIKit calls unmarkText on every focus change, and announcing the end of a
// composition that never started is noise every consumer would have to filter.
TEST(IosPlatformBackendTest, ClearingOrUnmarkingWithNoCompositionPublishesNothing)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"")));
    ASSERT_TRUE(compositions->tryPush(unmarkText()));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    EXPECT_TRUE(poll->frame()->inputTransitions().empty());
    IIosPlatformBackend& ios = iosFacet(**backend);
    EXPECT_EQ(ios.publishedCompositionCancelCount(), 0U);
    EXPECT_EQ(ios.publishedCompositionStartCount(), 0U);
}

TEST(IosPlatformBackendTest, ACompositionSurvivesAcrossPolls)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"ni", 2)));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"nihao", 5)));
    auto second = (*backend)->pollFrame();
    ASSERT_TRUE(second.has_value());
    const InputTransitionBatch transitions = second->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    EXPECT_EQ(std::get_if<TextCompositionTransition>(&transitions[0].payload)->stage,
              TextCompositionStage::Updated);
}

// Losing the drawable delivers no UITextInput call at all, so without an explicit cancel the UI keeps
// drawing a preedit the input system has already forgotten. Published on the next poll because that
// path runs outside a frame, and appending with no frame begun is a failure.
TEST(IosPlatformBackendTest, LosingTheLayerCancelsAnInFlightComposition)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(compositions->tryPush(markedText(u"ni", 2)));
    ASSERT_TRUE((*backend)->pollFrame().has_value());

    ASSERT_TRUE(iosFacet(**backend).onNativeLayerReleased().has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    // Two, in this order: the window-wide input cancel that releases the fingers and keys the layer
    // took with it, then the composition cancel. The window cancel comes first because releasing
    // capture cannot be conditional on there having been a preedit.
    ASSERT_EQ(transitions.size(), 2U);
    const auto* windowCancel = std::get_if<InputCancelTransition>(&transitions[0].payload);
    ASSERT_NE(windowCancel, nullptr);
    EXPECT_FALSE(windowCancel->pointer.has_value()) << "the whole window lost its input, not one finger";
    const auto* cancelled = std::get_if<TextCompositionTransition>(&transitions[1].payload);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->stage, TextCompositionStage::Cancelled);

    // And the session is genuinely clear: the next marked text starts a new pass.
    ASSERT_TRUE(iosFacet(**backend)
                    .onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x6000},
                                           FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F})
                    .has_value());
    ASSERT_TRUE(compositions->tryPush(markedText(u"hao", 3)));
    auto next = (*backend)->pollFrame();
    ASSERT_TRUE(next.has_value());
    const InputTransitionBatch restartTransitions = next->frame()->inputTransitions();
    // The acquisition posts its own window cancel, so the new pass follows it rather than leading.
    ASSERT_EQ(restartTransitions.size(), 2U);
    ASSERT_NE(std::get_if<InputCancelTransition>(&restartTransitions[0].payload), nullptr);
    const auto* restarted = std::get_if<TextCompositionTransition>(&restartTransitions[1].payload);
    ASSERT_NE(restarted, nullptr);
    EXPECT_EQ(restarted->stage, TextCompositionStage::Started);
}

// Losing the layer with nothing in flight must publish no *composition* stage, or every
// background/foreground cycle would inject a spurious Cancelled. The window-wide input cancel is
// unconditional and stays: releasing capture cannot depend on there having been a preedit.
TEST(IosPlatformBackendTest, LosingTheLayerWithNoCompositionPublishesNoStage)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    ASSERT_TRUE(iosFacet(**backend).onNativeLayerReleased().has_value());
    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const InputTransitionBatch transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 1U);
    EXPECT_NE(std::get_if<InputCancelTransition>(&transitions[0].payload), nullptr);
    EXPECT_EQ(iosFacet(**backend).publishedCompositionCancelCount(), 0U);
}

// Astral-plane text must survive the preedit path, not just commits: iOS predictive input shows an
// emoji in the marked region first. The cursor is in codepoints, so a surrogate pair counts once.
TEST(IosPlatformBackendTest, APreeditCarriesAstralPlaneTextWithACodepointCursor)
{
    auto compositions = std::make_shared<IosCompositionEventQueue>();
    auto params = validParams();
    params.compositionEvents = compositions;
    auto backend = createIosWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value());

    // U+1F600 then 'a'. Offset 3 UTF-16 units is 2 codepoints.
    ASSERT_TRUE(compositions->tryPush(markedText(u"\U0001F600a", 3)));

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value());
    const auto* started =
        std::get_if<TextCompositionTransition>(&poll->frame()->inputTransitions()[0].payload);
    ASSERT_NE(started, nullptr);
    EXPECT_EQ(started->preeditUtf8, "\U0001F600a");
    EXPECT_EQ(started->cursorCodepoint, 2U);
}

// --- Caret placement ---

// This case exists because rejecting a placement broke the whole engine on Android, not because
// accepting one is interesting: the Runtime publishes a caret every frame a TextEdit is focused, the
// coordinator turns a rejection into a LifecycleInvariantViolation, and EngineHost latches a terminal
// outcome. The app simply stops producing frames the moment a text field takes focus.
TEST(IosPlatformBackendTest, LatchesTheCaretInPointsWithoutScaling)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);
    EXPECT_FALSE(ios.caretPoints().has_value()) << "no caret before one is published";

    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value());
    ASSERT_TRUE(metrics->has_value());

    // Points, not pixels: UITextInput geometry is in view coordinates, so this is a narrowing
    // conversion rather than a unit conversion. Multiplying by 3x here is the Android habit and would
    // put the candidate bar three times too far down the screen.
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 10.0, .y = 20.0, .width = 2.0, .height = 16.0},
                    })
                    .has_value());
    const auto caret = ios.caretPoints();
    ASSERT_TRUE(caret.has_value());
    EXPECT_EQ(*caret, (IosCaretPoints{.x = 10.0F, .y = 20.0F, .width = 2.0F, .height = 16.0F}));
}

// Non-consuming, unlike the keyboard request: UIKit asks for caret geometry whenever it repositions
// the candidate bar or the magnifier, so clearing on read would leave the second query blind.
TEST(IosPlatformBackendTest, ReadingTheCaretDoesNotClearIt)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 1.0, .y = 2.0, .width = 0.0, .height = 4.0},
                    })
                    .has_value());

    IIosPlatformBackend& ios = iosFacet(**backend);
    const auto first = ios.caretPoints();
    const auto second = ios.caretPoints();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
}

TEST(IosPlatformBackendTest, ClearingThePlacementDropsTheCaret)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 4.0},
                    })
                    .has_value());
    ASSERT_TRUE(iosFacet(**backend).caretPoints().has_value());

    EXPECT_TRUE((*backend)->updateTextInputPlacement(std::nullopt).has_value());
    EXPECT_FALSE(iosFacet(**backend).caretPoints().has_value());
}

// A rotation reflows the whole UI, so a caret measured against the old layout points at nothing. The
// UI republishes it on the next focused frame, so dropping it costs one frame of bar placement.
TEST(IosPlatformBackendTest, ARotationDropsTheStaleCaret)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    ASSERT_TRUE((*backend)
                    ->updateTextInputPlacement(TextInputPlacement{
                        .window = (*metrics)->window,
                        .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 4.0},
                    })
                    .has_value());

    IIosPlatformBackend& ios = iosFacet(**backend);
    ASSERT_TRUE(ios.caretPoints().has_value());
    ASSERT_TRUE(ios.onDrawableResized(FramebufferExtent{2532, 1170}, ContentScale{3.0F, 3.0F}).has_value());
    EXPECT_FALSE(ios.caretPoints().has_value());
}

// Invalid geometry is rejected rather than clamped: an infinite CGRect places the candidate bar at an
// undefined position instead of failing. A default-constructed placement fails on both counts --
// unset window and zero height.
TEST(IosPlatformBackendTest, RejectsCaretGeometryItCannotUse)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    auto metrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(metrics.has_value() && metrics->has_value());
    const WindowId window = (*metrics)->window;

    EXPECT_FALSE((*backend)->updateTextInputPlacement(TextInputPlacement{}).has_value());
    EXPECT_FALSE((*backend)
                     ->updateTextInputPlacement(TextInputPlacement{
                         .window = window,
                         .caret = {.x = 1.0, .y = 2.0, .width = 2.0, .height = 0.0},
                     })
                     .has_value())
        << "a zero-height caret is not a position";
    EXPECT_FALSE((*backend)
                     ->updateTextInputPlacement(TextInputPlacement{
                         .window = window,
                         .caret = {.x = std::numeric_limits<double>::infinity(),
                                   .y = 2.0,
                                   .width = 2.0,
                                   .height = 4.0},
                     })
                     .has_value());
    EXPECT_FALSE((*backend)
                     ->updateTextInputPlacement(TextInputPlacement{
                         .window = window,
                         .caret = {.x = 1e300, .y = 2.0, .width = 2.0, .height = 4.0},
                     })
                     .has_value())
        << "a double outside float range would become infinity in a CGRect";
    EXPECT_FALSE(iosFacet(**backend).caretPoints().has_value());
}

// --- Backend lifetime ---

TEST(IosPlatformBackendTest, EveryEntryPointFailsAfterShutdown)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());
    IIosPlatformBackend& ios = iosFacet(**backend);

    (*backend)->shutdown();

    EXPECT_FALSE((*backend)->pollFrame().has_value());
    EXPECT_FALSE((*backend)->initialPrimaryWindowMetrics().has_value());
    EXPECT_FALSE((*backend)->acquirePrimaryWindowSurfaceLease().has_value());
    EXPECT_FALSE((*backend)->primaryWindowSurfaceSnapshot().has_value());
    EXPECT_FALSE(ios.onNativeLayerReleased().has_value());
    EXPECT_FALSE(ios.onNativeLayerAcquired(IosNativeLayerHandle{.metalLayer = 0x7000},
                                           FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F})
                     .has_value());
    EXPECT_FALSE(ios.onDrawableResized(FramebufferExtent{1170, 2532}, ContentScale{3.0F, 3.0F}).has_value());
    EXPECT_FALSE(ios.requestShowSoftKeyboard().has_value());
}

TEST(IosPlatformBackendTest, ShutdownIsIdempotent)
{
    auto backend = createIosWindowSurfacePlatformBackend(validParams());
    ASSERT_TRUE(backend.has_value());

    (*backend)->shutdown();
    (*backend)->shutdown();
    EXPECT_FALSE((*backend)->pollFrame().has_value());
}

} // namespace
} // namespace Tina::Platform
