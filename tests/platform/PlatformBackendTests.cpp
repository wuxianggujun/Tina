#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace Tina::Tests {
namespace {

using TestWindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using TestGamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;

[[nodiscard]] Platform::WindowId createWindowId(TestWindowPool& pool)
{
    auto id = pool.tryEmplace(1);
    EXPECT_TRUE(id.has_value());
    return id.has_value() ? *id : Platform::WindowId{};
}

[[nodiscard]] Platform::GamepadId createGamepadId(TestGamepadPool& pool)
{
    auto id = pool.tryEmplace(1);
    EXPECT_TRUE(id.has_value());
    return id.has_value() ? *id : Platform::GamepadId{};
}

[[nodiscard]] Platform::GamepadId createGamepadIdAtIndex(TestGamepadPool& pool, u32 targetIndex)
{
    Platform::GamepadId id{};
    for (u32 index = 0; index <= targetIndex; ++index)
    {
        id = createGamepadId(pool);
    }
    EXPECT_EQ(id.index(), targetIndex);
    return id;
}

[[nodiscard]] Platform::WindowMetricsSnapshot validWindowMetrics(Platform::WindowId window, u64 revision = 1) noexcept
{
    return Platform::WindowMetricsSnapshot{
        .window = window,
        .logicalExtent = {800, 600},
        .framebufferExtent = {1600, 1200},
        .contentScale = {2.0F, 2.0F},
        .revision = revision,
        .focused = true,
        .visible = true,
    };
}

[[nodiscard]] Platform::WindowInputSnapshot validWindowInput(Platform::WindowId window, u64 revision = 1) noexcept
{
    return Platform::WindowInputSnapshot{
        .window = window,
        .sourceMetricsRevision = revision,
    };
}

[[nodiscard]] Platform::GamepadSnapshot validGamepadSnapshot(Platform::GamepadId gamepad, u64 revision = 1) noexcept
{
    return Platform::GamepadSnapshot{
        .gamepad = gamepad,
        .revision = revision,
    };
}

TEST(HeadlessPlatformBackendTest, ReturnsOnlyContinueFramesWithMonotonicIdentity)
{
    const Platform::PlatformBackendCreateParams params{
        .primaryWindow = {.title = "Tina Runtime Tests"},
    };
    auto backendResult = Platform::createHeadlessPlatformBackend(params);
    ASSERT_TRUE(backendResult.has_value());
    ASSERT_NE(*backendResult, nullptr);

    auto firstPoll = (*backendResult)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value());
    ASSERT_TRUE(firstPoll->isContinueFrame());
    ASSERT_FALSE(firstPoll->isExitRequested());
    ASSERT_NE(firstPoll->frame(), nullptr);
    EXPECT_EQ(firstPoll->frame()->id(), Platform::PlatformFrameId{1});
    EXPECT_TRUE(firstPoll->frame()->windows().empty());
    EXPECT_TRUE(firstPoll->frame()->inputTransitions().empty());
    EXPECT_TRUE(firstPoll->frame()->platformEvents().empty());

    auto secondPoll = (*backendResult)->pollFrame();
    ASSERT_TRUE(secondPoll.has_value());
    ASSERT_NE(secondPoll->frame(), nullptr);
    EXPECT_EQ(secondPoll->frame()->id(), Platform::PlatformFrameId{2});
}

TEST(HeadlessPlatformBackendTest, RejectsInvalidCapacityAndPollAfterShutdown)
{
    const Platform::PlatformBackendCreateParams invalidParams{
        .frameCapacities = {.inputTransitionCapacity = 0},
    };
    auto invalidBackend = Platform::createHeadlessPlatformBackend(invalidParams);
    ASSERT_FALSE(invalidBackend.has_value());
    EXPECT_EQ(invalidBackend.error().code, Platform::PlatformErrorCode::InvalidFrameCapacity);

    auto backendResult = Platform::createHeadlessPlatformBackend({});
    ASSERT_TRUE(backendResult.has_value());
    (*backendResult)->shutdown();
    (*backendResult)->shutdown();

    auto stoppedPoll = (*backendResult)->pollFrame();
    ASSERT_FALSE(stoppedPoll.has_value());
    EXPECT_EQ(stoppedPoll.error().code, Platform::PlatformErrorCode::BackendStopped);
}

TEST(PlatformPollResultTest, ExitRequestedHasNoFrameView)
{
    const auto result = Platform::PlatformPollResult::Exit();
    EXPECT_TRUE(result.isExitRequested());
    EXPECT_FALSE(result.isContinueFrame());
    EXPECT_EQ(result.frame(), nullptr);
    EXPECT_TRUE(std::holds_alternative<Platform::PlatformPollResult::ExitRequested>(result.outcome()));
}

