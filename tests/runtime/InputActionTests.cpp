#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/InputRouting.hpp>

#include "../../src/runtime/input/ActionMapper.hpp"
#include "../../src/runtime/input/SimulationActionLatch.hpp"

#include <array>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

using Runtime::Input::ActionMapper;
using UI::ContinuousControlClaim;
using UI::ContinuousControlClaimsView;
using UI::InputTransitionConsumptionView;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;

inline constexpr InputActionId MoveAction{1};
inline constexpr InputActionId JumpAction{2};
inline constexpr InputActionId ExitAction{3};

struct TestFrameInput final {
    std::vector<Platform::Key> heldKeys;
    std::vector<Platform::PointerButton> heldPointerButtons;
    std::vector<Platform::GamepadSnapshot> gamepads;
    std::vector<Platform::InputTransitionPayload> transitions;
    std::vector<Platform::PlatformEventPayload> platformEvents;
    std::vector<usize> consumedOrdinals;
    std::vector<ContinuousControlClaim> claims;
};

[[nodiscard]] DigitalActionBinding keyBinding(Platform::Key key, InputActionId action,
                                              InputActionDomain domain = InputActionDomain::Simulation)
{
    return DigitalActionBinding{
        .input = PrimaryWindowKeyBinding{key},
        .action = action,
        .domain = domain,
    };
}

[[nodiscard]] DigitalActionBinding gamepadBinding(Platform::GamepadButton button, InputActionId action,
                                                  InputActionDomain domain = InputActionDomain::Simulation)
{
    return DigitalActionBinding{
        .input = StandardGamepadButtonBinding{button},
        .action = action,
        .domain = domain,
    };
}

[[nodiscard]] Core::Status mapTestFrame(ActionMapper& mapper, Platform::PlatformFrameBuilder& builder,
                                        Platform::WindowId window, u64 platformFrameValue, u64 engineFrameIndex,
                                        u64 nextSimulationTick, const TestFrameInput& input)
{
    const Platform::PlatformFrameId frameId{platformFrameValue};
    if (auto status = builder.beginFrame(frameId); !status)
    {
        return status;
    }

    Platform::WindowMetricsSnapshot metrics{
        .window = window,
        .logicalExtent = {1280, 720},
        .framebufferExtent = {1280, 720},
        .contentScale = {1.0F, 1.0F},
        .revision = platformFrameValue,
        .focused = true,
        .visible = true,
    };
    Platform::WindowInputSnapshot snapshot{
        .window = window,
        .sourceMetricsRevision = platformFrameValue,
    };
    for (Platform::Key key : input.heldKeys)
    {
        snapshot.heldKeys.set(static_cast<usize>(key));
    }
    for (Platform::PointerButton button : input.heldPointerButtons)
    {
        snapshot.pointer.heldButtons.set(static_cast<usize>(button));
    }
    if (!builder.setPrimaryWindowSnapshot(metrics, snapshot))
    {
        return Core::failure(Core::CoreErrorCode::Internal, "test primary Window snapshot was rejected");
    }
    if (!builder.setGamepadSnapshots(input.gamepads))
    {
        return Core::failure(Core::CoreErrorCode::Internal, "test Gamepad snapshots were rejected");
    }
    for (const Platform::InputTransitionPayload& transition : input.transitions)
    {
        const Platform::FrameBatchAppendResult appendResult = builder.appendInputTransition(transition);
        if (appendResult == Platform::FrameBatchAppendResult::FrameNotOpen ||
            appendResult == Platform::FrameBatchAppendResult::IgnoredAfterReset)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "test Input transition was rejected");
        }
    }
    for (const Platform::PlatformEventPayload& event : input.platformEvents)
    {
        const Platform::FrameBatchAppendResult appendResult = builder.appendPlatformEvent(event);
        if (appendResult != Platform::FrameBatchAppendResult::Appended &&
            appendResult != Platform::FrameBatchAppendResult::Coalesced &&
            appendResult != Platform::FrameBatchAppendResult::ResetInserted)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "test Platform event was rejected");
        }
    }

    auto frameResult = builder.finishFrame();
    if (!frameResult)
    {
        return Core::failure(std::move(frameResult.error()));
    }

    std::vector<u64> consumedWords;
    InputTransitionConsumptionView consumption =
        InputTransitionConsumptionView::None(frameId, input.transitions.size());
    if (!input.consumedOrdinals.empty())
    {
        constexpr usize BitsPerWord = sizeof(u64) * 8U;
        consumedWords.resize((input.transitions.size() + BitsPerWord - 1U) / BitsPerWord);
        for (usize ordinal : input.consumedOrdinals)
        {
            if (ordinal >= input.transitions.size())
            {
                return Core::failure(Core::CoreErrorCode::InvalidArgument, "test consumed ordinal is out of range");
            }
            consumedWords[ordinal / BitsPerWord] |= u64{1} << (ordinal % BitsPerWord);
        }
        consumption.consumedOrdinalWords = consumedWords;
    }
    const ContinuousControlClaimsView claims{
        .platformFrame = frameId,
        .controls = input.claims,
    };
    return mapper.mapFrame(*frameResult, consumption, claims, engineFrameIndex, nextSimulationTick);
}

