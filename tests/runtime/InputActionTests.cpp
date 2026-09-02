#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/InputRouting.hpp>

#include "../../src/runtime/input/ActionMapper.hpp"
#include "../../src/runtime/input/SimulationActionLatch.hpp"

#include <array>
#include <cmath>
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

[[nodiscard]] InputActionMapConfig actionMapConfig(
    std::span<const InputActionBinding> bindings,
    InputActionMapCapacityConfig capacities = {})
{
    return InputActionMapConfig{
        .capacities = capacities,
        .bindings = std::vector<InputActionBinding>(bindings.begin(), bindings.end()),
    };
}

struct TestFrameInput final {
    std::vector<Platform::Key> heldKeys;
    std::vector<Platform::PointerButton> heldPointerButtons;
    // Whole-slot overrides applied after the primary-pointer conveniences above, so a test
    // can place a second finger or give a pointer motion the snapshot has to carry.
    std::vector<Platform::PointerSnapshot> pointerOverrides;
    std::vector<Platform::GamepadSnapshot> gamepads;
    std::vector<Platform::InputTransitionPayload> transitions;
    std::vector<Platform::PlatformEventPayload> platformEvents;
    std::vector<usize> consumedOrdinals;
    std::vector<ContinuousControlClaim> claims;
};

[[nodiscard]] InputActionBinding keyBinding(Platform::Key key, InputActionId action,
                                              InputActionDomain domain = InputActionDomain::Simulation)
{
    return InputActionBinding{
        .input = PrimaryWindowKeyBinding{key},
        .action = action,
        .domain = domain,
    };
}

[[nodiscard]] InputActionBinding gamepadBinding(Platform::GamepadButton button, InputActionId action,
                                                  InputActionDomain domain = InputActionDomain::Simulation)
{
    return InputActionBinding{
        .input = StandardGamepadButtonBinding{button},
        .action = action,
        .domain = domain,
    };
}

[[nodiscard]] InputActionBinding gamepadAxisBinding(
    Platform::GamepadAxis axis, InputActionId action,
    GamepadAxisValueMode valueMode = GamepadAxisValueMode::Signed,
    InputActionDomain domain = InputActionDomain::Simulation, float deadzone = 0.0F,
    float scale = 1.0F,
    ActionCompositionMode composition = ActionCompositionMode::SumClamped)
{
    return InputActionBinding{
        .input = StandardGamepadAxisBinding{.axis = axis, .valueMode = valueMode},
        .action = action,
        .domain = domain,
        .composition = composition,
        .deadzone = deadzone,
        .scale = scale,
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
        snapshot.pointers[Platform::PrimaryPointerId].heldButtons.set(static_cast<usize>(button));
    }
    for (const Platform::PointerSnapshot& pointer : input.pointerOverrides)
    {
        if (pointer.pointer >= Platform::PointerCapacity)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument, "test pointer override is out of range");
        }
        snapshot.pointers[pointer.pointer] = pointer;
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

[[nodiscard]] const InputActionTransition* digital(const SimulationActionTransition& transition)
{
    return std::get_if<InputActionTransition>(&transition);
}

[[nodiscard]] const InputActionTransition* digital(const FrameActionTransition& transition)
{
    return std::get_if<InputActionTransition>(&transition);
}

template <typename Transition>
void expectLegalActionReplay(std::span<const Transition> transitions, InputActionId action,
                             float baseline, float current)
{
    constexpr float Epsilon = 1.0e-6F;
    const auto isActive = [](float value) { return std::abs(value) > Epsilon; };
    float replayed = baseline;
    u64 previousSequence = 0;
    bool sawSequence = false;
    for (const Transition& item : transitions)
    {
        const auto* transition = std::get_if<InputActionTransition>(&item);
        ASSERT_NE(transition, nullptr);
        ASSERT_EQ(transition->action, action);
        if (sawSequence)
        {
            EXPECT_LE(previousSequence, transition->sourceSequence);
        }

        const bool wasActive = isActive(replayed);
        const bool nowActive = isActive(transition->value);
        bool legal = false;
        switch (transition->kind)
        {
        case InputActionTransitionKind::Started:
            legal = !wasActive && nowActive;
            break;
        case InputActionTransitionKind::ValueChanged:
            legal = wasActive && nowActive && std::abs(replayed - transition->value) > Epsilon;
            break;
        case InputActionTransitionKind::Completed:
        case InputActionTransitionKind::Cancelled:
            legal = wasActive && !nowActive;
            break;
        }
        EXPECT_TRUE(legal);
        replayed = transition->value;
        previousSequence = transition->sourceSequence;
        sawSequence = true;
    }
    EXPECT_NEAR(replayed, current, Epsilon);
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
        auto gamepadPoolResult = GamepadPool::Create(2);
        ASSERT_TRUE(gamepadPoolResult.has_value());
        gamepadPool_ = std::make_unique<GamepadPool>(std::move(*gamepadPoolResult));
        auto gamepadResult = gamepadPool_->tryEmplace(0);
        ASSERT_TRUE(gamepadResult.has_value());
        gamepad_ = *gamepadResult;
        auto secondGamepadResult = gamepadPool_->tryEmplace(1);
        ASSERT_TRUE(secondGamepadResult.has_value());
        secondGamepad_ = *secondGamepadResult;
    }

    [[nodiscard]] std::unique_ptr<ActionMapper> createMapper(std::span<const InputActionBinding> bindings,
                                                             InputActionMapCapacityConfig capacities = {},
                                                             InputActionMapperCapacityConfig mapperCapacities = {})
    {
        auto mapperResult = ActionMapper::Create(actionMapConfig(bindings, capacities), mapperCapacities);
        EXPECT_TRUE(mapperResult.has_value());
        return mapperResult ? std::move(*mapperResult) : nullptr;
    }

    std::unique_ptr<WindowPool> windowPool_;
    std::unique_ptr<GamepadPool> gamepadPool_;
    std::unique_ptr<Platform::PlatformFrameBuilder> frameBuilder_;
    Platform::WindowId window_{};
    Platform::GamepadId gamepad_{};
    Platform::GamepadId secondGamepad_{};
};