TEST(PlatformFrameBuilderTest, ValidatesCapacityAndSnapshotRevision)
{
    auto emptyInput =
        Platform::PlatformFrameBuilder::Create({.inputTransitionCapacity = 0, .platformEventCapacity = 1});
    ASSERT_FALSE(emptyInput.has_value());
    EXPECT_EQ(emptyInput.error().code, Platform::PlatformErrorCode::InvalidFrameCapacity);

    auto excessiveEvents = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
        .platformEventCapacity = Platform::PlatformFrameCapacityConfig::MaximumPlatformEventCapacity + 1,
    });
    ASSERT_FALSE(excessiveEvents.has_value());
    EXPECT_EQ(excessiveEvents.error().code, Platform::PlatformErrorCode::InvalidFrameCapacity);

    auto emptyText = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
        .inputTextByteCapacity = 0,
        .platformEventCapacity = 1,
    });
    ASSERT_FALSE(emptyText.has_value());
    EXPECT_EQ(emptyText.error().code, Platform::PlatformErrorCode::InvalidFrameCapacity);

    auto excessiveText = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
        .inputTextByteCapacity = Platform::PlatformFrameCapacityConfig::MaximumInputTextByteCapacity + 1,
        .platformEventCapacity = 1,
    });
    ASSERT_FALSE(excessiveText.has_value());
    EXPECT_EQ(excessiveText.error().code, Platform::PlatformErrorCode::InvalidFrameCapacity);

    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_FALSE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 4),
                                                  {.window = window, .sourceMetricsRevision = 3}));
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 4), validWindowInput(window, 4)));

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_NE(frame->primaryWindow(), nullptr);
    EXPECT_EQ(frame->primaryWindow()->metrics.logicalExtent.width, 800U);
    EXPECT_EQ(frame->primaryWindow()->input.sourceMetricsRevision, 4U);
    EXPECT_FALSE(builder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, PreservesEdgesAndOnlyCoalescesAdjacentPointerMoves)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 8,
        .platformEventCapacity = 4,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());

    EXPECT_EQ(builder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::Space,
                  .state = Platform::DigitalTransition::Down,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::Space,
                  .state = Platform::DigitalTransition::Up,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::PointerMoveTransition{
                  .window = window,
                  .logicalX = 10.0,
                  .logicalY = 20.0,
                  .deltaX = 1.0,
                  .deltaY = 2.0,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::PointerMoveTransition{
                  .window = window,
                  .logicalX = 13.0,
                  .logicalY = 24.0,
                  .deltaX = 3.0,
                  .deltaY = 4.0,
              }),
              Platform::FrameBatchAppendResult::Coalesced);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::WindowMetricsChangedEvent{window, 1}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::PointerMoveTransition{
                  .window = window,
                  .logicalX = 15.0,
                  .logicalY = 25.0,
                  .deltaX = 2.0,
                  .deltaY = 1.0,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    const auto transitions = frame->inputTransitions();
    ASSERT_EQ(transitions.size(), 4U);
    EXPECT_LT(transitions[0].sequence, transitions[1].sequence);
    EXPECT_LT(transitions[1].sequence, transitions[2].sequence);
    EXPECT_LT(transitions[2].sequence, transitions[3].sequence);

    const auto* mergedMove = std::get_if<Platform::PointerMoveTransition>(&transitions[2].payload);
    ASSERT_NE(mergedMove, nullptr);
    EXPECT_DOUBLE_EQ(mergedMove->logicalX, 13.0);
    EXPECT_DOUBLE_EQ(mergedMove->logicalY, 24.0);
    EXPECT_DOUBLE_EQ(mergedMove->deltaX, 4.0);
    EXPECT_DOUBLE_EQ(mergedMove->deltaY, 6.0);
}

TEST(PlatformFrameBuilderTest, RejectsPointerMoveCoalesceDeltaOverflow)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 4,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::PointerMoveTransition{
                  .window = window,
                  .logicalX = 1.0,
                  .logicalY = 1.0,
                  .deltaX = (std::numeric_limits<double>::max)(),
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::PointerMoveTransition{
                  .window = window,
                  .logicalX = 2.0,
                  .logicalY = 2.0,
                  .deltaX = (std::numeric_limits<double>::max)(),
              }),
              Platform::FrameBatchAppendResult::InvalidPayload);

    auto frame = builder.finishFrame();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, ReservesResetSlotsAndKeepsFinalSnapshotWritable)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);
    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 2,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());

    EXPECT_EQ(builder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::A,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::A,
                  .state = Platform::DigitalTransition::Up,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::PointerWheelTransition{.window = window}),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_EQ(builder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::B,
              }),
              Platform::FrameBatchAppendResult::IgnoredAfterReset);

    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadConnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadConnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::IgnoredAfterReset);
    std::vector gamepadSnapshots{validGamepadSnapshot(gamepad, 1)};
    ASSERT_TRUE(builder.setGamepadSnapshots(gamepadSnapshots));

    Platform::WindowInputSnapshot finalInput{validWindowInput(window, 9)};
    finalInput.heldKeys.set(static_cast<usize>(Platform::Key::A));
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 9), finalInput));

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->inputTransitions().size(), 3U);
    EXPECT_TRUE(std::holds_alternative<Platform::InputStreamReset>(frame->inputTransitions().back().payload));
    ASSERT_EQ(frame->platformEvents().size(), 2U);
    EXPECT_TRUE(std::holds_alternative<Platform::PlatformEventStreamReset>(frame->platformEvents().back().payload));
    EXPECT_EQ(frame->diagnostics().inputOverflowCount, 1U);
    EXPECT_EQ(frame->diagnostics().platformEventOverflowCount, 1U);
    ASSERT_NE(frame->primaryWindow(), nullptr);
    EXPECT_TRUE(frame->primaryWindow()->input.isHeld(Platform::Key::A));
}

