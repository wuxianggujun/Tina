#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/InputRouting.hpp>

#include "../../../src/runtime/input/ActionMapper.hpp"
#include "../../../src/runtime/input/LastPresentedCamera2DLatch.hpp"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace Tina::Tests {
namespace {

using Runtime::Input::ActionMapper;
using Runtime::Input::LastPresentedCamera2DLatch;
using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

inline constexpr InputActionId SelectAction{77};

struct PointerFrameSpec final {
    Platform::PlatformFrameId frameId{1};
    u32 logicalWidth = 100;
    u32 logicalHeight = 100;
    double finalPointerX = 50.0;
    double finalPointerY = 50.0;
    std::vector<Platform::InputTransitionPayload> transitions{};
    std::vector<Platform::PointerButton> heldPointerButtons{};
    std::vector<usize> consumedOrdinals{};
    std::vector<UI::ContinuousControlClaim> claims{};
};

[[nodiscard]] DigitalActionBinding primaryPointerBinding() noexcept
{
    return DigitalActionBinding{
        .input =
            PrimaryPointerButtonBinding{
                .pointer = Platform::PrimaryPointerId,
                .button = Platform::PointerButton::Primary,
            },
        .action = SelectAction,
        .domain = InputActionDomain::Simulation,
    };
}

[[nodiscard]] Platform::PointerButtonTransition pointerDown(Platform::WindowId window, double x, double y) noexcept
{
    return Platform::PointerButtonTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .button = Platform::PointerButton::Primary,
        .state = Platform::DigitalTransition::Down,
        .logicalX = x,
        .logicalY = y,
    };
}

[[nodiscard]] Platform::PointerButtonTransition pointerUp(Platform::WindowId window, double x, double y) noexcept
{
    return Platform::PointerButtonTransition{
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .button = Platform::PointerButton::Primary,
        .state = Platform::DigitalTransition::Up,
        .logicalX = x,
        .logicalY = y,
    };
}

[[nodiscard]] Render::RenderCamera2DInput camera(float centerX = 0.0F, float centerY = 0.0F) noexcept
{
    return Render::RenderCamera2DInput{
        .stableCameraKey = 9001,
        .centerX = centerX,
        .centerY = centerY,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .actualPixelsPerMeter = 32.0F,
    };
}

[[nodiscard]] const DigitalActionTransition* digital(const SimulationActionTransition& transition) noexcept
{
    return std::get_if<DigitalActionTransition>(&transition);
}

[[nodiscard]] Core::Status mapPointerFrame(ActionMapper& mapper, Platform::PlatformFrameBuilder& builder,
                                           Platform::WindowId window, const PointerFrameSpec& spec,
                                           u64 engineFrameIndex, u64 nextSimulationTick,
                                           const LastPresentedCamera2DLatch* latch)
{
    if (auto status = builder.beginFrame(spec.frameId); !status)
    {
        return status;
    }

    const Platform::WindowMetricsSnapshot metrics{
        .window = window,
        .logicalExtent = {spec.logicalWidth, spec.logicalHeight},
        .framebufferExtent = {spec.logicalWidth, spec.logicalHeight},
        .contentScale = {1.0F, 1.0F},
        .revision = spec.frameId.value,
        .focused = true,
        .visible = true,
    };
    Platform::WindowInputSnapshot input{
        .window = window,
        .sourceMetricsRevision = spec.frameId.value,
    };
    input.pointer.logicalX = spec.finalPointerX;
    input.pointer.logicalY = spec.finalPointerY;
    for (Platform::PointerButton button : spec.heldPointerButtons)
    {
        input.pointer.heldButtons.set(static_cast<usize>(button));
    }
    if (!builder.setPrimaryWindowSnapshot(metrics, input) || !builder.setGamepadSnapshots({}))
    {
        return Core::failure(Core::CoreErrorCode::Internal, "test frame snapshot was rejected");
    }

    for (const Platform::InputTransitionPayload& transition : spec.transitions)
    {
        const Platform::FrameBatchAppendResult result = builder.appendInputTransition(transition);
        if (result != Platform::FrameBatchAppendResult::Appended &&
            result != Platform::FrameBatchAppendResult::Coalesced &&
            result != Platform::FrameBatchAppendResult::ResetInserted)
        {
            return Core::failure(Core::CoreErrorCode::Internal, "test pointer transition append failed");
        }
    }

    auto frame = builder.finishFrame();
    if (!frame)
    {
        return Core::failure(std::move(frame.error()));
    }

    std::vector<u64> consumedWords;
    UI::InputTransitionConsumptionView consumption =
        UI::InputTransitionConsumptionView::None(spec.frameId, spec.transitions.size());
    if (!spec.consumedOrdinals.empty())
    {
        constexpr usize BitsPerWord = sizeof(u64) * 8U;
        consumedWords.resize((spec.transitions.size() + BitsPerWord - 1U) / BitsPerWord);
        for (usize ordinal : spec.consumedOrdinals)
        {
            if (ordinal >= spec.transitions.size())
            {
                return Core::failure(Core::CoreErrorCode::InvalidArgument, "test consumed ordinal is out of range");
            }
            consumedWords[ordinal / BitsPerWord] |= u64{1} << (ordinal % BitsPerWord);
        }
        consumption.consumedOrdinalWords = consumedWords;
    }

    const UI::ContinuousControlClaimsView claims{
        .platformFrame = spec.frameId,
        .controls = spec.claims,
    };
    return mapper.mapFrame(*frame, consumption, claims, engineFrameIndex, nextSimulationTick, latch);
}

