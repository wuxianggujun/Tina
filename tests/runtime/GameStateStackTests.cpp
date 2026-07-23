#include <gtest/gtest.h>

#include <tina/runtime/GameState.hpp>
#include <tina/runtime/GameStateCommands.hpp>
#include <tina/runtime/InputActions.hpp>
#include <tina/runtime/RuntimeErrors.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Tina::Tests {
namespace {

using EventLog = std::vector<std::string>;

class RecordingState final : public IGameState {
public:
    explicit RecordingState(EventLog& log, std::string name) : m_log(&log), m_name(std::move(name)) {}

    Core::Status onEnter(GameStateEnterContext&) override
    {
        m_log->push_back(m_name + ".onEnter");
        return Core::success();
    }

    void onExit(GameStateExitContext&) noexcept override { m_log->push_back(m_name + ".onExit"); }

    [[nodiscard]] GameStatePolicy initialPolicy() const noexcept override
    {
        return GameStatePolicy{.blocksGameplayInputBelow = m_name == "overlay"};
    }

private:
    EventLog* m_log = nullptr;
    std::string m_name;
};

TEST(GameStateStackTest, PushPopMaintainsLIFOAndPolicy)
{
    GameStateStack stack;
    EventLog log;
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "base"), {}));
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "overlay"),
                                    GameStatePolicy{.blocksGameplayInputBelow = true}));
    EXPECT_EQ(stack.size(), 2U);
    EXPECT_TRUE(stack.topPolicy().blocksGameplayInputBelow);
    auto top = stack.popCommitted();
    ASSERT_NE(top, nullptr);
    EXPECT_FALSE(stack.topPolicy().blocksGameplayInputBelow);
    EXPECT_EQ(stack.size(), 1U);
    (void)stack.popCommitted();
    EXPECT_TRUE(stack.empty());
}

TEST(GameStateStackTest, RejectsPushBeyondCapacity)
{
    GameStateStack stack;
    EventLog log;
    for (usize i = 0; i < GameStatePendingCommands::MaxStackDepth; ++i)
    {
        ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "s"), {})) << i;
    }
    auto status = stack.pushCommitted(std::make_unique<RecordingState>(log, "overflow"), {});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, RuntimeErrorCode::GameStateStackCapacityExceeded);
}

TEST(GameStatePendingCommandsTest, TracksSingleStructuralAndPolicySlots)
{
    GameStatePendingCommands pending{};
    EXPECT_FALSE(pending.hasStructural());

    EventLog log;
    pending.structural = GameStateStructuralCommandKind::Push;
    pending.candidate = std::make_unique<RecordingState>(log, "a");
    ++pending.structuralSequence;
    EXPECT_TRUE(pending.hasStructural());
    EXPECT_EQ(pending.structuralSequence, 1U);
    EXPECT_NE(pending.candidate, nullptr);

    pending.policyChangeRequested = true;
    pending.requestedPolicy = GameStatePolicy{.blocksFixedUpdateBelow = true};
    ++pending.policySequence;
    EXPECT_TRUE(pending.policyChangeRequested);
    EXPECT_TRUE(pending.requestedPolicy.blocksFixedUpdateBelow);

    pending.clearAll();
    EXPECT_FALSE(pending.hasStructural());
    EXPECT_FALSE(pending.policyChangeRequested);
    EXPECT_EQ(pending.candidate, nullptr);
}

TEST(GameStatePolicyDispatchTest, BlocksBelowStopsPropagation)
{
    EXPECT_TRUE(policyBlocksBelow(GameStatePolicy{.blocksFixedUpdateBelow = true},
                                  GameStateDispatchPhase::FixedUpdate));
    EXPECT_FALSE(policyBlocksBelow(GameStatePolicy{.blocksFixedUpdateBelow = true},
                                   GameStateDispatchPhase::FrameUpdate));
    EXPECT_TRUE(policyBlocksBelow(GameStatePolicy{.blocksFrameUpdateBelow = true},
                                  GameStateDispatchPhase::FrameUpdate));
    EXPECT_TRUE(policyBlocksBelow(GameStatePolicy{.blocksRenderBelow = true},
                                  GameStateDispatchPhase::RenderExtract));
    EXPECT_TRUE(policyBlocksBelow(GameStatePolicy{.blocksUIInputBelow = true},
                                  GameStateDispatchPhase::UIUpdate));
}