TEST(PlatformFrameBuilderTest, CopiesTextInputAndCompositionIntoOwnedStorage)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 4,
        .inputTextByteCapacity = 64,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());

    std::string committed = "Tina";
    const std::string committedExpected = committed;
    std::string preedit = "\xE6\xB5\x8B\xE8\xAF\x95";
    const std::string preeditExpected = preedit;

    EXPECT_EQ(builder.appendInputTransition(Platform::TextInputTransition{
                  .window = window,
                  .committedUtf8 = committed,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::TextCompositionTransition{
                  .window = window,
                  .preeditUtf8 = preedit,
                  .cursorCodepoint = 2,
                  .stage = Platform::TextCompositionStage::Updated,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));

    committed.assign("XXXX");
    preedit.assign("YYYYYY");

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->inputTransitions().size(), 2U);

    const auto* text = std::get_if<Platform::TextInputTransition>(&frame->inputTransitions()[0].payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->committedUtf8, committedExpected);
    EXPECT_NE(text->committedUtf8.data(), committed.data());

    const auto* composition = std::get_if<Platform::TextCompositionTransition>(&frame->inputTransitions()[1].payload);
    ASSERT_NE(composition, nullptr);
    EXPECT_EQ(composition->preeditUtf8, preeditExpected);
    EXPECT_NE(composition->preeditUtf8.data(), preedit.data());
}

TEST(PlatformFrameBuilderTest, TextByteOverflowWritesInputResetWithoutTruncation)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 4,
        .inputTextByteCapacity = 4,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());

    EXPECT_EQ(builder.appendInputTransition(Platform::TextInputTransition{
                  .window = window,
                  .committedUtf8 = "abcde",
              }),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_EQ(builder.appendInputTransition(Platform::TextInputTransition{
                  .window = window,
                  .committedUtf8 = "x",
              }),
              Platform::FrameBatchAppendResult::IgnoredAfterReset);

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->inputTransitions().size(), 1U);
    const auto* reset = std::get_if<Platform::InputStreamReset>(&frame->inputTransitions().front().payload);
    ASSERT_NE(reset, nullptr);
    EXPECT_EQ(reset->reason, Platform::InputResetReason::TextByteCapacityExceeded);
    EXPECT_EQ(frame->diagnostics().inputTextOverflowCount, 1U);
    EXPECT_EQ(frame->diagnostics().inputOverflowCount, 0U);
}