[[nodiscard]] std::optional<u64> notePresentedCamera(LastPresentedCamera2DLatch& latch,
                                                     const Render::RenderCamera2DInput& cameraInput,
                                                     u64 surfaceRevision)
{
    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    if (!builder)
    {
        ADD_FAILURE() << builder.error().message;
        return std::nullopt;
    }
    if (auto status = builder->beginFrame(); !status)
    {
        ADD_FAILURE() << status.error().message;
        return std::nullopt;
    }
    if (auto status = builder->writer().setCamera2D(cameraInput); !status)
    {
        ADD_FAILURE() << status.error().message;
        return std::nullopt;
    }
    auto scene = builder->commit();
    if (!scene)
    {
        ADD_FAILURE() << scene.error().message;
        return std::nullopt;
    }

    latch.notePresented(*scene, surfaceRevision);
    return latch.cameraRevision();
}

class WorldPointerActionMappingTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto pool = WindowPool::Create(1);
        ASSERT_TRUE(pool.has_value()) << (pool ? "" : pool.error().message);
        windows = std::make_unique<WindowPool>(std::move(*pool));

        auto createdWindow = windows->tryEmplace(1);
        ASSERT_TRUE(createdWindow.has_value()) << (createdWindow ? "" : createdWindow.error().message);
        window = *createdWindow;

        auto frameBuilder = Platform::PlatformFrameBuilder::Create({
            .inputTransitionCapacity = 8,
            .platformEventCapacity = 1,
        });
        ASSERT_TRUE(frameBuilder.has_value()) << (frameBuilder ? "" : frameBuilder.error().message);
        builder = std::make_unique<Platform::PlatformFrameBuilder>(std::move(*frameBuilder));
    }

    [[nodiscard]] std::unique_ptr<ActionMapper> createMapper()
    {
        const std::array bindings{primaryPointerBinding()};
        auto mapper = ActionMapper::Create(bindings);
        EXPECT_TRUE(mapper.has_value()) << (mapper ? "" : mapper.error().message);
        return mapper ? std::move(*mapper) : nullptr;
    }

    std::unique_ptr<WindowPool> windows;
    std::unique_ptr<Platform::PlatformFrameBuilder> builder;
    Platform::WindowId window{};
};