TEST(GameStatePolicyDispatchTest, GameplayInputBlockedForDepthUsesOverlayFlag)
{
    GameStateStack stack;
    EventLog log;
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "base"), {}));
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "overlay"),
                                    GameStatePolicy{.blocksGameplayInputBelow = true}));
    EXPECT_FALSE(stack.gameplayInputBlockedForDepth(0));
    EXPECT_TRUE(stack.gameplayInputBlockedForDepth(1));

    stack.setTopPolicy({});
    EXPECT_FALSE(stack.gameplayInputBlockedForDepth(1));
}

TEST(InputActionSnapshotTest, SuppressedSnapshotsReportNoHeldActions)
{
    const auto sim = SimulationActionSnapshot::suppressed(7);
    EXPECT_EQ(sim.targetSimulationTick, 7U);
    EXPECT_TRUE(sim.states.empty());
    EXPECT_FALSE(sim.isHeld(InputActionId{1}));
    EXPECT_FLOAT_EQ(sim.axis(InputActionId{1}), 0.0F);

    const auto frame = FrameActionSnapshot::suppressed(9);
    EXPECT_EQ(frame.engineFrameIndex, 9U);
    EXPECT_TRUE(frame.states.empty());
    EXPECT_FALSE(frame.isHeld(InputActionId{2}));
}

TEST(GameStatePolicyDispatchTest, CollectDispatchIndicesHonorsOverlayBlock)
{
    GameStateStack stack;
    EventLog log;
    // index 0 = base (bottom), index 1 = overlay (top)
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "base"), {}));
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "overlay"),
                                    GameStatePolicy{.blocksUIInputBelow = true,
                                                    .blocksFixedUpdateBelow = true,
                                                    .blocksFrameUpdateBelow = true,
                                                    .blocksRenderBelow = true}));

    std::array<usize, 4> indices{};
    // FixedUpdate: only top (stack index 1)
    usize n = stack.collectDispatchIndices(GameStateDispatchPhase::FixedUpdate, indices);
    ASSERT_EQ(n, 1U);
    EXPECT_EQ(indices[0], 1U);

    // Without block on top, both would run if policy is empty on top
    stack.setTopPolicy({});
    n = stack.collectDispatchIndices(GameStateDispatchPhase::FixedUpdate, indices);
    ASSERT_EQ(n, 2U);
    EXPECT_EQ(indices[0], 1U); // top first
    EXPECT_EQ(indices[1], 0U); // then base
}

TEST(GameStatePolicyDispatchTest, ForEachDispatchOrderIsTopDownAndStops)
{
    GameStateStack stack;
    EventLog log;
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "base"), {}));
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "mid"), {}));
    ASSERT_TRUE(stack.pushCommitted(std::make_unique<RecordingState>(log, "top"),
                                    GameStatePolicy{.blocksFrameUpdateBelow = true}));

    std::vector<usize> visitedDepths;
    ASSERT_TRUE(stack
                    .forEachDispatch(GameStateDispatchPhase::FrameUpdate,
                                     [&](IGameState&, const GameStatePolicy&, usize depth) -> Core::Status {
                                         visitedDepths.push_back(depth);
                                         return Core::success();
                                     })
                    .has_value());
    // Only top (depth 0) — mid/base blocked by blocksFrameUpdateBelow
    ASSERT_EQ(visitedDepths.size(), 1U);
    EXPECT_EQ(visitedDepths[0], 0U);

    visitedDepths.clear();
    stack.setTopPolicy({});
    ASSERT_TRUE(stack
                    .forEachDispatch(GameStateDispatchPhase::FrameUpdate,
                                     [&](IGameState&, const GameStatePolicy&, usize depth) -> Core::Status {
                                         visitedDepths.push_back(depth);
                                         return Core::success();
                                     })
                    .has_value());
    ASSERT_EQ(visitedDepths.size(), 3U);
    EXPECT_EQ(visitedDepths[0], 0U);
    EXPECT_EQ(visitedDepths[1], 1U);
    EXPECT_EQ(visitedDepths[2], 2U);
}

} // namespace
} // namespace Tina::Tests