TEST(PlatformFrameBuilderTest, InvalidUtf8TextPayloadFailsFrame)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 4,
        .inputTextByteCapacity = 64,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());

    const std::string overlongNul{
        static_cast<char>(0xC0U),
        static_cast<char>(0x80U),
    };
    EXPECT_EQ(builder.appendInputTransition(Platform::TextInputTransition{
                  .window = window,
                  .committedUtf8 = overlongNul,
              }),
              Platform::FrameBatchAppendResult::InvalidPayload);

    auto failedFrame = builder.finishFrame();
    ASSERT_FALSE(failedFrame.has_value());
    EXPECT_EQ(failedFrame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);

    ASSERT_TRUE(builder.beginFrame({2}).has_value());
    const std::string embeddedNul{"a\0b", 3};
    EXPECT_EQ(builder.appendInputTransition(Platform::TextCompositionTransition{
                  .window = window,
                  .preeditUtf8 = embeddedNul,
              }),
              Platform::FrameBatchAppendResult::InvalidPayload);
    failedFrame = builder.finishFrame();
    ASSERT_FALSE(failedFrame.has_value());
    EXPECT_EQ(failedFrame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, RejectsInvalidInputTransitionPayloads)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);
    auto overflowGamepadPoolResult = TestGamepadPool::Create(Platform::PlatformFrameBuilder::MaximumGamepadSlots + 1U);
    ASSERT_TRUE(overflowGamepadPoolResult.has_value());
    auto& overflowGamepadPool = *overflowGamepadPoolResult;
    const Platform::GamepadId gamepadOutsideRoutingSlots = createGamepadIdAtIndex(
        overflowGamepadPool, static_cast<u32>(Platform::PlatformFrameBuilder::MaximumGamepadSlots));

    auto expectInvalidInputPayload = [](Platform::InputTransitionPayload payload) {
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        auto& builder = *builderResult;
        ASSERT_TRUE(builder.beginFrame({1}).has_value());
        EXPECT_EQ(builder.appendInputTransition(std::move(payload)), Platform::FrameBatchAppendResult::InvalidPayload);
        auto frame = builder.finishFrame();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
    };

    expectInvalidInputPayload(Platform::KeyTransition{
        .window = window,
        .key = Platform::Key::Unknown,
    });
    expectInvalidInputPayload(Platform::KeyTransition{
        .window = window,
        .key = Platform::Key::A,
        .state = static_cast<Platform::DigitalTransition>(255),
    });
    expectInvalidInputPayload(Platform::KeyTransition{
        .window = window,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Up,
        .repeat = true,
    });
    expectInvalidInputPayload(Platform::PointerButtonTransition{
        .window = window,
        .button = static_cast<Platform::PointerButton>(Platform::PointerButtonCount),
    });
    expectInvalidInputPayload(Platform::PointerButtonTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId + 1,
    });
    expectInvalidInputPayload(Platform::PointerMoveTransition{
        .window = window,
        .logicalX = std::numeric_limits<double>::quiet_NaN(),
    });
    expectInvalidInputPayload(Platform::PointerMoveTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId + 1,
    });
    expectInvalidInputPayload(Platform::PointerWheelTransition{
        .window = window,
        .deltaY = std::numeric_limits<double>::infinity(),
    });
    expectInvalidInputPayload(Platform::PointerWheelTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId + 1,
    });
    expectInvalidInputPayload(Platform::GamepadButtonTransition{
        .routedWindow = window,
        .button = Platform::GamepadButton::South,
    });
    expectInvalidInputPayload(Platform::GamepadButtonTransition{
        .routedWindow = window,
        .gamepad = gamepadOutsideRoutingSlots,
        .button = Platform::GamepadButton::South,
    });
    expectInvalidInputPayload(Platform::GamepadAxisTransition{
        .routedWindow = window,
        .gamepad = gamepad,
        .axis = static_cast<Platform::GamepadAxis>(Platform::GamepadAxisCount),
    });
    expectInvalidInputPayload(Platform::GamepadAxisTransition{
        .routedWindow = window,
        .gamepad = gamepad,
        .axis = Platform::GamepadAxis::LeftX,
        .value = std::numeric_limits<float>::quiet_NaN(),
    });
    expectInvalidInputPayload(Platform::TextInputTransition{
        .committedUtf8 = "abc",
    });
    expectInvalidInputPayload(Platform::TextCompositionTransition{
        .window = window,
        .preeditUtf8 = "abc",
        .stage = static_cast<Platform::TextCompositionStage>(255),
    });
    expectInvalidInputPayload(Platform::TextCompositionTransition{
        .window = window,
        .preeditUtf8 = "abc",
        .cursorCodepoint = 4,
        .stage = Platform::TextCompositionStage::Updated,
    });
    expectInvalidInputPayload(Platform::InputCancelTransition{
        .reason = Platform::InputCancelReason::FocusLost,
    });
    expectInvalidInputPayload(Platform::InputCancelTransition{
        .routedWindow = window,
        .reason = static_cast<Platform::InputCancelReason>(255),
    });
    expectInvalidInputPayload(Platform::InputCancelTransition{
        .routedWindow = window,
        .reason = Platform::InputCancelReason::DeviceDisconnected,
    });
    expectInvalidInputPayload(Platform::InputCancelTransition{
        .routedWindow = window,
        .reason = Platform::InputCancelReason::DeviceDisconnected,
        .gamepad = std::optional<Platform::GamepadId>{Platform::GamepadId{}},
    });
    expectInvalidInputPayload(Platform::InputCancelTransition{
        .routedWindow = window,
        .reason = Platform::InputCancelReason::FocusLost,
        .gamepad = gamepad,
    });
    expectInvalidInputPayload(Platform::InputCancelTransition{
        .routedWindow = window,
        .reason = Platform::InputCancelReason::WindowClosing,
        .gamepad = gamepad,
    });
    expectInvalidInputPayload(Platform::InputStreamReset{
        .routedWindow = std::optional<Platform::WindowId>{Platform::WindowId{}},
        .reason = Platform::InputResetReason::CapacityExceeded,
    });
    expectInvalidInputPayload(Platform::InputStreamReset{
        .reason = static_cast<Platform::InputResetReason>(255),
    });
}

TEST(PlatformFrameBuilderTest, RejectsInvalidPlatformEventPayloads)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);
    auto overflowGamepadPoolResult = TestGamepadPool::Create(Platform::PlatformFrameBuilder::MaximumGamepadSlots + 1U);
    ASSERT_TRUE(overflowGamepadPoolResult.has_value());
    auto& overflowGamepadPool = *overflowGamepadPoolResult;
    const Platform::GamepadId gamepadOutsideRoutingSlots = createGamepadIdAtIndex(
        overflowGamepadPool, static_cast<u32>(Platform::PlatformFrameBuilder::MaximumGamepadSlots));

    auto expectInvalidPlatformEvent = [](Platform::PlatformEventPayload payload) {
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        auto& builder = *builderResult;
        ASSERT_TRUE(builder.beginFrame({1}).has_value());
        EXPECT_EQ(builder.appendPlatformEvent(std::move(payload)), Platform::FrameBatchAppendResult::InvalidPayload);
        auto frame = builder.finishFrame();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidPlatformEventPayload);
    };

    expectInvalidPlatformEvent(Platform::WindowMetricsChangedEvent{});
    expectInvalidPlatformEvent(Platform::WindowMetricsChangedEvent{window, 0});
    expectInvalidPlatformEvent(Platform::GamepadConnectedEvent{});
    expectInvalidPlatformEvent(Platform::GamepadDisconnectedEvent{});
    expectInvalidPlatformEvent(Platform::GamepadConnectedEvent{gamepadOutsideRoutingSlots});
    expectInvalidPlatformEvent(Platform::GamepadDisconnectedEvent{gamepadOutsideRoutingSlots});
    expectInvalidPlatformEvent(Platform::PlatformEventStreamReset{
        .reason = static_cast<Platform::PlatformEventResetReason>(255),
    });
}