TEST_F(WorldPointerActionMappingTest, ConsumedPointerTransitionDoesNotRequirePresentedCamera)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    const LastPresentedCamera2DLatch emptyLatch;

    const PointerFrameSpec frame{
        .frameId = {1},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .consumedOrdinals = {0},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, frame, 0, 0, &emptyLatch).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value()) << (snapshot ? "" : snapshot.error().message);
    EXPECT_TRUE(snapshot->transitions.empty());
    EXPECT_FALSE(snapshot->isHeld(SelectAction));
}

TEST_F(WorldPointerActionMappingTest, ClaimedPointerTransitionDoesNotRequirePresentedCamera)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    const LastPresentedCamera2DLatch emptyLatch;

    const PointerFrameSpec frame{
        .frameId = {1},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
        .claims =
            {UI::ContinuousControlClaim{
                .control = Platform::PointerButtonControlIdentity{
                    .window = window,
                    .pointer = Platform::PrimaryPointerId,
                    .button = Platform::PointerButton::Primary,
                },
            }},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, frame, 0, 0, &emptyLatch).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value()) << (snapshot ? "" : snapshot.error().message);
    EXPECT_TRUE(snapshot->transitions.empty());
    EXPECT_FALSE(snapshot->isHeld(SelectAction));
}

TEST_F(WorldPointerActionMappingTest, UnconsumedPointerTransitionFailsWithoutPresentedCamera)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    const LastPresentedCamera2DLatch emptyLatch;

    const PointerFrameSpec frame{
        .frameId = {1},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    const Core::Status status = mapPointerFrame(*mapper, *builder, window, frame, 0, 0, &emptyLatch);

    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::LifecycleInvariantViolation);
}

TEST_F(WorldPointerActionMappingTest, FailedWorldPickLeavesPressSourceRetryable)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    LastPresentedCamera2DLatch latch;

    const PointerFrameSpec failedFrame{
        .frameId = {1},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    auto failed = mapPointerFrame(*mapper, *builder, window, failedFrame, 0, 0, &latch);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    auto unchanged = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(unchanged.has_value()) << (unchanged ? "" : unchanged.error().message);
    EXPECT_TRUE(unchanged->transitions.empty());
    EXPECT_FALSE(unchanged->isHeld(SelectAction));

    ASSERT_TRUE(notePresentedCamera(latch, camera(), 9).has_value());
    const PointerFrameSpec retryFrame{
        .frameId = {2},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, retryFrame, 0, 0, &latch).has_value());

    auto retried = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(retried.has_value()) << (retried ? "" : retried.error().message);
    ASSERT_EQ(retried->transitions.size(), 1U);
    const DigitalActionTransition* transition = digital(retried->transitions.front());
    ASSERT_NE(transition, nullptr);
    EXPECT_EQ(transition->kind, DigitalActionTransitionKind::Pressed);
    EXPECT_TRUE(transition->worldPointerSample.has_value());
    EXPECT_TRUE(retried->isHeld(SelectAction));
}

TEST_F(WorldPointerActionMappingTest, UnconsumedPointerTransitionLocksWorldSampleFromEventCoordinates)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    LastPresentedCamera2DLatch latch;
    const std::optional<u64> cameraRevision = notePresentedCamera(latch, camera(), 42);
    ASSERT_TRUE(cameraRevision.has_value());

    const PointerFrameSpec frame{
        .frameId = {1},
        .finalPointerX = 50.0,
        .finalPointerY = 50.0,
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, frame, 0, 0, &latch).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value()) << (snapshot ? "" : snapshot.error().message);
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    const DigitalActionTransition* transition = digital(snapshot->transitions[0]);
    ASSERT_NE(transition, nullptr);
    ASSERT_TRUE(transition->worldPointerSample.has_value());

    const Render::WorldPointerSample& sample = *transition->worldPointerSample;
    EXPECT_TRUE(sample.hit);
    EXPECT_FLOAT_EQ(sample.worldX, -2.5F);
    EXPECT_FLOAT_EQ(sample.worldY, -2.5F);
    EXPECT_EQ(sample.cameraRevision, *cameraRevision);
    EXPECT_EQ(sample.surfaceRevision, 42U);
    EXPECT_EQ(sample.inputSequence, transition->sourceSequence);
    EXPECT_EQ(sample.inputSequence, 1U);
    EXPECT_EQ(sample.stableCameraKey, 9001U);
}