TEST(InputActionMapperConfigurationTest, RejectsDuplicatePhysicalBindingAndCapacityOverflow)
{
    const std::array duplicateBindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::A, JumpAction, InputActionDomain::Frame),
    };
    auto duplicateResult = ActionMapper::Create(actionMapConfig(duplicateBindings));
    ASSERT_FALSE(duplicateResult.has_value());
    EXPECT_EQ(duplicateResult.error().code, ConfigurationErrorCode::InvalidEngineConfig);

    InputActionMapCapacityConfig capacities;
    capacities.actionBindingCapacity = 1;
    const std::array twoBindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
    };
    auto capacityResult = ActionMapper::Create(actionMapConfig(twoBindings, capacities));
    ASSERT_FALSE(capacityResult.has_value());
    EXPECT_EQ(capacityResult.error().code, ConfigurationErrorCode::InvalidEngineConfig);

    const std::array invalidDomain{
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::A},
            .action = MoveAction,
            .domain = static_cast<InputActionDomain>(255),
        },
    };
    EXPECT_FALSE(ActionMapper::Create(actionMapConfig(invalidDomain)).has_value());

    const std::array unsupportedPointer{
        InputActionBinding{
            .input =
                PointerButtonBinding{
                    .pointer = static_cast<Platform::PointerId>(Platform::PointerCapacity),
                    .button = Platform::PointerButton::Primary,
                },
            .action = MoveAction,
        },
    };
    EXPECT_FALSE(ActionMapper::Create(actionMapConfig(unsupportedPointer)).has_value());

    const std::array crossDomainAction{
        keyBinding(Platform::Key::A, MoveAction, InputActionDomain::Simulation),
        keyBinding(Platform::Key::B, MoveAction, InputActionDomain::Frame),
    };
    EXPECT_FALSE(ActionMapper::Create(actionMapConfig(crossDomainAction)).has_value());
}

TEST_F(InputActionMapperTest, AppliesGameplayDeadzoneAndScaleToAnalogValues)
{
    const std::array bindings{
        gamepadAxisBinding(Platform::GamepadAxis::LeftX, MoveAction,
                           GamepadAxisValueMode::Signed, InputActionDomain::Simulation,
                           0.25F, 1.5F),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    Platform::GamepadSnapshot gamepad{
        .gamepad = gamepad_,
        .revision = 1,
    };
    gamepad.axes[static_cast<usize>(Platform::GamepadAxis::LeftX)] = 0.5F;
    TestFrameInput frame;
    frame.gamepads = {gamepad};
    frame.transitions = {Platform::GamepadAxisTransition{
        .routedWindow = window_,
        .gamepad = gamepad_,
        .axis = Platform::GamepadAxis::LeftX,
        .value = 0.5F,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, frame).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NEAR(snapshot->value(MoveAction), 0.5F, 1.0e-6F);
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    const auto* transition = digital(snapshot->transitions.front());
    ASSERT_NE(transition, nullptr);
    EXPECT_EQ(transition->kind, InputActionTransitionKind::Started);
    EXPECT_NEAR(transition->value, 0.5F, 1.0e-6F);
}

TEST_F(InputActionMapperTest, ComposesAnalogSourcesAcrossGamepadsInBothModes)
{
    const usize leftX = static_cast<usize>(Platform::GamepadAxis::LeftX);
    Platform::GamepadSnapshot first{
        .gamepad = gamepad_,
        .revision = 1,
    };
    first.axes[leftX] = 0.75F;
    Platform::GamepadSnapshot second{
        .gamepad = secondGamepad_,
        .revision = 1,
    };
    second.axes[leftX] = -0.25F;
    TestFrameInput frame;
    frame.gamepads = {first, second};
    frame.transitions = {
        Platform::GamepadAxisTransition{
            .routedWindow = window_,
            .gamepad = gamepad_,
            .axis = Platform::GamepadAxis::LeftX,
            .value = 0.75F,
        },
        Platform::GamepadAxisTransition{
            .routedWindow = window_,
            .gamepad = secondGamepad_,
            .axis = Platform::GamepadAxis::LeftX,
            .value = -0.25F,
        },
    };

    const std::array sumBindings{
        gamepadAxisBinding(Platform::GamepadAxis::LeftX, MoveAction),
    };
    auto sumMapper = createMapper(sumBindings);
    ASSERT_NE(sumMapper, nullptr);
    ASSERT_TRUE(mapTestFrame(*sumMapper, *frameBuilder_, window_, 1, 0, 0, frame).has_value());
    auto sum = sumMapper->simulationActionsForTick(0);
    ASSERT_TRUE(sum.has_value());
    EXPECT_NEAR(sum->value(MoveAction), 0.5F, 1.0e-6F);

    const std::array strongestBindings{
        gamepadAxisBinding(Platform::GamepadAxis::LeftX, MoveAction,
                           GamepadAxisValueMode::Signed, InputActionDomain::Simulation,
                           0.0F, 1.0F, ActionCompositionMode::StrongestMagnitude),
    };
    auto strongestMapper = createMapper(strongestBindings);
    ASSERT_NE(strongestMapper, nullptr);
    ASSERT_TRUE(mapTestFrame(*strongestMapper, *frameBuilder_, window_, 2, 0, 0, frame).has_value());
    auto strongest = strongestMapper->simulationActionsForTick(0);
    ASSERT_TRUE(strongest.has_value());
    EXPECT_NEAR(strongest->value(MoveAction), 0.75F, 1.0e-6F);
}

TEST_F(InputActionMapperTest, SumClampedAppliesClampAfterAllScaledSources)
{
    const std::array bindings{
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::A},
            .action = MoveAction,
            .scale = 2.0F,
        },
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::B},
            .action = MoveAction,
            .scale = -1.0F,
        },
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput frame;
    frame.heldKeys = {Platform::Key::A, Platform::Key::B};
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
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, frame).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FLOAT_EQ(snapshot->value(MoveAction), 1.0F);
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    ASSERT_NE(digital(snapshot->transitions.front()), nullptr);
    EXPECT_FLOAT_EQ(digital(snapshot->transitions.front())->value, 1.0F);
}