[[nodiscard]] const DigitalActionTransition* digital(const SimulationActionTransition& transition)
{
    return std::get_if<DigitalActionTransition>(&transition);
}

[[nodiscard]] const DigitalActionTransition* digital(const FrameActionTransition& transition)
{
    return std::get_if<DigitalActionTransition>(&transition);
}

class InputActionMapperTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto poolResult = WindowPool::Create(1);
        ASSERT_TRUE(poolResult.has_value());
        windowPool_ = std::make_unique<WindowPool>(std::move(*poolResult));
        auto windowResult = windowPool_->tryEmplace(0);
        ASSERT_TRUE(windowResult.has_value());
        window_ = *windowResult;
        auto builderResult = Platform::PlatformFrameBuilder::Create();
        ASSERT_TRUE(builderResult.has_value());
        frameBuilder_ = std::make_unique<Platform::PlatformFrameBuilder>(std::move(*builderResult));
        auto gamepadPoolResult = GamepadPool::Create(1);
        ASSERT_TRUE(gamepadPoolResult.has_value());
        gamepadPool_ = std::make_unique<GamepadPool>(std::move(*gamepadPoolResult));
        auto gamepadResult = gamepadPool_->tryEmplace(0);
        ASSERT_TRUE(gamepadResult.has_value());
        gamepad_ = *gamepadResult;
    }

    [[nodiscard]] std::unique_ptr<ActionMapper> createMapper(std::span<const DigitalActionBinding> bindings,
                                                             InputActionMapperCapacityConfig capacities = {})
    {
        auto mapperResult = ActionMapper::Create(bindings, capacities);
        EXPECT_TRUE(mapperResult.has_value());
        return mapperResult ? std::move(*mapperResult) : nullptr;
    }

    std::unique_ptr<WindowPool> windowPool_;
    std::unique_ptr<GamepadPool> gamepadPool_;
    std::unique_ptr<Platform::PlatformFrameBuilder> frameBuilder_;
    Platform::WindowId window_{};
    Platform::GamepadId gamepad_{};
};