TEST(PlatformFrameBuilderTest, RejectsInputOutsideFinalPrimaryWindow)
{
    auto firstWindowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(firstWindowPoolResult.has_value());
    auto& firstWindowPool = *firstWindowPoolResult;
    const Platform::WindowId primaryWindow = createWindowId(firstWindowPool);

    auto secondWindowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(secondWindowPoolResult.has_value());
    auto& secondWindowPool = *secondWindowPoolResult;
    const Platform::WindowId otherWindow = createWindowId(secondWindowPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::KeyTransition{
                  .window = otherWindow,
                  .key = Platform::Key::A,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(
        builder.setPrimaryWindowSnapshot(validWindowMetrics(primaryWindow, 1), validWindowInput(primaryWindow, 1)));
    auto frame = builder.finishFrame();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, RejectsPlatformEventsInconsistentWithFinalSnapshots)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto expectInvalidRelationship = [](Platform::PlatformEventPayload event, Platform::WindowId finalWindow,
                                        u64 finalRevision) {
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        auto& builder = *builderResult;
        ASSERT_TRUE(builder.beginFrame({1}).has_value());
        EXPECT_EQ(builder.appendPlatformEvent(std::move(event)), Platform::FrameBatchAppendResult::Appended);
        if (finalWindow.hasValue())
        {
            EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(finalWindow, finalRevision),
                                                         validWindowInput(finalWindow, finalRevision)));
        }
        auto frame = builder.finishFrame();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidPlatformEventPayload);
    };

    expectInvalidRelationship(Platform::WindowMetricsChangedEvent{window, 1}, Platform::WindowId{}, 0);
    expectInvalidRelationship(Platform::WindowMetricsChangedEvent{window, 1}, window, 2);
    expectInvalidRelationship(Platform::GamepadConnectedEvent{gamepad}, window, 1);
    expectInvalidRelationship(Platform::GamepadDisconnectedEvent{gamepad}, window, 1);

    auto validDisconnectBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(validDisconnectBuilderResult.has_value());
    auto& validDisconnectBuilder = *validDisconnectBuilderResult;
    ASSERT_TRUE(validDisconnectBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(validDisconnectBuilder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(validDisconnectBuilder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(
        validDisconnectBuilder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(validDisconnectBuilder.finishFrame().has_value());

    auto validConnectBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(validConnectBuilderResult.has_value());
    auto& validConnectBuilder = *validConnectBuilderResult;
    ASSERT_TRUE(validConnectBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(validConnectBuilder.appendPlatformEvent(Platform::GamepadConnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    std::vector finalGamepads{validGamepadSnapshot(gamepad, 1)};
    ASSERT_TRUE(validConnectBuilder.setGamepadSnapshots(finalGamepads));
    EXPECT_TRUE(validConnectBuilder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, AllowsEventResetRecoveryToResyncFromFinalSnapshots)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 4,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendPlatformEvent(Platform::WindowMetricsChangedEvent{window, 1}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadConnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 2), validWindowInput(window, 2)));

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->platformEvents().size(), 2U);
    EXPECT_TRUE(std::holds_alternative<Platform::PlatformEventStreamReset>(frame->platformEvents().back().payload));
    ASSERT_NE(frame->primaryWindow(), nullptr);
    EXPECT_EQ(frame->primaryWindow()->metrics.revision, 2U);
}

TEST(PlatformFrameBuilderTest, EventResetDoesNotReplaceRawDisconnectCancellation)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 4,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::WindowMetricsChangedEvent{window, 1}),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));

    auto frame = builder.finishFrame();

    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidPlatformEventPayload);
}