TEST_F(InputActionMapperTest, ClaimRebuildsOpposingSimulationSourcesAcrossZeroStepFrames)
{
    const std::array bindings{
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::A},
            .action = MoveAction,
            .scale = 1.0F,
        },
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::B},
            .action = MoveAction,
            .scale = -1.0F,
        },
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput opposingDown;
    opposingDown.heldKeys = {Platform::Key::A, Platform::Key::B};
    opposingDown.transitions = {
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
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, opposingDown).has_value());
    auto balanced = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(balanced.has_value());
    ASSERT_EQ(balanced->transitions.size(), 2U);
    EXPECT_FLOAT_EQ(balanced->value(MoveAction), 0.0F);
    expectLegalActionReplay(balanced->transitions, MoveAction, 0.0F,
                            balanced->value(MoveAction));

    TestFrameInput claimedPositive;
    claimedPositive.heldKeys = {Platform::Key::A, Platform::Key::B};
    claimedPositive.claims = {ContinuousControlClaim{
        .control = Platform::KeyControlIdentity{window_, Platform::Key::A},
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0,
                             claimedPositive).has_value());

    auto reconciled = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(reconciled.has_value());
    ASSERT_EQ(reconciled->transitions.size(), 1U);
    EXPECT_FLOAT_EQ(reconciled->value(MoveAction), -1.0F);
    expectLegalActionReplay(reconciled->transitions, MoveAction, 0.0F,
                            reconciled->value(MoveAction));
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
    EXPECT_EQ(digital(firstTick->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_EQ(digital(firstTick->transitions[1])->kind, InputActionTransitionKind::Completed);
    EXPECT_LT(digital(firstTick->transitions[0])->sourceSequence, digital(firstTick->transitions[1])->sourceSequence);
    EXPECT_FALSE(firstTick->isActive(MoveAction));

    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());
    for (u64 catchUpTick = 1; catchUpTick <= 3; ++catchUpTick)
    {
        auto catchUp = mapper->simulationActionsForTick(catchUpTick);
        ASSERT_TRUE(catchUp.has_value());
        EXPECT_TRUE(catchUp->transitions.empty());
        EXPECT_FALSE(catchUp->isActive(MoveAction));
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
    EXPECT_EQ(digital(firstFrame.transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(firstFrame.isActive(ExitAction));

    TestFrameInput held;
    held.heldKeys = {Platform::Key::Escape};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, held).has_value());
    FrameActionSnapshot secondFrame = mapper->frameActions();
    EXPECT_TRUE(secondFrame.transitions.empty());
    EXPECT_TRUE(secondFrame.isActive(ExitAction));
}

TEST_F(InputActionMapperTest, WheelTransitionsAccumulateIntoTheFrameSnapshot)
{
    const std::array bindings{keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    // Two notches in one frame. Summed rather than replaced: keeping only the last would
    // silently scale fast scrolling down to a single notch.
    TestFrameInput input;
    input.transitions = {
        Platform::PointerWheelTransition{.window = window_, .deltaX = 0.0, .deltaY = 1.5},
        Platform::PointerWheelTransition{.window = window_, .deltaX = -0.5, .deltaY = 2.0},
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, input).has_value());

    const FrameActionSnapshot frame = mapper->frameActions();
    EXPECT_DOUBLE_EQ(frame.wheelDeltaY, 3.5);
    EXPECT_DOUBLE_EQ(frame.wheelDeltaX, -0.5);
    ASSERT_FALSE(frame.pointers.empty());
    EXPECT_DOUBLE_EQ(frame.pointers[Platform::PrimaryPointerId].wheelDeltaY, 3.5);
}

TEST_F(InputActionMapperTest, WheelDoesNotSurviveIntoTheNextFrame)
{
    const std::array bindings{keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput scrolled;
    scrolled.transitions = {Platform::PointerWheelTransition{.window = window_, .deltaY = 2.0}};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, scrolled).has_value());
    ASSERT_DOUBLE_EQ(mapper->frameActions().wheelDeltaY, 2.0);

    // A wheel is a per-frame quantity with no resting value, so a frame with no wheel
    // transition must report zero rather than the previous frame's total.
    const TestFrameInput idle;
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, idle).has_value());
    EXPECT_DOUBLE_EQ(mapper->frameActions().wheelDeltaY, 0.0);
}