TEST(InputActionMapperConfigurationTest, RejectsDuplicatePhysicalBindingAndCapacityOverflow)
{
    const std::array duplicateBindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::A, JumpAction, InputActionDomain::Frame),
    };
    auto duplicateResult = ActionMapper::Create(duplicateBindings);
    ASSERT_FALSE(duplicateResult.has_value());
    EXPECT_EQ(duplicateResult.error().code, ConfigurationErrorCode::InvalidEngineConfig);

    InputActionMapperCapacityConfig capacities;
    capacities.digitalActionBindingCapacity = 1;
    const std::array twoBindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
    };
    auto capacityResult = ActionMapper::Create(twoBindings, capacities);
    ASSERT_FALSE(capacityResult.has_value());
    EXPECT_EQ(capacityResult.error().code, ConfigurationErrorCode::InvalidEngineConfig);

    const std::array invalidDomain{
        DigitalActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::A},
            .action = MoveAction,
            .domain = static_cast<InputActionDomain>(255),
        },
    };
    EXPECT_FALSE(ActionMapper::Create(invalidDomain).has_value());

    const std::array unsupportedPointer{
        DigitalActionBinding{
            .input =
                PrimaryPointerButtonBinding{
                    .pointer = Platform::PrimaryPointerId + 1,
                    .button = Platform::PointerButton::Primary,
                },
            .action = MoveAction,
        },
    };
    EXPECT_FALSE(ActionMapper::Create(unsupportedPointer).has_value());

    const std::array crossDomainAction{
        keyBinding(Platform::Key::A, MoveAction, InputActionDomain::Simulation),
        keyBinding(Platform::Key::B, MoveAction, InputActionDomain::Frame),
    };
    EXPECT_FALSE(ActionMapper::Create(crossDomainAction).has_value());
}

TEST_F(InputActionMapperTest, SimulationEdgesSurviveZeroStepFramesAndOnlyFirstTickConsumesThem)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());

    TestFrameInput up;
    up.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Up,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, up).has_value());

    auto firstTick = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(firstTick.has_value());
    ASSERT_EQ(firstTick->transitions.size(), 2U);
    ASSERT_NE(digital(firstTick->transitions[0]), nullptr);
    ASSERT_NE(digital(firstTick->transitions[1]), nullptr);
    EXPECT_EQ(digital(firstTick->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_EQ(digital(firstTick->transitions[1])->kind, DigitalActionTransitionKind::Released);
    EXPECT_LT(digital(firstTick->transitions[0])->sourceSequence, digital(firstTick->transitions[1])->sourceSequence);
    EXPECT_FALSE(firstTick->isHeld(MoveAction));

    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());
    for (u64 catchUpTick = 1; catchUpTick <= 3; ++catchUpTick)
    {
        auto catchUp = mapper->simulationActionsForTick(catchUpTick);
        ASSERT_TRUE(catchUp.has_value());
        EXPECT_TRUE(catchUp->transitions.empty());
        EXPECT_FALSE(catchUp->isHeld(MoveAction));
        ASSERT_TRUE(mapper->completeSimulationTick(catchUpTick).has_value());
    }
}

TEST_F(InputActionMapperTest, FrameActionEdgesExpireAtTheNextRenderFrame)
{
    const std::array bindings{
        keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::Escape};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::Escape,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());
    FrameActionSnapshot firstFrame = mapper->frameActions();
    ASSERT_EQ(firstFrame.transitions.size(), 1U);
    ASSERT_NE(digital(firstFrame.transitions[0]), nullptr);
    EXPECT_EQ(digital(firstFrame.transitions[0])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_TRUE(firstFrame.isHeld(ExitAction));

    TestFrameInput held;
    held.heldKeys = {Platform::Key::Escape};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, held).has_value());
    FrameActionSnapshot secondFrame = mapper->frameActions();
    EXPECT_TRUE(secondFrame.transitions.empty());
    EXPECT_TRUE(secondFrame.isHeld(ExitAction));
}

