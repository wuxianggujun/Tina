#include <tina/ai/BehaviorTree.hpp>
#include <tina/ai/StateMachine.hpp>

#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace Tina;

Core::Result<AI::BehaviorStatus> succeed(AI::Blackboard&, double, void*) { return AI::BehaviorStatus::Success; }
Core::Result<AI::BehaviorStatus> running(AI::Blackboard&, double, void*) { return AI::BehaviorStatus::Running; }

class FailingMemoryResource final : public std::pmr::memory_resource {
public:
    explicit FailingMemoryResource(Core::usize remaining = (std::numeric_limits<Core::usize>::max)())
        : m_remaining(remaining) {}
    void seal() noexcept { m_remaining = 0; }
    Core::usize liveBytes() const noexcept { return m_liveBytes; }
private:
    void* do_allocate(Core::usize bytes, Core::usize alignment) override {
        if (m_remaining == 0) { throw std::bad_alloc{}; }
        void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        --m_remaining;
        m_liveBytes += bytes;
        return result;
    }
    void do_deallocate(void* pointer, Core::usize bytes, Core::usize alignment) override {
        m_liveBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    Core::usize m_remaining;
    Core::usize m_liveBytes = 0;
};

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

TEST(AI, BlackboardKeysAreSparseAndZeroReserveIsValid) {
    auto board = AI::Blackboard::Create({.initialSlotReserve = 0});
    ASSERT_TRUE(board);
    constexpr AI::BlackboardKey<Core::u64> highKey{(std::numeric_limits<Core::u32>::max)() - 1};
    EXPECT_EQ(board->boundSlotCount(), 0U);
    ASSERT_TRUE(board->set(highKey, 42));
    ASSERT_TRUE(board->set(AI::BlackboardKey<bool>{70000}, true));
    EXPECT_EQ(board->boundSlotCount(), 2U);
    EXPECT_EQ(board->valueCount(), 2U);
    EXPECT_EQ(board->get(highKey).value(), 42U);
    board->clearValues();
    EXPECT_EQ(board->valueCount(), 0U);
    EXPECT_EQ(board->boundSlotCount(), 2U);
    EXPECT_FALSE(board->set(AI::BlackboardKey<float>{highKey.slot}, 1.0F));
    EXPECT_TRUE(board->set(highKey, 43));
    EXPECT_FALSE(board->set(AI::BlackboardKey<bool>{}, true));
    EXPECT_EQ(board->boundSlotCount(), 2U);
}

TEST(AI, BlackboardGrowthFailurePreservesValuesAndTypeBindings) {
    FailingMemoryResource memory;
    auto board = AI::Blackboard::Create({.initialSlotReserve = 1}, memory);
    ASSERT_TRUE(board);
    const AI::BlackboardKey<Core::i64> key{7};
    ASSERT_TRUE(board->set(key, 10));
    memory.seal();
    auto rejected = board->set(AI::BlackboardKey<bool>{8}, true);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, AI::AIErrorCode::AllocationFailed);
    EXPECT_EQ(board->boundSlotCount(), 1U);
    ASSERT_TRUE(board->set(key, 11));
    EXPECT_EQ(board->get(key).value(), 11);
    AI::Blackboard moved(std::move(*board));
    EXPECT_FALSE(*board);
    EXPECT_EQ(moved.get(key).value(), 11);
}