TEST_F(InputActionMapperTest, ConsumedWheelTransitionIsWithheldFromTheGame)
{
    const std::array bindings{keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    // The UI ate the first notch and left the second. Consumption is per transition, so the
    // game must see exactly the remainder, not all of it and not none of it.
    TestFrameInput input;
    input.transitions = {
        Platform::PointerWheelTransition{.window = window_, .deltaY = 1.0},
        Platform::PointerWheelTransition{.window = window_, .deltaY = 4.0},
    };
    input.consumedOrdinals = {0};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, input).has_value());

    EXPECT_DOUBLE_EQ(mapper->frameActions().wheelDeltaY, 4.0);
}

TEST_F(InputActionMapperTest, WheelClaimWithholdsWheelWithoutTouchingMotion)
{
    const std::array bindings{keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    Platform::PointerSnapshot moving{};
    moving.pointer = Platform::PrimaryPointerId;
    moving.present = true;
    moving.accumulatedDeltaX = 7.0;

    // Wheel and motion are separately claimable controls on the same pointer. A scrollable
    // widget claims the wheel; the camera underneath must keep its motion.
    TestFrameInput input;
    input.pointerOverrides = {moving};
    input.transitions = {Platform::PointerWheelTransition{.window = window_, .deltaY = 3.0}};
    input.claims = {ContinuousControlClaim{
        .control = Platform::PointerContinuousControlIdentity{
            .window = window_,
            .pointer = Platform::PrimaryPointerId,
            .control = Platform::PointerContinuousControl::Wheel,
        },
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, input).has_value());

    const FrameActionSnapshot frame = mapper->frameActions();
    EXPECT_DOUBLE_EQ(frame.wheelDeltaY, 0.0);
    EXPECT_DOUBLE_EQ(frame.pointerLookDeltaX, 7.0);
    EXPECT_DOUBLE_EQ(frame.pointers[Platform::PrimaryPointerId].deltaX, 7.0);
}

TEST_F(InputActionMapperTest, PointerTableReportsNonPrimaryPointers)
{
    const std::array bindings{keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    // A second finger. Before the pointer table existed this was invisible to a game: the
    // snapshot published only the primary pointer's look delta.
    Platform::PointerSnapshot second{};
    second.pointer = 1;
    second.present = true;
    second.logicalX = 120.0;
    second.logicalY = 240.0;
    second.accumulatedDeltaY = -4.0;
    second.heldButtons.set(static_cast<usize>(Platform::PointerButton::Primary));

    TestFrameInput input;
    input.pointerOverrides = {second};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, input).has_value());

    const FrameActionSnapshot frame = mapper->frameActions();
    ASSERT_EQ(frame.pointers.size(), Platform::PointerCapacity);
    const FramePointerState* state = frame.pointerState(1);
    ASSERT_NE(state, nullptr);
    // Slot identity is positional, so index and id must agree or a caller tracking a drag
    // would follow the wrong finger.
    EXPECT_EQ(state->pointer, 1U);
    EXPECT_TRUE(state->present);
    EXPECT_DOUBLE_EQ(state->logicalX, 120.0);
    EXPECT_DOUBLE_EQ(state->logicalY, 240.0);
    EXPECT_DOUBLE_EQ(state->deltaY, -4.0);
    EXPECT_TRUE(state->isHeld(Platform::PointerButton::Primary));

    // An untouched slot must be absent rather than inheriting the primary pointer's defaults.
    const FramePointerState* unused = frame.pointerState(5);
    ASSERT_NE(unused, nullptr);
    EXPECT_FALSE(unused->present);
    EXPECT_EQ(frame.pointerState(Platform::PointerCapacity), nullptr);
}

