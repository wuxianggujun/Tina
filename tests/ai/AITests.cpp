#include <tina/ai/BehaviorTree.hpp>
#include <tina/ai/StateMachine.hpp>

#include <gtest/gtest.h>
#include <array>

namespace {
using namespace Tina;

Core::Result<AI::BehaviorStatus> succeed(AI::Blackboard&, double, void*) { return AI::BehaviorStatus::Success; }
Core::Result<AI::BehaviorStatus> running(AI::Blackboard&, double, void*) { return AI::BehaviorStatus::Running; }

TEST(AI, BlackboardFixesTypeAndClearsValue) {
    auto board = AI::Blackboard::Create({4});
    ASSERT_TRUE(board);
    AI::BlackboardKey<Core::i64> key{1};
    EXPECT_TRUE(board->set(key, 7));
    EXPECT_EQ(board->get(key).value(), 7);
    EXPECT_TRUE(board->clear(key));
    EXPECT_FALSE(board->contains(key));
    EXPECT_FALSE(board->set(AI::BlackboardKey<float>{1}, 1.0F));
}

TEST(AI, BehaviorTreeMemorySequenceResumesRunningLeaf) {
    std::array<Core::u32, 2> children{1, 2};
    std::array<AI::BehaviorNodeDesc, 3> nodes{{
        {.kind = AI::BehaviorNodeKind::Sequence, .children = children},
        {.kind = AI::BehaviorNodeKind::Action, .tick = running},
        {.kind = AI::BehaviorNodeKind::Action, .tick = succeed},
    }};
    auto tree = AI::BehaviorTree::Create(nodes);
    ASSERT_TRUE(tree);
    auto board = AI::Blackboard::Create();
    ASSERT_TRUE(board);
    auto result = tree->tick(*board, 0.016, 8);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->state, AI::BehaviorTreeState::Running);
}

Core::Result<AI::StateDecision> toState(AI::Blackboard&, double, void* data) {
    auto* count = static_cast<int*>(data);
    ++*count;
    return *count == 1 ? AI::StateDecision::transition(1) : AI::StateDecision::succeed();
}

TEST(AI, StateMachineTransitionsAndTerminates) {
    int firstTicks = 0;
    std::array<AI::StateDesc, 2> states{{
        {.tick = toState, .userData = &firstTicks},
        {.tick = toState, .userData = &firstTicks},
    }};
    auto machine = AI::StateMachine::Create(states);
    ASSERT_TRUE(machine);
    auto board = AI::Blackboard::Create();
    ASSERT_TRUE(board);
    auto result = machine->tick(*board, 0.0, 2);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->state, AI::StateMachineState::Succeeded);
}
} // namespace