TEST(AI, BlackboardFactoryFailureReleasesPartialStorage) {
    bool reachedSuccess = false;
    for (Core::usize limit = 0; limit < 20 && !reachedSuccess; ++limit) {
        FailingMemoryResource memory(limit);
        {
            auto board = AI::Blackboard::Create({.initialSlotReserve = 4}, memory);
            reachedSuccess = board.has_value();
            if (!board) { EXPECT_EQ(board.error().code, AI::AIErrorCode::AllocationFailed); }
        }
        EXPECT_EQ(memory.liveBytes(), 0U) << "allocation limit " << limit;
    }
    EXPECT_TRUE(reachedSuccess);
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

TEST(AI, BehaviorTreeHasNoArtificialNodeOrCallStackDepthLimit) {
    constexpr Core::usize nodeCount = 8193;
    std::vector<Core::u32> edges(nodeCount - 1);
    std::vector<AI::BehaviorNodeDesc> nodes(nodeCount);
    for (Core::usize index = 0; index + 1 < nodeCount; ++index) {
        edges[index] = static_cast<Core::u32>(index + 1);
        nodes[index] = {.kind = AI::BehaviorNodeKind::Inverter, .children = {&edges[index], 1}};
    }
    FailingMemoryResource memory;
    auto tree = AI::BehaviorTree::Create(nodes, 0, memory);
    auto board = AI::Blackboard::Create();
    ASSERT_TRUE(tree);
    ASSERT_TRUE(board);
    EXPECT_EQ(tree->nodeCount(), nodeCount);
    memory.seal();
    auto first = tree->tick(*board, 0.0, 31);
    ASSERT_TRUE(first);
    EXPECT_TRUE(first->budgetExhausted);
    for (Core::usize pass = 0; pass < nodeCount && tree->state() == AI::BehaviorTreeState::Running; ++pass) {
        ASSERT_TRUE(tree->tick(*board, 0.0, 257));
    }
    EXPECT_EQ(tree->state(), AI::BehaviorTreeState::Succeeded);
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

Core::Result<AI::StateDecision> stay(AI::Blackboard&, double, void*) { return AI::StateDecision::stay(); }

TEST(AI, StateMachineHasNoArtificialStateCountLimit) {
    std::vector<AI::StateDesc> states(8192, AI::StateDesc{.tick = stay});
    auto machine = AI::StateMachine::Create(states, 8191);
    auto board = AI::Blackboard::Create();
    ASSERT_TRUE(machine);
    ASSERT_TRUE(board);
    auto result = machine->tick(*board, 0.0);
    ASSERT_TRUE(result);
    EXPECT_EQ(machine->stateCount(), states.size());
    EXPECT_EQ(result->activeState, 8191U);
    EXPECT_EQ(result->state, AI::StateMachineState::Running);
    ASSERT_TRUE(machine->cancel());
}

TEST(AI, StateMachineDestructorPublishesTerminalStateBeforeOneNonReentrantExit) {
    struct ExitProbe {
        AI::StateMachine* machine = nullptr;
        int exits = 0;
        bool rejectedCancel = false;
        bool rejectedReset = false;
        bool rejectedTick = false;
        AI::StateMachineState observed = AI::StateMachineState::Idle;
    } probe;
    auto board = AI::Blackboard::Create();
    ASSERT_TRUE(board);
    const std::array<AI::StateDesc, 1> states{{{
        .tick = stay,
        .exit = [](AI::Blackboard& blackboard, void* opaque) noexcept {
            auto& value = *static_cast<ExitProbe*>(opaque);
            ++value.exits;
            value.observed = value.machine->state();
            const auto cancel = value.machine->cancel();
            const auto reset = value.machine->reset();
            const auto tick = value.machine->tick(blackboard, 0.0);
            value.rejectedCancel = !cancel && cancel.error().code == AI::AIErrorCode::ReentrantDispatch;
            value.rejectedReset = !reset && reset.error().code == AI::AIErrorCode::ReentrantDispatch;
            value.rejectedTick = !tick && tick.error().code == AI::AIErrorCode::ReentrantDispatch;
        },
        .userData = &probe,
    }}};
    auto result = AI::StateMachine::Create(states);
    ASSERT_TRUE(result);
    std::optional<AI::StateMachine> machine(std::move(*result));
    probe.machine = &*machine;
    ASSERT_TRUE(machine->tick(*board, 0.0));
    machine.reset();
    EXPECT_EQ(probe.exits, 1);
    EXPECT_EQ(probe.observed, AI::StateMachineState::Cancelled);
    EXPECT_TRUE(probe.rejectedCancel);
    EXPECT_TRUE(probe.rejectedReset);
    EXPECT_TRUE(probe.rejectedTick);
}
} // namespace