TEST_F(InputActionMapperTest, PointerDeltaClaimAppliesOnlyToTheClaimedPointer)
{
    const std::array bindings{keyBinding(Platform::Key::Escape, ExitAction, InputActionDomain::Frame)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    Platform::PointerSnapshot primary{};
    primary.pointer = Platform::PrimaryPointerId;
    primary.present = true;
    primary.accumulatedDeltaX = 5.0;
    Platform::PointerSnapshot second{};
    second.pointer = 1;
    second.present = true;
    second.accumulatedDeltaX = 9.0;

    // Claims carry a pointer id, so a widget owning one finger must not mute the other.
    TestFrameInput input;
    input.pointerOverrides = {primary, second};
    input.claims = {ContinuousControlClaim{
        .control = Platform::PointerContinuousControlIdentity{
            .window = window_,
            .pointer = 1,
            .control = Platform::PointerContinuousControl::Delta,
        },
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, input).has_value());

    const FrameActionSnapshot frame = mapper->frameActions();
    EXPECT_DOUBLE_EQ(frame.pointers[1].deltaX, 0.0);
    EXPECT_DOUBLE_EQ(frame.pointers[Platform::PrimaryPointerId].deltaX, 5.0);
    // The scalar look delta describes the primary pointer, so a claim on finger 1 leaves it.
    EXPECT_DOUBLE_EQ(frame.pointerLookDeltaX, 5.0);
}

TEST_F(InputActionMapperTest, SuppressedFrameSnapshotHasNoPointerTable)
{
    // A suppressed snapshot must not hand out a stale table: the documented contract is that
    // pointers is empty, and pointerState then returns null for every id.
    const FrameActionSnapshot suppressed = FrameActionSnapshot::suppressed(9);
    EXPECT_EQ(suppressed.engineFrameIndex, 9U);
    EXPECT_TRUE(suppressed.pointers.empty());
    EXPECT_EQ(suppressed.pointerState(Platform::PrimaryPointerId), nullptr);
    EXPECT_DOUBLE_EQ(suppressed.wheelDeltaX, 0.0);
    EXPECT_DOUBLE_EQ(suppressed.wheelDeltaY, 0.0);
}

TEST_F(InputActionMapperTest, RebindAndClaimRebuildOpposingFrameSourcesFromFrameBaseline)
{
    const std::array bindings{
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::A},
            .action = MoveAction,
            .domain = InputActionDomain::Frame,
            .scale = 1.0F,
        },
        InputActionBinding{
            .input = PrimaryWindowKeyBinding{Platform::Key::B},
            .action = MoveAction,
            .domain = InputActionDomain::Frame,
            .scale = -1.0F,
        },
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput opposingDown;
    opposingDown.heldKeys = {Platform::Key::A, Platform::Key::B};
    opposingDown.transitions = {
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
    };
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, opposingDown).has_value());
    const FrameActionSnapshot balanced = mapper->frameActions();
    ASSERT_EQ(balanced.transitions.size(), 2U);
    EXPECT_FLOAT_EQ(balanced.value(MoveAction), 0.0F);
    expectLegalActionReplay(balanced.transitions, MoveAction, 0.0F,
                            balanced.value(MoveAction));

    auto transaction = mapper->beginRebind(mapper->bindings().front().binding);
    ASSERT_TRUE(transaction.has_value());
    auto queued = mapper->commitRebind(*transaction,
                                       PrimaryWindowKeyBinding{Platform::Key::C},
                                       RebindConflictPolicy::Reject);
    ASSERT_TRUE(queued.has_value());
    ASSERT_EQ(queued->outcome, RebindCommitOutcome::Queued);

    TestFrameInput claimedNegative;
    claimedNegative.heldKeys = {Platform::Key::A, Platform::Key::B};
    claimedNegative.claims = {ContinuousControlClaim{
        .control = Platform::KeyControlIdentity{window_, Platform::Key::B},
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0,
                             claimedNegative).has_value());
    EXPECT_EQ(mapper->rebindState().state, RebindState::Applied);

    const FrameActionSnapshot reconciled = mapper->frameActions();
    EXPECT_TRUE(reconciled.transitions.empty());
    EXPECT_FLOAT_EQ(reconciled.value(MoveAction), 0.0F);
    expectLegalActionReplay(reconciled.transitions, MoveAction, 0.0F,
                            reconciled.value(MoveAction));
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
    EXPECT_FALSE(suppressed->isActive(MoveAction));

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
    EXPECT_EQ(digital(restored->transitions[0])->kind, InputActionTransitionKind::Started);
}

TEST_F(InputActionMapperTest, ConsumedAxisSuppressesUntilNeutralThenAllowsARealRestart)
{
    const std::array bindings{
        gamepadAxisBinding(Platform::GamepadAxis::LeftX, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);
    const usize leftX = static_cast<usize>(Platform::GamepadAxis::LeftX);

    Platform::GamepadSnapshot activeGamepad{
        .gamepad = gamepad_,
        .revision = 1,
    };
    activeGamepad.axes[leftX] = 0.75F;
    TestFrameInput consumed;
    consumed.gamepads = {activeGamepad};
    consumed.transitions = {Platform::GamepadAxisTransition{
        .routedWindow = window_,
        .gamepad = gamepad_,
        .axis = Platform::GamepadAxis::LeftX,
        .value = 0.75F,
    }};
    consumed.consumedOrdinals = {0};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, consumed).has_value());
    auto suppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(suppressed.has_value());
    EXPECT_FALSE(suppressed->isActive(MoveAction));
    EXPECT_TRUE(suppressed->transitions.empty());

    activeGamepad.revision = 2;
    TestFrameInput stillActive;
    stillActive.gamepads = {activeGamepad};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, stillActive).has_value());
    auto stillSuppressed = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(stillSuppressed.has_value());
    EXPECT_FALSE(stillSuppressed->isActive(MoveAction));

    Platform::GamepadSnapshot neutralGamepad{
        .gamepad = gamepad_,
        .revision = 3,
    };
    TestFrameInput neutral;
    neutral.gamepads = {neutralGamepad};
    neutral.transitions = {Platform::GamepadAxisTransition{
        .routedWindow = window_,
        .gamepad = gamepad_,
        .axis = Platform::GamepadAxis::LeftX,
        .value = 0.0F,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 3, 2, 0, neutral).has_value());

    activeGamepad.revision = 4;
    TestFrameInput restarted;
    restarted.gamepads = {activeGamepad};
    restarted.transitions = {Platform::GamepadAxisTransition{
        .routedWindow = window_,
        .gamepad = gamepad_,
        .axis = Platform::GamepadAxis::LeftX,
        .value = 0.75F,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 4, 3, 0, restarted).has_value());
    auto restored = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->transitions.size(), 1U);
    ASSERT_NE(digital(restored->transitions.front()), nullptr);
    EXPECT_EQ(digital(restored->transitions.front())->kind, InputActionTransitionKind::Started);
    EXPECT_FLOAT_EQ(restored->value(MoveAction), 0.75F);
}

