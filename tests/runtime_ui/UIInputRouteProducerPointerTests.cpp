#include "UIInputRouteProducerTestSupport.hpp"

#include <array>
#include <variant>

namespace Tina::Tests {
namespace {

struct PointerEventTrace final {
    std::array<UI::UIPointerInputEvent, 8> events{};
    usize size = 0;

    void push(const UI::UIPointerInputEvent& event) noexcept
    {
        if (size < events.size())
        {
            events[size] = event;
            ++size;
        }
    }
};

TEST_F(UIInputRouteProducerTest, PublishesDeduplicatedHeldPointerClaimsAndDropsNonHeldRequests)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
        }});
    ASSERT_TRUE(token);

    auto heldFrame = buildFrame(*builder, window,
                                {
                                    .frameId = {2},
                                    .transitions = {pointerMove(window, 10.0, 10.0)},
                                    .heldPointerButtons = {Platform::PointerButton::Primary},
                                    .pointerX = 10.0,
                                    .pointerY = 10.0,
                                });
    ASSERT_TRUE(heldFrame.has_value()) << (heldFrame ? "" : heldFrame.error().message);
    auto heldOutput = producer->produce(tree.context.get(), *heldFrame);
    ASSERT_TRUE(heldOutput.has_value()) << (heldOutput ? "" : heldOutput.error().message);
    ASSERT_EQ(heldOutput->claims.controls.size(), 1U);
    const auto* heldClaim = std::get_if<Platform::PointerButtonControlIdentity>(
        &heldOutput->claims.controls.front().control);
    ASSERT_NE(heldClaim, nullptr);
    EXPECT_EQ(heldClaim->window, window);
    EXPECT_EQ(heldClaim->pointer, Platform::PrimaryPointerId);
    EXPECT_EQ(heldClaim->button, Platform::PointerButton::Primary);
    EXPECT_FALSE(heldOutput->consumption.isConsumed(0));

    auto releasedFrame = buildFrame(*builder, window,
                                    {
                                        .frameId = {3},
                                        .transitions = {pointerMove(window, 10.0, 10.0)},
                                        .pointerX = 10.0,
                                        .pointerY = 10.0,
                                    });
    ASSERT_TRUE(releasedFrame.has_value()) << (releasedFrame ? "" : releasedFrame.error().message);
    auto releasedOutput = producer->produce(tree.context.get(), *releasedFrame);
    ASSERT_TRUE(releasedOutput.has_value()) << (releasedOutput ? "" : releasedOutput.error().message);
    EXPECT_TRUE(releasedOutput->claims.controls.empty());
}
TEST_F(UIInputRouteProducerTest, ClaimCapacityFailurePreservesPublishedClaimsAndConsumesAttemptWatermark)
{
    auto producer = createProducer(128, 1);
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Move, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept {
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Primary));
            EXPECT_TRUE(event.claimPointerButton(Platform::PointerButton::Secondary));
        }});
    ASSERT_TRUE(token);

    auto firstFrame = buildFrame(*builder, window,
                                 {
                                     .frameId = {4},
                                     .transitions = {pointerMove(window, 10.0, 10.0)},
                                     .heldPointerButtons = {Platform::PointerButton::Primary},
                                     .pointerX = 10.0,
                                     .pointerY = 10.0,
                                 });
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);
    auto firstOutput = producer->produce(tree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    ASSERT_EQ(firstOutput->claims.controls.size(), 1U);
    EXPECT_EQ(firstOutput->claims.platformFrame, Platform::PlatformFrameId{4});

    auto overflowingFrame = buildFrame(*builder, window,
                                       {
                                           .frameId = {5},
                                           .transitions = {pointerMove(window, 10.0, 10.0)},
                                           .heldPointerButtons = {Platform::PointerButton::Primary,
                                                                  Platform::PointerButton::Secondary},
                                           .pointerX = 10.0,
                                           .pointerY = 10.0,
                                       });
    ASSERT_TRUE(overflowingFrame.has_value()) << (overflowingFrame ? "" : overflowingFrame.error().message);
    auto failed = producer->produce(tree.context.get(), *overflowingFrame);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_EQ(firstOutput->claims.platformFrame, Platform::PlatformFrameId{4});
    ASSERT_EQ(firstOutput->claims.controls.size(), 1U);

    auto retry = producer->produce(tree.context.get(), *overflowingFrame);
    ASSERT_FALSE(retry.has_value());
    EXPECT_EQ(retry.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}
TEST_F(UIInputRouteProducerTest, MapsMixedRawOrdinalsWithHolesToFrameAndSequence)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    PointerEventTrace observed;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(token);

    FrameSpec spec{
        .frameId = {7},
        .transitions =
            {
                keyDown(window, Platform::Key::A),
                pointerMove(window, 90.0, 90.0),
                keyDown(window, Platform::Key::B),
                pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
            },
        .heldKeys = {Platform::Key::A, Platform::Key::B},
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(observed.size, 1U);
    EXPECT_EQ(observed.events[0].platformFrame, frame->id());
    EXPECT_EQ(observed.events[0].transitionOrdinal, 3U);
    EXPECT_EQ(observed.events[0].sourceSequence, frame->inputTransitions()[3].sequence);
    EXPECT_TRUE(output->consumption.isConsumed(3));
    EXPECT_FALSE(output->consumption.isConsumed(0));
    EXPECT_FALSE(output->consumption.isConsumed(1));
    EXPECT_FALSE(output->consumption.isConsumed(2));
}
TEST_F(UIInputRouteProducerTest, PreservesPointerTransitionPositions)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    PointerEventTrace observed;
    auto buttonToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    auto wheelToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Wheel, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&observed](UI::UIRoutedPointerEvent& event) noexcept {
            observed.push(event.input());
            event.consumeInputTransition();
        }});
    ASSERT_TRUE(buttonToken && wheelToken);

    FrameSpec spec{
        .frameId = {8},
        .transitions =
            {
                pointerMove(window, 10.0, 10.0, 1.0, 1.0),
                pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0),
                pointerMove(window, 30.0, 30.0, 20.0, 20.0),
                pointerWheel(window, 30.0, 30.0, 0.0, -1.0),
                pointerMove(window, 50.0, 50.0, 20.0, 20.0),
            },
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .pointerX = 50.0,
        .pointerY = 50.0,
        .accumulatedDeltaX = 41.0,
        .accumulatedDeltaY = 41.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);

    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    ASSERT_EQ(observed.size, 2U);
    EXPECT_EQ(observed.events[0].kind, UI::UIRoutedPointerEventKind::ButtonDown);
    EXPECT_FLOAT_EQ(observed.events[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(observed.events[0].position.y, 10.0F);
    EXPECT_EQ(observed.events[1].kind, UI::UIRoutedPointerEventKind::Wheel);
    EXPECT_FLOAT_EQ(observed.events[1].position.x, 30.0F);
    EXPECT_FLOAT_EQ(observed.events[1].position.y, 30.0F);
    EXPECT_TRUE(output->consumption.isConsumed(1));
    EXPECT_TRUE(output->consumption.isConsumed(3));
}
TEST_F(UIInputRouteProducerTest, ConsumedBitsCoverOrdinalsSixtyThreeAndSixtyFourAndClearNextFrame)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);

    auto consumeButton = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    auto consumeWheel = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::Wheel, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[](UI::UIRoutedPointerEvent& event) noexcept { event.consumeInputTransition(); }});
    ASSERT_TRUE(consumeButton && consumeWheel);

    FrameSpec first{.frameId = {63}};
    for (usize index = 0; index < 63; ++index)
    {
        first.transitions.push_back(keyDown(window, Platform::Key::A));
    }
    first.transitions.push_back(pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0));
    first.transitions.push_back(pointerWheel(window, 10.0, 10.0, 0.0, 1.0));
    first.heldKeys = {Platform::Key::A};
    first.heldPointerButtons = {Platform::PointerButton::Primary};
    first.pointerX = 10.0;
    first.pointerY = 10.0;
    auto firstFrame = buildFrame(*builder, window, first);
    ASSERT_TRUE(firstFrame.has_value()) << (firstFrame ? "" : firstFrame.error().message);

    auto firstOutput = producer->produce(tree.context.get(), *firstFrame);
    ASSERT_TRUE(firstOutput.has_value()) << (firstOutput ? "" : firstOutput.error().message);
    EXPECT_TRUE(firstOutput->consumption.isConsumed(63));
    EXPECT_TRUE(firstOutput->consumption.isConsumed(64));
    ASSERT_EQ(firstOutput->consumption.consumedOrdinalWords.size(), 2U);

    FrameSpec second{
        .frameId = {64},
        .transitions = {pointerMove(window, 10.0, 10.0)},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto secondFrame = buildFrame(*builder, window, second);
    ASSERT_TRUE(secondFrame.has_value()) << (secondFrame ? "" : secondFrame.error().message);
    auto secondOutput = producer->produce(nullptr, *secondFrame);
    ASSERT_TRUE(secondOutput.has_value()) << (secondOutput ? "" : secondOutput.error().message);
    EXPECT_FALSE(secondOutput->consumption.isConsumed(0));
    EXPECT_TRUE(secondOutput->consumption.consumedOrdinalWords.empty());
}
TEST_F(UIInputRouteProducerTest, NoHitDoesNotConsumeAndStoppedButtonDefaultStillConsumes)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto token = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonDown, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent& event) noexcept {
            ++callbackCount;
            event.stopPropagation();
        }});
    ASSERT_TRUE(token);

    auto noHitFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {9},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 90.0, 90.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 90.0,
                       .pointerY = 90.0,
                   });
    ASSERT_TRUE(noHitFrame.has_value()) << (noHitFrame ? "" : noHitFrame.error().message);
    auto noHitOutput = producer->produce(tree.context.get(), *noHitFrame);
    ASSERT_TRUE(noHitOutput.has_value()) << (noHitOutput ? "" : noHitOutput.error().message);
    EXPECT_EQ(callbackCount, 0U);
    EXPECT_FALSE(noHitOutput->consumption.isConsumed(0));

    auto stoppedFrame =
        buildFrame(*builder, window,
                   {
                       .frameId = {10},
                       .transitions = {pointerButton(window, Platform::DigitalTransition::Down, 10.0, 10.0)},
                       .heldPointerButtons = {Platform::PointerButton::Primary},
                       .pointerX = 10.0,
                       .pointerY = 10.0,
                   });
    ASSERT_TRUE(stoppedFrame.has_value()) << (stoppedFrame ? "" : stoppedFrame.error().message);
    auto stoppedOutput = producer->produce(tree.context.get(), *stoppedFrame);
    ASSERT_TRUE(stoppedOutput.has_value()) << (stoppedOutput ? "" : stoppedOutput.error().message);
    EXPECT_EQ(callbackCount, 1U);
    EXPECT_TRUE(stoppedOutput->consumption.isConsumed(0));
}
TEST_F(UIInputRouteProducerTest, ResetCancelAndNonPointerTransitionsDoNotRouteOrFabricateUp)
{
    auto producer = createProducer();
    RouteTree tree = createRouteTree(window);
    ASSERT_NE(producer, nullptr);
    ASSERT_NE(tree.context, nullptr);
    usize callbackCount = 0;
    auto upToken = addListener(
        *tree.context,
        {.node = tree.target, .kind = UI::UIRoutedPointerEventKind::ButtonUp, .phases = UI::UIEventPhaseMask::Target},
        UI::UIRoutedPointerCallback{[&callbackCount](UI::UIRoutedPointerEvent&) noexcept { ++callbackCount; }});
    ASSERT_TRUE(upToken);

    FrameSpec spec{
        .frameId = {12},
        .transitions =
            {
                keyDown(window, Platform::Key::A),
                Platform::InputCancelTransition{
                    .routedWindow = window,
                    .reason = Platform::InputCancelReason::FocusLost,
                },
                Platform::InputStreamReset{
                    .routedWindow = window,
                    .reason = Platform::InputResetReason::BackendRecovery,
                },
            },
        .heldKeys = {Platform::Key::A},
        .pointerX = 10.0,
        .pointerY = 10.0,
    };
    auto frame = buildFrame(*builder, window, spec);
    ASSERT_TRUE(frame.has_value()) << (frame ? "" : frame.error().message);
    auto output = producer->produce(tree.context.get(), *frame);
    ASSERT_TRUE(output.has_value()) << (output ? "" : output.error().message);
    EXPECT_EQ(callbackCount, 0U);
    for (usize ordinal = 0; ordinal < frame->inputTransitions().size(); ++ordinal)
    {
        EXPECT_FALSE(output->consumption.isConsumed(ordinal));
    }
}

} // namespace
} // namespace Tina::Tests