TEST_F(WorldPointerActionMappingTest, ViewportMissProducesLockedNoHitSample)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    LastPresentedCamera2DLatch latch;
    Render::RenderCamera2DInput narrowCamera = camera();
    narrowCamera.normalizedViewport = {.x = 0.25F, .y = 0.25F, .width = 0.5F, .height = 0.5F};
    const std::optional<u64> cameraRevision = notePresentedCamera(latch, narrowCamera, 5);
    ASSERT_TRUE(cameraRevision.has_value());

    const PointerFrameSpec frame{
        .frameId = {1},
        .transitions = {pointerDown(window, 5.0, 5.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, frame, 0, 0, &latch).has_value());

    auto snapshot = mapper->simulationActionsForTick(0);
    ASSERT_TRUE(snapshot.has_value()) << (snapshot ? "" : snapshot.error().message);
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    const DigitalActionTransition* transition = digital(snapshot->transitions[0]);
    ASSERT_NE(transition, nullptr);
    ASSERT_TRUE(transition->worldPointerSample.has_value());

    const Render::WorldPointerSample& sample = *transition->worldPointerSample;
    EXPECT_FALSE(sample.hit);
    EXPECT_EQ(sample.cameraRevision, *cameraRevision);
    EXPECT_EQ(sample.surfaceRevision, 5U);
    EXPECT_EQ(sample.inputSequence, transition->sourceSequence);
}

TEST_F(WorldPointerActionMappingTest, UnconsumedReleaseLocksWorldSample)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    LastPresentedCamera2DLatch latch;
    ASSERT_TRUE(notePresentedCamera(latch, camera(), 12).has_value());

    const PointerFrameSpec downFrame{
        .frameId = {1},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, downFrame, 0, 0, &latch).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    const PointerFrameSpec upFrame{
        .frameId = {2},
        .transitions = {pointerUp(window, 75.0, 25.0)},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, upFrame, 1, 1, &latch).has_value());

    auto snapshot = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(snapshot.has_value()) << (snapshot ? "" : snapshot.error().message);
    ASSERT_EQ(snapshot->transitions.size(), 1U);
    const DigitalActionTransition* transition = digital(snapshot->transitions.front());
    ASSERT_NE(transition, nullptr);
    EXPECT_EQ(transition->kind, DigitalActionTransitionKind::Released);
    ASSERT_TRUE(transition->worldPointerSample.has_value());
    EXPECT_TRUE(transition->worldPointerSample->hit);
    EXPECT_FLOAT_EQ(transition->worldPointerSample->worldX, 2.5F);
    EXPECT_FLOAT_EQ(transition->worldPointerSample->worldY, 2.5F);
    EXPECT_EQ(transition->worldPointerSample->surfaceRevision, 12U);
    EXPECT_EQ(transition->worldPointerSample->inputSequence, transition->sourceSequence);
    EXPECT_FALSE(snapshot->isHeld(SelectAction));
}

TEST_F(WorldPointerActionMappingTest, FailedWorldPickLeavesReleaseSourceRetryable)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    LastPresentedCamera2DLatch latch;
    ASSERT_TRUE(notePresentedCamera(latch, camera(), 20).has_value());

    const PointerFrameSpec downFrame{
        .frameId = {1},
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, downFrame, 0, 0, &latch).has_value());
    ASSERT_TRUE(mapper->completeSimulationTick(0).has_value());

    latch.clear();
    const PointerFrameSpec failedUpFrame{
        .frameId = {2},
        .transitions = {pointerUp(window, 75.0, 25.0)},
    };
    auto failed = mapPointerFrame(*mapper, *builder, window, failedUpFrame, 1, 1, &latch);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, RuntimeErrorCode::LifecycleInvariantViolation);

    auto unchanged = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(unchanged.has_value()) << (unchanged ? "" : unchanged.error().message);
    EXPECT_TRUE(unchanged->transitions.empty());
    EXPECT_TRUE(unchanged->isHeld(SelectAction));

    ASSERT_TRUE(notePresentedCamera(latch, camera(), 21).has_value());
    const PointerFrameSpec retryUpFrame{
        .frameId = {3},
        .transitions = {pointerUp(window, 75.0, 25.0)},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, retryUpFrame, 1, 1, &latch).has_value());

    auto retried = mapper->simulationActionsForTick(1);
    ASSERT_TRUE(retried.has_value()) << (retried ? "" : retried.error().message);
    ASSERT_EQ(retried->transitions.size(), 1U);
    const DigitalActionTransition* transition = digital(retried->transitions.front());
    ASSERT_NE(transition, nullptr);
    EXPECT_EQ(transition->kind, DigitalActionTransitionKind::Released);
    EXPECT_TRUE(transition->worldPointerSample.has_value());
    EXPECT_FALSE(retried->isHeld(SelectAction));
}