TEST_F(InputActionMapperTest, ClaimedAxisCancelsDeliveredValueAndRemainsSuppressed)
{
    const std::array bindings{
        gamepadAxisBinding(Platform::GamepadAxis::LeftX, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);
    const usize leftX = static_cast<usize>(Platform::GamepadAxis::LeftX);

    Platform::GamepadSnapshot gamepad{
        .gamepad = gamepad_,
        .revision = 1,
    };
    gamepad.axes[leftX] = 0.6F;
    TestFrameInput activeFrame;
    activeFrame.gamepads = {gamepad};
    activeFrame.transitions = {Platform::GamepadAxisTransition{
        .routedWindow = window_,
        .gamepad = gamepad_,
        .axis = Platform::GamepadAxis::LeftX,
        .value = 0.6F,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, activeFrame).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    gamepad.revision = 2;
    TestFrameInput claimed;
    claimed.gamepads = {gamepad};
    claimed.claims = {ContinuousControlClaim{
        .control = Platform::GamepadAxisControlIdentity{
            .routedWindow = window_,
            .gamepad = gamepad_,
            .axis = Platform::GamepadAxis::LeftX,
        },
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 1, claimed).has_value());
    auto cancelled = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(cancelled->transitions.size(), 1U);
    ASSERT_NE(digital(cancelled->transitions.front()), nullptr);
    EXPECT_EQ(digital(cancelled->transitions.front())->kind, InputActionTransitionKind::Cancelled);
    EXPECT_FLOAT_EQ(digital(cancelled->transitions.front())->value, 0.0F);
    EXPECT_FALSE(cancelled->isActive(MoveAction));

    gamepad.revision = 3;
    TestFrameInput stillSuppressed;
    stillSuppressed.gamepads = {gamepad};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 3, 2, 1, stillSuppressed).has_value());
    auto unchanged = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_EQ(unchanged->transitions.size(), 1U);
    EXPECT_FALSE(unchanged->isActive(MoveAction));
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
    EXPECT_FALSE(snapshot->isActive(MoveAction));
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
    EXPECT_EQ(digital(cancelled->transitions[0])->kind, InputActionTransitionKind::Cancelled);
    EXPECT_FALSE(cancelled->isActive(MoveAction));
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
    EXPECT_EQ(digital(cancelled->transitions[0])->kind, InputActionTransitionKind::Cancelled);
}

TEST_F(InputActionMapperTest, RawResetKeepsMarkerAsOnlyPendingTransitionUntilTickCompletion)
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
    EXPECT_FALSE(resetSnapshot->isActive(MoveAction));

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
    ASSERT_EQ(recovered->transitions.size(), 1U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&recovered->transitions[0]), nullptr);
    EXPECT_FALSE(recovered->isActive(MoveAction));
}

TEST_F(InputActionMapperTest, AcceptsRawCapacityResetInReservedSlot)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
    };
    InputActionMapperCapacityConfig mapperCapacities;
    mapperCapacities.rawInputTransitionCapacity = 1;
    auto mapper = createMapper(bindings, {}, mapperCapacities);
    ASSERT_NE(mapper, nullptr);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto builder = std::move(*builderResult);

    TestFrameInput overflow;
    overflow.heldKeys = {Platform::Key::A, Platform::Key::B};
    overflow.transitions = {
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
    };
    ASSERT_TRUE(mapTestFrame(*mapper, builder, window_, 1, 0, 0, overflow).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    const auto* reset = std::get_if<SimulationInputStreamReset>(&snapshot->transitions.front());
    ASSERT_NE(reset, nullptr);
    EXPECT_EQ(reset->reason, ActionInputStreamResetReason::RawInputStreamReset);
    EXPECT_FALSE(snapshot->isActive(MoveAction));
    EXPECT_FALSE(snapshot->isActive(JumpAction));
    EXPECT_EQ(mapper->statistics().rawInputResetCount, 1U);
}

TEST_F(InputActionMapperTest, SimulationOverflowKeepsResetAsOnlyPendingTransition)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
    };
    InputActionMapCapacityConfig capacities;
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
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&snapshot->transitions[0]), nullptr);
    EXPECT_FALSE(snapshot->isActive(MoveAction));
    EXPECT_FALSE(snapshot->isActive(JumpAction));
    EXPECT_EQ(mapper->simulationLatchStatistics().capacityResetCount, 1U);
}

TEST_F(InputActionMapperTest, SimulationOverflowSuppressesLaterDownInSameRawBatch)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
        keyBinding(Platform::Key::C, ExitAction),
    };
    InputActionMapCapacityConfig capacities;
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
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    EXPECT_NE(std::get_if<SimulationInputStreamReset>(&snapshot->transitions[0]), nullptr);
    EXPECT_FALSE(snapshot->isActive(MoveAction));
    EXPECT_FALSE(snapshot->isActive(JumpAction));
    EXPECT_FALSE(snapshot->isActive(ExitAction));
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
    EXPECT_EQ(digital(snapshot->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_EQ(digital(snapshot->transitions[1])->kind, InputActionTransitionKind::Completed);
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
    EXPECT_EQ(digital(snapshot->transitions[0])->kind, InputActionTransitionKind::Cancelled);
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
    EXPECT_FALSE(snapshot->isActive(MoveAction));
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
    EXPECT_EQ(digital(pending->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(pending->isActive(MoveAction));
}

TEST_F(InputActionMapperTest, RebindRejectReportsConflictAndSwapAppliesOnNextMappingFrame)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        keyBinding(Platform::Key::B, JumpAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);
    ASSERT_EQ(mapper->bindings().size(), 2U);
    const InputBindingId moveBinding = mapper->bindings()[0].binding;
    const InputBindingId jumpBinding = mapper->bindings()[1].binding;
    ASSERT_TRUE(moveBinding.hasValue());
    ASSERT_TRUE(jumpBinding.hasValue());
    ASSERT_NE(moveBinding, jumpBinding);

    auto transaction = mapper->beginRebind(moveBinding);
    ASSERT_TRUE(transaction.has_value());
    EXPECT_EQ(mapper->rebindState().state, RebindState::Capturing);
    auto conflict = mapper->commitRebind(
        *transaction, PrimaryWindowKeyBinding{Platform::Key::B},
        RebindConflictPolicy::Reject);
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(conflict->outcome, RebindCommitOutcome::Conflict);
    ASSERT_TRUE(conflict->conflictingBinding.has_value());
    EXPECT_EQ(*conflict->conflictingBinding, jumpBinding);
    EXPECT_EQ(mapper->rebindState().state, RebindState::Capturing);

    auto queued = mapper->commitRebind(
        *transaction, PrimaryWindowKeyBinding{Platform::Key::B},
        RebindConflictPolicy::Swap);
    ASSERT_TRUE(queued.has_value());
    EXPECT_EQ(queued->outcome, RebindCommitOutcome::Queued);
    EXPECT_EQ(mapper->rebindState().state, RebindState::Queued);
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>(mapper->bindings()[0].input).key,
              Platform::Key::A);
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>(mapper->bindings()[1].input).key,
              Platform::Key::B);

    const TestFrameInput emptyFrame;
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, emptyFrame).has_value());
    EXPECT_EQ(mapper->rebindState().state, RebindState::Applied);
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>(mapper->bindings()[0].input).key,
              Platform::Key::B);
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>(mapper->bindings()[1].input).key,
              Platform::Key::A);
    EXPECT_EQ(mapper->statistics().rebindConflictCount, 1U);
    EXPECT_EQ(mapper->statistics().rebindApplyCount, 1U);
}