TEST(PlatformFrameBuilderTest, AllowsRawResetToProveLaterGamepadDisconnect)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::InputStreamReset{
                  .reason = Platform::InputResetReason::BackendRecovery,
              }),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(builder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, AllowsConnectThenCancelThenDisconnectInSamePoll)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadConnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(builder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, RejectsGamepadInputWithoutLifecycleMembership)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::GamepadButtonTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .button = Platform::GamepadButton::South,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    auto frame = builder.finishFrame();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, AllowsGamepadInputClosedByLaterDisconnectOrRawReset)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto disconnectBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(disconnectBuilderResult.has_value());
    auto& disconnectBuilder = *disconnectBuilderResult;
    ASSERT_TRUE(disconnectBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(disconnectBuilder.appendInputTransition(Platform::GamepadButtonTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .button = Platform::GamepadButton::South,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(disconnectBuilder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(disconnectBuilder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(disconnectBuilder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(disconnectBuilder.finishFrame().has_value());

    auto resetBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(resetBuilderResult.has_value());
    auto& resetBuilder = *resetBuilderResult;
    ASSERT_TRUE(resetBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(resetBuilder.appendInputTransition(Platform::GamepadAxisTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .axis = Platform::GamepadAxis::LeftX,
                  .value = 0.5F,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(resetBuilder.appendInputTransition(Platform::InputStreamReset{
                  .reason = Platform::InputResetReason::BackendRecovery,
              }),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_TRUE(resetBuilder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(resetBuilder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, RejectsGamepadInputAfterMatchingDisconnect)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::GamepadButtonTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .button = Platform::GamepadButton::South,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    auto frame = builder.finishFrame();
    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, RejectsDeviceDisconnectCancelForStillConnectedGamepad)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    const std::vector finalGamepads{validGamepadSnapshot(gamepad, 1)};
    ASSERT_TRUE(builder.setGamepadSnapshots(finalGamepads));

    auto frame = builder.finishFrame();

    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, RejectsDeviceDisconnectCancelWithoutLifecycleProof)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));

    auto frame = builder.finishFrame();

    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, AllowsLifecycleResetToProveDeviceDisconnectCancel)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::PlatformEventStreamReset{
                  .reason = Platform::PlatformEventResetReason::BackendRecovery,
              }),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));

    EXPECT_TRUE(builder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, RejectsGamepadInputAfterDisconnectCancelBeforeEvent)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::DeviceDisconnected,
                  .gamepad = gamepad,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendInputTransition(Platform::GamepadButtonTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .button = Platform::GamepadButton::South,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::GamepadDisconnectedEvent{gamepad}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));

    auto frame = builder.finishFrame();

    ASSERT_FALSE(frame.has_value());
    EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
}

TEST(PlatformFrameBuilderTest, RejectsDigitalEdgesInconsistentWithFinalSnapshots)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    const auto expectInvalid = [window](Platform::InputTransitionPayload transition, bool finalHeld,
                                        std::vector<Platform::GamepadSnapshot> gamepads = {},
                                        bool appendPlatformReset = false) {
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        auto& builder = *builderResult;
        ASSERT_TRUE(builder.beginFrame({1}).has_value());
        EXPECT_EQ(builder.appendInputTransition(transition), Platform::FrameBatchAppendResult::Appended);
        if (appendPlatformReset)
        {
            EXPECT_EQ(builder.appendPlatformEvent(Platform::PlatformEventStreamReset{
                          .reason = Platform::PlatformEventResetReason::BackendRecovery,
                      }),
                      Platform::FrameBatchAppendResult::ResetInserted);
        }
        Platform::WindowInputSnapshot finalInput = validWindowInput(window, 1);
        if (const auto* key = std::get_if<Platform::KeyTransition>(&transition); key != nullptr)
        {
            finalInput.heldKeys.set(static_cast<usize>(key->key), finalHeld);
        } else if (const auto* pointer = std::get_if<Platform::PointerButtonTransition>(&transition);
                   pointer != nullptr)
        {
            finalInput.pointer.heldButtons.set(static_cast<usize>(pointer->button), finalHeld);
        } else if (const auto* gamepadTransition = std::get_if<Platform::GamepadButtonTransition>(&transition);
                   gamepadTransition != nullptr)
        {
            const auto snapshot =
                std::ranges::find(gamepads, gamepadTransition->gamepad, &Platform::GamepadSnapshot::gamepad);
            ASSERT_NE(snapshot, gamepads.end());
            snapshot->heldButtons.set(static_cast<usize>(gamepadTransition->button), finalHeld);
        }
        EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), finalInput));
        ASSERT_TRUE(builder.setGamepadSnapshots(gamepads));
        auto frame = builder.finishFrame();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidInputPayload);
    };

    expectInvalid(
        Platform::KeyTransition{
            .window = window,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
        false);
    expectInvalid(
        Platform::KeyTransition{
            .window = window,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Up,
        },
        true);
    expectInvalid(
        Platform::PointerButtonTransition{
            .window = window,
            .pointer = Platform::PrimaryPointerId,
            .button = Platform::PointerButton::Primary,
            .state = Platform::DigitalTransition::Down,
        },
        false);
    expectInvalid(
        Platform::PointerButtonTransition{
            .window = window,
            .pointer = Platform::PrimaryPointerId,
            .button = Platform::PointerButton::Primary,
            .state = Platform::DigitalTransition::Up,
        },
        true);
    const std::vector gamepads{validGamepadSnapshot(gamepad, 1)};
    expectInvalid(
        Platform::GamepadButtonTransition{
            .routedWindow = window,
            .gamepad = gamepad,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Down,
        },
        false, gamepads);
    expectInvalid(
        Platform::GamepadButtonTransition{
            .routedWindow = window,
            .gamepad = gamepad,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Up,
        },
        true, gamepads);
    expectInvalid(
        Platform::KeyTransition{
            .window = window,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
        false, {}, true);
}

TEST(PlatformFrameBuilderTest, AcceptsFinalDigitalStateAfterLaterEdgeOrReset)
{
    auto windowPoolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(windowPoolResult.has_value());
    auto& windowPool = *windowPoolResult;
    const Platform::WindowId window = createWindowId(windowPool);

    auto gamepadPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(gamepadPoolResult.has_value());
    auto& gamepadPool = *gamepadPoolResult;
    const Platform::GamepadId gamepad = createGamepadId(gamepadPool);

    auto edgeBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(edgeBuilderResult.has_value());
    auto& edgeBuilder = *edgeBuilderResult;
    ASSERT_TRUE(edgeBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(edgeBuilder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::A,
                  .state = Platform::DigitalTransition::Down,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(edgeBuilder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::A,
                  .state = Platform::DigitalTransition::Up,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(edgeBuilder.appendInputTransition(Platform::PointerButtonTransition{
                  .window = window,
                  .pointer = Platform::PrimaryPointerId,
                  .button = Platform::PointerButton::Primary,
                  .state = Platform::DigitalTransition::Up,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(edgeBuilder.appendInputTransition(Platform::PointerButtonTransition{
                  .window = window,
                  .pointer = Platform::PrimaryPointerId,
                  .button = Platform::PointerButton::Primary,
                  .state = Platform::DigitalTransition::Down,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(edgeBuilder.appendInputTransition(Platform::GamepadButtonTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .button = Platform::GamepadButton::South,
                  .state = Platform::DigitalTransition::Up,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(edgeBuilder.appendInputTransition(Platform::GamepadButtonTransition{
                  .routedWindow = window,
                  .gamepad = gamepad,
                  .button = Platform::GamepadButton::South,
                  .state = Platform::DigitalTransition::Down,
              }),
              Platform::FrameBatchAppendResult::Appended);
    auto edgeInput = validWindowInput(window, 1);
    edgeInput.pointer.heldButtons.set(static_cast<usize>(Platform::PointerButton::Primary));
    EXPECT_TRUE(edgeBuilder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), edgeInput));
    auto finalGamepad = validGamepadSnapshot(gamepad, 1);
    finalGamepad.heldButtons.set(static_cast<usize>(Platform::GamepadButton::South));
    const std::array finalGamepads{finalGamepad};
    ASSERT_TRUE(edgeBuilder.setGamepadSnapshots(finalGamepads));
    EXPECT_TRUE(edgeBuilder.finishFrame().has_value());

    auto resetBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(resetBuilderResult.has_value());
    auto& resetBuilder = *resetBuilderResult;
    ASSERT_TRUE(resetBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(resetBuilder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::A,
                  .state = Platform::DigitalTransition::Down,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(resetBuilder.appendInputTransition(Platform::InputStreamReset{
                  .routedWindow = window,
                  .reason = Platform::InputResetReason::BackendRecovery,
              }),
              Platform::FrameBatchAppendResult::ResetInserted);
    EXPECT_TRUE(resetBuilder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(resetBuilder.finishFrame().has_value());

    auto cancelBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(cancelBuilderResult.has_value());
    auto& cancelBuilder = *cancelBuilderResult;
    ASSERT_TRUE(cancelBuilder.beginFrame({1}).has_value());
    EXPECT_EQ(cancelBuilder.appendInputTransition(Platform::KeyTransition{
                  .window = window,
                  .key = Platform::Key::A,
                  .state = Platform::DigitalTransition::Down,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(cancelBuilder.appendInputTransition(Platform::InputCancelTransition{
                  .routedWindow = window,
                  .reason = Platform::InputCancelReason::FocusLost,
              }),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_TRUE(cancelBuilder.setPrimaryWindowSnapshot(validWindowMetrics(window, 1), validWindowInput(window, 1)));
    EXPECT_TRUE(cancelBuilder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, RejectsInvalidFinalWindowSnapshots)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto expectInvalidWindowSnapshot = [](Platform::WindowMetricsSnapshot metrics,
                                          Platform::WindowInputSnapshot input) {
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        auto& builder = *builderResult;
        ASSERT_TRUE(builder.beginFrame({1}).has_value());
        ASSERT_TRUE(builder.setPrimaryWindowSnapshot(metrics, input));
        auto frame = builder.finishFrame();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidFrameSnapshot);
    };

    expectInvalidWindowSnapshot({.revision = 1}, {.sourceMetricsRevision = 1});

    auto metrics = validWindowMetrics(window, 0);
    auto input = validWindowInput(window, 0);
    expectInvalidWindowSnapshot(metrics, input);

    metrics = validWindowMetrics(window, 1);
    input = validWindowInput(window, 1);
    metrics.logicalExtent = {};
    expectInvalidWindowSnapshot(metrics, input);

    metrics = validWindowMetrics(window, 1);
    input = validWindowInput(window, 1);
    metrics.contentScale.x = 0.0F;
    expectInvalidWindowSnapshot(metrics, input);

    metrics = validWindowMetrics(window, 1);
    input = validWindowInput(window, 1);
    metrics.contentScale.y = std::numeric_limits<float>::quiet_NaN();
    expectInvalidWindowSnapshot(metrics, input);

    metrics = validWindowMetrics(window, 1);
    input = validWindowInput(window, 1);
    input.pointer.pointer = Platform::PrimaryPointerId + 1;
    expectInvalidWindowSnapshot(metrics, input);

    metrics = validWindowMetrics(window, 1);
    input = validWindowInput(window, 1);
    input.pointer.logicalX = std::numeric_limits<double>::quiet_NaN();
    expectInvalidWindowSnapshot(metrics, input);

    metrics = validWindowMetrics(window, 1);
    input = validWindowInput(window, 1);
    input.pointer.accumulatedDeltaY = std::numeric_limits<double>::infinity();
    expectInvalidWindowSnapshot(metrics, input);
}

TEST(PlatformFrameBuilderTest, RejectsInvalidFinalGamepadSnapshots)
{
    auto poolResult = TestGamepadPool::Create(2);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::GamepadId first = createGamepadId(pool);
    const Platform::GamepadId second = createGamepadId(pool);
    auto overflowPoolResult = TestGamepadPool::Create(Platform::PlatformFrameBuilder::MaximumGamepadSlots + 1U);
    ASSERT_TRUE(overflowPoolResult.has_value());
    auto& overflowPool = *overflowPoolResult;
    const Platform::GamepadId gamepadOutsideRoutingSlots =
        createGamepadIdAtIndex(overflowPool, static_cast<u32>(Platform::PlatformFrameBuilder::MaximumGamepadSlots));

    auto expectInvalidGamepadSnapshots = [](std::vector<Platform::GamepadSnapshot> snapshots) {
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        auto& builder = *builderResult;
        ASSERT_TRUE(builder.beginFrame({1}).has_value());
        ASSERT_TRUE(builder.setGamepadSnapshots(snapshots));
        auto frame = builder.finishFrame();
        ASSERT_FALSE(frame.has_value());
        EXPECT_EQ(frame.error().code, Platform::PlatformErrorCode::InvalidFrameSnapshot);
    };

    expectInvalidGamepadSnapshots({Platform::GamepadSnapshot{.revision = 1}});
    expectInvalidGamepadSnapshots({validGamepadSnapshot(first, 0)});
    expectInvalidGamepadSnapshots({validGamepadSnapshot(gamepadOutsideRoutingSlots, 1)});

    auto invalidAxis = validGamepadSnapshot(first, 1);
    invalidAxis.axes[static_cast<usize>(Platform::GamepadAxis::LeftX)] = std::numeric_limits<float>::quiet_NaN();
    expectInvalidGamepadSnapshots({invalidAxis});

    expectInvalidGamepadSnapshots({
        validGamepadSnapshot(first, 1),
        validGamepadSnapshot(first, 2),
    });

    auto reusedPoolResult = TestGamepadPool::Create(1);
    ASSERT_TRUE(reusedPoolResult.has_value());
    auto& reusedPool = *reusedPoolResult;
    const Platform::GamepadId staleGeneration = createGamepadId(reusedPool);
    ASSERT_EQ(reusedPool.erase(staleGeneration), Core::GenerationEraseResult::Erased);
    const Platform::GamepadId currentGeneration = createGamepadId(reusedPool);
    ASSERT_EQ(staleGeneration.index(), currentGeneration.index());
    ASSERT_NE(staleGeneration.generation(), currentGeneration.generation());
    expectInvalidGamepadSnapshots({
        validGamepadSnapshot(staleGeneration, 1),
        validGamepadSnapshot(currentGeneration, 1),
    });

    auto otherOwnerPoolResult = TestGamepadPool::Create(2);
    ASSERT_TRUE(otherOwnerPoolResult.has_value());
    auto& otherOwnerPool = *otherOwnerPoolResult;
    static_cast<void>(createGamepadId(otherOwnerPool));
    const Platform::GamepadId otherOwnerSecond = createGamepadId(otherOwnerPool);
    ASSERT_EQ(first.index(), 0U);
    ASSERT_EQ(otherOwnerSecond.index(), 1U);
    ASSERT_NE(first.owner(), otherOwnerSecond.owner());
    expectInvalidGamepadSnapshots({
        validGamepadSnapshot(first, 1),
        validGamepadSnapshot(otherOwnerSecond, 1),
    });

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    std::vector validSnapshots{
        validGamepadSnapshot(first, 1),
        validGamepadSnapshot(second, 1),
    };
    ASSERT_TRUE(builder.setGamepadSnapshots(validSnapshots));
    EXPECT_TRUE(builder.finishFrame().has_value());
}

TEST(PlatformFrameBuilderTest, CoalescesWindowMetricsBeforeEventOverflow)
{
    auto poolResult = TestWindowPool::Create(1);
    ASSERT_TRUE(poolResult.has_value());
    auto& pool = *poolResult;
    const Platform::WindowId window = createWindowId(pool);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
        .platformEventCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto& builder = *builderResult;
    ASSERT_TRUE(builder.beginFrame({1}).has_value());
    EXPECT_EQ(builder.appendPlatformEvent(Platform::WindowMetricsChangedEvent{window, 1}),
              Platform::FrameBatchAppendResult::Appended);
    EXPECT_EQ(builder.appendPlatformEvent(Platform::WindowMetricsChangedEvent{window, 2}),
              Platform::FrameBatchAppendResult::Coalesced);
    EXPECT_TRUE(builder.setPrimaryWindowSnapshot(validWindowMetrics(window, 2), validWindowInput(window, 2)));

    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->platformEvents().size(), 1U);
    const auto* metrics = std::get_if<Platform::WindowMetricsChangedEvent>(&frame->platformEvents().front().payload);
    ASSERT_NE(metrics, nullptr);
    EXPECT_EQ(metrics->metricsRevision, 2U);
    EXPECT_EQ(frame->diagnostics().platformEventOverflowCount, 0U);
}

static_assert(!std::is_default_constructible_v<Platform::PlatformPollResult>);
static_assert(std::is_move_constructible_v<Platform::PlatformFrameBuilder>);
static_assert(!std::is_copy_constructible_v<Platform::PlatformFrameBuilder>);
static_assert(std::is_same_v<Platform::InputTransitionBatch, std::span<const Platform::InputTransition>>);

} // namespace
} // namespace Tina::Tests