TEST_F(InputActionMapperTest, ConsumedDownSuppressesGameplayUntilTrueUpWithoutReleaseEdge)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput consumedDown;
    consumedDown.heldKeys = {Platform::Key::A};
    consumedDown.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    consumedDown.consumedOrdinals = {0};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, consumedDown).has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value());
    EXPECT_TRUE(suppressed->transitions.empty());
    EXPECT_FALSE(suppressed->isHeld(MoveAction));

    TestFrameInput stillHeld;
    stillHeld.heldKeys = {Platform::Key::A};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, stillHeld).has_value());
    auto stillSuppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(stillSuppressed.has_value());
    EXPECT_TRUE(stillSuppressed->transitions.empty());

    TestFrameInput released;
    released.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Up,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 3, 2, 0, released).has_value());
    auto afterRelease = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(afterRelease.has_value());
    EXPECT_TRUE(afterRelease->transitions.empty());

    TestFrameInput pressedAgain;
    pressedAgain.heldKeys = {Platform::Key::A};
    pressedAgain.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 4, 3, 0, pressedAgain).has_value());
    auto restored = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions[0]), nullptr);
    EXPECT_EQ(digital(restored->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
}

TEST_F(InputActionMapperTest, ClaimOfPendingHeldControlCancelsUnobservedPress)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());

    TestFrameInput claimed;
    claimed.heldKeys = {Platform::Key::A};
    claimed.claims = {ContinuousControlClaim{
        .control = Platform::KeyControlIdentity{window_, Platform::Key::A},
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, claimed).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->transitions.empty());
    EXPECT_FALSE(snapshot->isHeld(MoveAction));
}

TEST_F(InputActionMapperTest, ClaimAfterSimulationObservedPressProducesCancelNotRelease)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    TestFrameInput claimed;
    claimed.heldKeys = {Platform::Key::A};
    claimed.claims = {ContinuousControlClaim{
        .control = Platform::KeyControlIdentity{window_, Platform::Key::A},
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, claimed).has_value());

    auto cancelled = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(cancelled->transitions.size(), 1U);
    ASSERT_NE(digital(cancelled->transitions[0]), nullptr);
    EXPECT_EQ(digital(cancelled->transitions[0])->kind, DigitalActionTransitionKind::Cancelled);
    EXPECT_FALSE(cancelled->isHeld(MoveAction));
}

TEST_F(InputActionMapperTest, FocusCancelNeverFabricatesReleased)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    TestFrameInput focusLost;
    focusLost.transitions = {Platform::InputCancelTransition{
        .routedWindow = window_,
        .reason = Platform::InputCancelReason::FocusLost,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, focusLost).has_value());
    auto cancelled = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(cancelled->transitions.size(), 1U);
    ASSERT_NE(digital(cancelled->transitions[0]), nullptr);
    EXPECT_EQ(digital(cancelled->transitions[0])->kind, DigitalActionTransitionKind::Cancelled);
}

TEST_F(InputActionMapperTest, RawResetPreservesMarkerAndSuppressesHeldControlsUntilUp)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());

    TestFrameInput reset;
    reset.heldKeys = {Platform::Key::A};
    reset.transitions = {Platform::InputStreamReset{
        .routedWindow = window_,
        .reason = Platform::InputResetReason::CapacityExceeded,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, reset).has_value());
    auto resetSnapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(resetSnapshot.has_value());
    ASSERT_EQ(resetSnapshot->transitions.size(), 1U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&resetSnapshot->transitions[0]), nullptr);
    EXPECT_FALSE(resetSnapshot->isHeld(MoveAction));

    TestFrameInput up;
    up.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Up,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 3, 2, 0, up).has_value());

    TestFrameInput downAgain;
    downAgain.heldKeys = {Platform::Key::A};
    downAgain.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 4, 3, 0, downAgain).has_value());
    auto recovered = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(recovered.has_value());
    ASSERT_EQ(recovered->transitions.size(), 2U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&recovered->transitions[0]), nullptr);
    ASSERT_NE(digital(recovered->transitions[1]), nullptr);
    EXPECT_EQ(digital(recovered->transitions[1])->kind, DigitalActionTransitionKind::Pressed);
}