TEST_F(InputActionMapperTest, RebindRejectReportsConflictBeforeSwapTransformCompatibility)
{
    const std::array bindings{
        keyBinding(Platform::Key::A, MoveAction),
        gamepadAxisBinding(Platform::GamepadAxis::LeftX, JumpAction,
                           GamepadAxisValueMode::Signed, InputActionDomain::Simulation,
                           0.2F),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    auto transaction = mapper->beginRebind(mapper->bindings()[0].binding);
    ASSERT_TRUE(transaction.has_value());
    const StandardGamepadAxisBinding replacement{
        .axis = Platform::GamepadAxis::LeftX,
        .valueMode = GamepadAxisValueMode::Signed,
    };
    auto rejected = mapper->commitRebind(*transaction, replacement, RebindConflictPolicy::Reject);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected->outcome, RebindCommitOutcome::Conflict);

    auto incompatibleSwap = mapper->commitRebind(*transaction, replacement, RebindConflictPolicy::Swap);
    ASSERT_FALSE(incompatibleSwap.has_value());
    EXPECT_EQ(incompatibleSwap.error().code, RuntimeErrorCode::InvalidRebindTransaction);
    EXPECT_EQ(mapper->rebindState().state, RebindState::Capturing);
    EXPECT_TRUE(mapper->cancelRebind(*transaction).has_value());
}

TEST_F(InputActionMapperTest, QueuedRebindCanBeCancelledBeforeNextMappingFrame)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);
    const InputBindingId binding = mapper->bindings().front().binding;

    auto transaction = mapper->beginRebind(binding);
    ASSERT_TRUE(transaction.has_value());
    auto queued = mapper->commitRebind(
        *transaction, PrimaryWindowKeyBinding{Platform::Key::C},
        RebindConflictPolicy::Reject);
    ASSERT_TRUE(queued.has_value());
    ASSERT_EQ(queued->outcome, RebindCommitOutcome::Queued);
    ASSERT_TRUE(mapper->cancelRebind(*transaction).has_value());
    EXPECT_EQ(mapper->rebindState().state, RebindState::Cancelled);

    const TestFrameInput emptyFrame;
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, emptyFrame).has_value());
    EXPECT_EQ(std::get<PrimaryWindowKeyBinding>(mapper->bindings().front().input).key,
              Platform::Key::A);
    EXPECT_EQ(mapper->statistics().rebindApplyCount, 0U);
}