TEST_F(WorldPointerActionMappingTest, ZeroStepFrameKeepsLockedSampleAcrossLaterCameraAndResize)
{
    auto mapper = createMapper();
    ASSERT_NE(mapper, nullptr);
    LastPresentedCamera2DLatch latch;
    const std::optional<u64> firstCameraRevision = notePresentedCamera(latch, camera(), 10);
    ASSERT_TRUE(firstCameraRevision.has_value());

    const PointerFrameSpec firstFrame{
        .frameId = {1},
        .logicalWidth = 100,
        .logicalHeight = 100,
        .finalPointerX = 50.0,
        .finalPointerY = 50.0,
        .transitions = {pointerDown(window, 25.0, 75.0)},
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, firstFrame, 0, 7, &latch).has_value());

    auto pendingBefore = mapper->simulationActionsForTick(7);
    ASSERT_TRUE(pendingBefore.has_value()) << (pendingBefore ? "" : pendingBefore.error().message);
    ASSERT_EQ(pendingBefore->transitions.size(), 1U);
    const DigitalActionTransition* firstTransition = digital(pendingBefore->transitions[0]);
    ASSERT_NE(firstTransition, nullptr);
    ASSERT_TRUE(firstTransition->worldPointerSample.has_value());
    const Render::WorldPointerSample lockedSample = *firstTransition->worldPointerSample;

    const std::optional<u64> secondCameraRevision = notePresentedCamera(latch, camera(100.0F, 100.0F), 11);
    ASSERT_TRUE(secondCameraRevision.has_value());
    ASSERT_GT(*secondCameraRevision, *firstCameraRevision);

    const PointerFrameSpec zeroStepFrame{
        .frameId = {2},
        .logicalWidth = 200,
        .logicalHeight = 200,
        .finalPointerX = 150.0,
        .finalPointerY = 150.0,
        .heldPointerButtons = {Platform::PointerButton::Primary},
    };
    ASSERT_TRUE(mapPointerFrame(*mapper, *builder, window, zeroStepFrame, 1, 7, &latch).has_value());

    auto pendingAfter = mapper->simulationActionsForTick(7);
    ASSERT_TRUE(pendingAfter.has_value()) << (pendingAfter ? "" : pendingAfter.error().message);
    ASSERT_EQ(pendingAfter->transitions.size(), 1U);
    const DigitalActionTransition* retainedTransition = digital(pendingAfter->transitions[0]);
    ASSERT_NE(retainedTransition, nullptr);
    ASSERT_TRUE(retainedTransition->worldPointerSample.has_value());

    EXPECT_EQ(*retainedTransition->worldPointerSample, lockedSample);
    EXPECT_FLOAT_EQ(retainedTransition->worldPointerSample->worldX, -2.5F);
    EXPECT_FLOAT_EQ(retainedTransition->worldPointerSample->worldY, -2.5F);
    EXPECT_EQ(retainedTransition->worldPointerSample->cameraRevision, *firstCameraRevision);
    EXPECT_EQ(retainedTransition->worldPointerSample->surfaceRevision, 10U);
}

} // namespace
} // namespace Tina::Tests
