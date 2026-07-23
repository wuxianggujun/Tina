#include <gtest/gtest.h>

#include <tina/runtime/GameState.hpp>
#include <tina/runtime/GameStateCommands.hpp>
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

} // namespace
} // namespace Tina::Tests