TEST_F(InputActionMapperTest, SimulationOverflowInsertsResetAndAcceptsPostResetInput)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
    };
    InputActionMapperCapacityConfig capacities;
    capacities.simulationActionTransitionCapacity = 1;
    auto mapper = createMapper(bindings, capacities);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput frame;
    frame.heldKeys = {Platform::Key::A};
    frame.transitions = {
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::B,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Up,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::B,
            .state = Platform::DigitalTransition::Up,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, frame).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->transitions.size(), 2U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&snapshot->transitions[0]), nullptr);
    ASSERT_NE(digital(snapshot->transitions[1]), nullptr);
    EXPECT_EQ(digital(snapshot->transitions[1])->action, MoveAction);
    EXPECT_EQ(digital(snapshot->transitions[1])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_TRUE(snapshot->isHeld(MoveAction));
    EXPECT_FALSE(snapshot->isHeld(JumpAction));
    EXPECT_EQ(mapper->simulationLatchStatistics().capacityResetCount, 1U);
}

TEST_F(InputActionMapperTest, SimulationOverflowDoesNotSuppressLaterDownInSameRawBatch)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
        keyBinding(Platform::Key::C, ExitAction),
    };
    InputActionMapperCapacityConfig capacities;
    capacities.simulationActionTransitionCapacity = 1;
    auto mapper = createMapper(bindings, capacities);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput frame;
    frame.heldKeys = {
        Platform::Key::A,
        Platform::Key::B,
        Platform::Key::C,
    };
    frame.transitions = {
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::B,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::C,
            .state = Platform::DigitalTransition::Down,
        },
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, frame).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->transitions.size(), 2U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&snapshot->transitions[0]), nullptr);
    const auto* postReset = digital(snapshot->transitions[1]);
    ASSERT_NE(postReset, nullptr);
    EXPECT_EQ(postReset->action, ExitAction);
    EXPECT_EQ(postReset->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_FALSE(snapshot->isHeld(MoveAction));
    EXPECT_FALSE(snapshot->isHeld(JumpAction));
    EXPECT_TRUE(snapshot->isHeld(ExitAction));
    EXPECT_EQ(mapper->simulationLatchStatistics().capacityResetCount, 1U);
}

TEST_F(InputActionMapperTest, MultiplePhysicalBindingsReleaseOnlyAfterLastSource)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::D, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput frame;
    frame.transitions = {
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::D,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = Platform::DigitalTransition::Up,
        },
        Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::D,
            .state = Platform::DigitalTransition::Up,
        },
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, frame).has_value());
    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->transitions.size(), 2U);
    ASSERT_NE(digital(snapshot->transitions[0]), nullptr);
    ASSERT_NE(digital(snapshot->transitions[1]), nullptr);
    EXPECT_EQ(digital(snapshot->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_EQ(digital(snapshot->transitions[1])->kind, DigitalActionTransitionKind::Released);
}

TEST_F(InputActionMapperTest, ConsumedUpCancelsObservedActionInsteadOfReleasingIt)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    TestFrameInput up;
    up.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Up,
    }};
    up.consumedOrdinals = {0};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, up).has_value());
    auto snapshot = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    ASSERT_NE(digital(snapshot->transitions[0]), nullptr);
    EXPECT_EQ(digital(snapshot->transitions[0])->kind, DigitalActionTransitionKind::Cancelled);
}