TEST_F(InputActionMapperTest, ExplicitGamepadDisconnectCancelsRebindCapture)
{
    const std::array bindings{
        gamepadBinding(Platform::GamepadButton::South, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput connected;
    connected.gamepads = {Platform::GamepadSnapshot{
        .gamepad = gamepad_,
        .revision = 1,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, connected).has_value());
    auto transaction = mapper->beginRebind(mapper->bindings().front().binding, gamepad_);
    ASSERT_TRUE(transaction.has_value());

    TestFrameInput disconnected;
    disconnected.transitions = {Platform::InputCancelTransition{
        .routedWindow = window_,
        .reason = Platform::InputCancelReason::DeviceDisconnected,
        .gamepad = gamepad_,
    }};
    disconnected.platformEvents = {Platform::GamepadDisconnectedEvent{
        .gamepad = gamepad_,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, disconnected).has_value());
    EXPECT_EQ(mapper->rebindState().state, RebindState::DeviceDisconnected);
    EXPECT_EQ(mapper->rebindState().transaction, *transaction);
    EXPECT_EQ(mapper->statistics().rebindDeviceCancellationCount, 1U);
}

TEST_F(InputActionMapperTest, RawResetCancelsRebindWhenCapturedGenerationDisappears)
{
    const std::array bindings{
        gamepadBinding(Platform::GamepadButton::South, MoveAction),
    };
    auto mapper = createMapper(bindings);
    ASSERT_NE(mapper, nullptr);

    TestFrameInput connected;
    connected.gamepads = {Platform::GamepadSnapshot{
        .gamepad = gamepad_,
        .revision = 1,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 1, 0, 0, connected).has_value());
    auto transaction = mapper->beginRebind(mapper->bindings().front().binding, gamepad_);
    ASSERT_TRUE(transaction.has_value());

    TestFrameInput reset;
    reset.transitions = {Platform::InputStreamReset{
        .routedWindow = window_,
        .reason = Platform::InputResetReason::CapacityExceeded,
    }};
    ASSERT_TRUE(mapTestFrame(*mapper, *frameBuilder_, window_, 2, 1, 0, reset).has_value());
    EXPECT_EQ(mapper->rebindState().state, RebindState::DeviceDisconnected);
    EXPECT_EQ(mapper->rebindState().transaction, *transaction);
    EXPECT_EQ(mapper->statistics().rebindDeviceCancellationCount, 1U);
}

TEST_F(InputActionMapperTest, RejectsUndersizedNonEmptyUiConsumptionBitset)
{
    const std::array bindings{keyBinding(Platform::Key::A, MoveAction)};
    InputActionMapperCapacityConfig mapperCapacities;
    mapperCapacities.rawInputTransitionCapacity = 65;
    auto mapper = createMapper(bindings, {}, mapperCapacities);
    ASSERT_NE(mapper, nullptr);

    auto builderResult = Platform::PlatformFrameBuilder::Create({
        .inputTransitionCapacity = 65,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto builder = std::move(*builderResult);
    const Platform::PlatformFrameId frameId{1};
    ASSERT_TRUE(builder.beginFrame(frameId).has_value());
    const Platform::WindowMetricsSnapshot metrics{
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
    input.heldKeys.set(static_cast<usize>(Platform::Key::A));
    ASSERT_TRUE(builder.setPrimaryWindowSnapshot(metrics, input));
    ASSERT_TRUE(builder.setGamepadSnapshots({}));
    for (usize ordinal = 0; ordinal < 65; ++ordinal)
    {
        const auto result = builder.appendInputTransition(Platform::KeyTransition{
            .window = window_,
            .key = Platform::Key::A,
            .state = ordinal % 2U == 0U ? Platform::DigitalTransition::Down
                                        : Platform::DigitalTransition::Up,
        });
        ASSERT_EQ(result, Platform::FrameBatchAppendResult::Appended);
    }
    auto frame = builder.finishFrame();
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->inputTransitions().size(), 65U);

    const std::array<u64, 1> tooShort{};
    const InputTransitionConsumptionView consumption{
        .platformFrame = frameId,
        .transitionCount = 65,
        .consumedOrdinalWords = tooShort,
    };
    const auto claims = ContinuousControlClaimsView::None(frameId);
    const Core::Status status = mapper->mapFrame(*frame, consumption, claims, 0, 0);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
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
    EXPECT_EQ(digital(original->transitions[0])->kind, InputActionTransitionKind::Started);
    EXPECT_TRUE(original->isActive(MoveAction));
}

TEST(SimulationActionLatchTest, RejectsWrongTargetBeforeChangingPendingBatch)
{
    const std::array actions{MoveAction};
    auto latchResult = Runtime::Input::SimulationActionLatch::Create(actions, 4);
    ASSERT_TRUE(latchResult.has_value());
    auto latch = std::move(*latchResult);
    ASSERT_TRUE(latch.setValue(MoveAction, 1.0F).has_value());
    ASSERT_TRUE(latch
                    .append(5,
                            InputActionTransition{
                                .action = MoveAction,
                                .kind = InputActionTransitionKind::Started,
                                .value = 1.0F,
                                .sourceSequence = 1,
                            })
                    .has_value());

    const std::array<Runtime::Input::ActionSourceToken, 1> sources{0};
    auto wrongTarget = latch.reconcileCancellation(6, MoveAction, sources, 2, true);
    ASSERT_FALSE(wrongTarget.has_value());
    auto original = latch.snapshotForTick(5);
    ASSERT_TRUE(original.has_value());
    ASSERT_EQ(original->transitions.size(), 1U);
    ASSERT_NE(digital(original->transitions[0]), nullptr);
    EXPECT_EQ(digital(original->transitions[0])->kind, InputActionTransitionKind::Started);
}

TEST(SimulationActionLatchTest, ReconciliationTransitionDoesNotRetainCancelledSource)
{
    const std::array actions{MoveAction};
    auto latchResult = Runtime::Input::SimulationActionLatch::Create(actions, 4);
    ASSERT_TRUE(latchResult.has_value());
    auto latch = std::move(*latchResult);
    ASSERT_TRUE(latch.setValue(MoveAction, 1.0F).has_value());
    ASSERT_TRUE(latch
                    .append(5,
                            InputActionTransition{
                                .action = MoveAction,
                                .kind = InputActionTransitionKind::Started,
                                .value = 1.0F,
                                .sourceSequence = 1,
                            },
                            7)
                    .has_value());

    const std::array<Runtime::Input::ActionSourceToken, 1> sources{7};
    auto rebuilt = latch.reconcileCancellation(5, MoveAction, sources, 2, true);
    ASSERT_TRUE(rebuilt.has_value());
    EXPECT_EQ(*rebuilt, Runtime::Input::SimulationLatchAppendResult::Appended);

    auto repeated = latch.reconcileCancellation(5, MoveAction, sources, 3, false);
    ASSERT_TRUE(repeated.has_value());
    EXPECT_EQ(*repeated, Runtime::Input::SimulationLatchAppendResult::NoTransitionNeeded);

    auto snapshot = latch.snapshotForTick(5);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    ASSERT_NE(digital(snapshot->transitions.front()), nullptr);
    EXPECT_EQ(digital(snapshot->transitions.front())->sourceSequence, 2U);
    expectLegalActionReplay(snapshot->transitions, MoveAction, 0.0F,
                            snapshot->value(MoveAction));
}

} // namespace
} // namespace Tina::Tests