TEST_F(InputActionMapperTest, RejectsDroppedKeyReleaseAgainstFinalSnapshot)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    const TestFrameInput droppedUp;
    const Core::Status status = mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, droppedUp);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(InputActionMapperTest, RejectsDroppedGamepadDisconnectAgainstFinalSnapshot)
{
    const std::array bindings{
        gamepadBinding(Platform::GamepadButton::South, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    Platform::GamepadSnapshot heldGamepad{
        .gamepad = gamepad_,
        .revision = 1,
    };
    heldGamepad.heldButtons.set(static_cast<usize>(Platform::GamepadButton::South));
    TestFrameInput down;
    down.gamepads = {heldGamepad};
    down.transitions = {Platform::GamepadButtonTransition{
        .routedWindow = window_,
        .gamepad = gamepad_,
        .button = Platform::GamepadButton::South,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, down).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    const TestFrameInput droppedDisconnect;
    const Core::Status status = mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, droppedDisconnect);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(InputActionMapperTest, RefreshesGamepadRouteAfterWindowGenerationChanges)
{
    const std::array bindings{
        gamepadBinding(Platform::GamepadButton::South, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput initialRoute;
    initialRoute.gamepads = {Platform::GamepadSnapshot{
        .gamepad = gamepad_,
        .revision = 1,
    }};
    initialRoute.transitions = {
        Platform::GamepadButtonTransition{
            .routedWindow = window_,
            .gamepad = gamepad_,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::GamepadButtonTransition{
            .routedWindow = window_,
            .gamepad = gamepad_,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Up,
        },
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, initialRoute).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    ASSERT_EQ(windowPool_->erase(window_), Core::GenerationEraseResult::Erased);
    auto replacementResult = windowPool_->tryEmplace(1);
    ASSERT_TRUE(replacementResult.has_value());
    const Platform::WindowId replacementWindow = *replacementResult;
    ASSERT_EQ(replacementWindow.index(), window_.index());
    ASSERT_NE(replacementWindow.generation(), window_.generation());

    Platform::GamepadSnapshot heldGamepad{
        .gamepad = gamepad_,
        .revision = 2,
    };
    heldGamepad.heldButtons.set(static_cast<usize>(Platform::GamepadButton::South));
    TestFrameInput focusLost;
    focusLost.gamepads = {heldGamepad};
    focusLost.transitions = {
        Platform::GamepadButtonTransition{
            .routedWindow = replacementWindow,
            .gamepad = gamepad_,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::InputCancelTransition{
            .routedWindow = replacementWindow,
            .reason = Platform::InputCancelReason::FocusLost,
        },
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, replacementWindow, 2, 1, 1, focusLost).has_value());

    auto snapshot = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(snapshot->isHeld(MoveAction));
}

TEST_F(InputActionMapperTest, DisconnectOfInactiveGamepadDoesNotDeleteKeyboardPendingEdge)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        gamepadBinding(Platform::GamepadButton::South, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput registerGamepad;
    registerGamepad.gamepads = {Platform::GamepadSnapshot{
        .gamepad = gamepad_,
        .revision = 1,
    }};
    registerGamepad.transitions = {
        Platform::GamepadButtonTransition{
            .routedWindow = window_,
            .gamepad = gamepad_,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Down,
        },
        Platform::GamepadButtonTransition{
            .routedWindow = window_,
            .gamepad = gamepad_,
            .button = Platform::GamepadButton::South,
            .state = Platform::DigitalTransition::Up,
        },
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, registerGamepad).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    TestFrameInput keyboardDown;
    keyboardDown.heldKeys = {Platform::Key::A};
    keyboardDown.gamepads = {Platform::GamepadSnapshot{
        .gamepad = gamepad_,
        .revision = 2,
    }};
    keyboardDown.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, keyboardDown).has_value());

    TestFrameInput disconnected;
    disconnected.heldKeys = {Platform::Key::A};
    disconnected.transitions = {Platform::InputCancelTransition{
        .routedWindow = window_,
        .reason = Platform::InputCancelReason::DeviceDisconnected,
        .gamepad = gamepad_,
    }};
    disconnected.platformEvents = {Platform::GamepadDisconnectedEvent{
        .gamepad = gamepad_,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 3, 2, 1, disconnected).has_value());

    auto pending = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(pending.has_value());
    ASSERT_EQ(pending->transitions.size(), 1U);
    ASSERT_NE(digital(pending->transitions[0]), nullptr);
    EXPECT_EQ(digital(pending->transitions[0])->action, MoveAction);
    EXPECT_EQ(digital(pending->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_TRUE(pending->isHeld(MoveAction));
}

TEST_F(InputActionMapperTest, UiOutputFromAnotherPlatformFrameIsRejectedBeforeMutation)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    auto builderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(builderResult.has_value());
    auto builder = std::move(*builderResult);
    ASSERT_TRUE(builder.beginFrame(Platform::PlatformFrameId{1}).has_value());
    Platform::WindowMetricsSnapshot metrics{
        .window = window_,
        .logicalExtent = {1280, 720},
        .framebufferExtent = {1280, 720},
        .contentScale = {1.0F, 1.0F},
        .revision = 1,
        .focused = true,
        .visible = true,
    };
    Platform::WindowInputSnapshot input{
        .window = window_,
        .sourceMetricsRevision = 1,
    };
    ASSERT_TRUE(builder.setPrimaryWindowSnapshot(metrics, input));
    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());

    const auto consumption = InputTransitionConsumptionView::None(Platform::PlatformFrameId{2}, 0);
    const auto claims = ContinuousControlClaimsView::None(Platform::PlatformFrameId{1});
    auto status = mapper->mapFrame(*frame, consumption, claims, 0, 0);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(InputActionMapperTest, RawSequenceRegressionAcrossZeroStepFramesIsRejected)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);
    auto firstBuilderResult = Platform::PlatformFrameBuilder::Create();
    auto regressedBuilderResult = Platform::PlatformFrameBuilder::Create();
    ASSERT_TRUE(firstBuilderResult.has_value());
    ASSERT_TRUE(regressedBuilderResult.has_value());
    auto firstBuilder = std::move(*firstBuilderResult);
    auto regressedBuilder = std::move(*regressedBuilderResult);

    TestFrameInput down;
    down.heldKeys = {Platform::Key::A};
    down.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Down,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, firstBuilder, window_, 1, 0, 0, down).has_value());

    TestFrameInput up;
    up.transitions = {Platform::KeyTransition{
        .window = window_,
        .key = Platform::Key::A,
        .state = Platform::DigitalTransition::Up,
    }};
    auto regressed = mapTestFrame(*mapper, regressedBuilder, window_, 2, 1, 0, up);
    ASSERT_FALSE(regressed.has_value());
    EXPECT_EQ(regressed.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    auto original = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(original.has_value());
    ASSERT_EQ(original->transitions.size(), 1U);
    ASSERT_NE(digital(original->transitions[0]), nullptr);
    EXPECT_EQ(digital(original->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_TRUE(original->isHeld(MoveAction));
}

TEST(SimulationActionLatchTest, RejectsWrongTargetBeforeChangingPendingBatch)
{
    const std::array actions{MoveAction};
    auto latchResult = Runtime::Input::SimulationActionLatch::Create(actions, 4);
    ASSERT_TRUE(latchResult.has_value());
    auto latch = std::move(*latchResult);
    ASSERT_TRUE(latch.setHeld(MoveAction, true).has_value());
    ASSERT_TRUE(latch
                    .append(5,
                            DigitalActionTransition{
                                .action = MoveAction,
                                .kind = DigitalActionTransitionKind::Pressed,
                                .sourceSequence = 1,
                            })
                    .has_value());

    auto wrongTarget = latch.reconcileCancellation(6, MoveAction, 0, 2, true);
    ASSERT_FALSE(wrongTarget.has_value());
    auto original = latch.snapshotForTick(5);
    ASSERT_TRUE(original.has_value());
    ASSERT_EQ(original->transitions.size(), 1U);
    ASSERT_NE(digital(original->transitions[0]), nullptr);
    EXPECT_EQ(digital(original->transitions[0])->kind, DigitalActionTransitionKind::Pressed);
}

} // namespace
} // namespace Tina::Tests
