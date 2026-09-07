#pragma once

#include <tina/ai/Blackboard.hpp>

#include <limits>
#include <memory>
#include <memory_resource>
#include <span>

namespace Tina::AI {

enum class BehaviorStatus : Core::u8 { Success, Failure, Running };
enum class BehaviorNodeKind : Core::u8 { Action, Condition, Sequence, Selector, Inverter, Succeed, Fail };
enum class BehaviorTreeState : Core::u8 { Idle, Running, Succeeded, Failed, Cancelled, Faulted };
enum class BehaviorHaltReason : Core::u8 { Cancelled, Reset, Faulted, Destroyed };

using BehaviorTick = Core::Result<BehaviorStatus> (*)(Blackboard&, double deltaSeconds, void* userData);
using BehaviorHalt = void (*)(void* userData, BehaviorHaltReason reason) noexcept;

struct BehaviorNodeDesc final {
    BehaviorNodeKind kind = BehaviorNodeKind::Succeed;
    // Borrowed only during Create; the validated topology is deep-copied.
    std::span<const Core::u32> children{};
    BehaviorTick tick = nullptr;
    BehaviorHalt halt = nullptr;
    // Borrowed until tree destruction, including an optional destructor halt.
    void* userData = nullptr;
};

struct BehaviorTickResult final {
    BehaviorTreeState state = BehaviorTreeState::Idle;
    Core::usize visitedNodes = 0;
    bool budgetExhausted = false;
};

// Memory Sequence/Selector semantics: resume the running child, do not recheck
// already completed siblings. Terminal roots remain terminal until reset().
// One owner per agent; no recursion, worker, Scene dependency, or global state.
class BehaviorTree final {
public:
    [[nodiscard]] static Core::Result<BehaviorTree> Create(
        std::span<const BehaviorNodeDesc> nodes, Core::u32 root = 0,
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());
    ~BehaviorTree() noexcept;
    BehaviorTree(const BehaviorTree&) = delete;
    BehaviorTree& operator=(const BehaviorTree&) = delete;
    BehaviorTree(BehaviorTree&& other) noexcept;
    BehaviorTree& operator=(BehaviorTree&&) = delete;

    [[nodiscard]] Core::Result<BehaviorTickResult> tick(Blackboard& blackboard, double deltaSeconds,
                                                      Core::usize nodeBudget);
    [[nodiscard]] Core::Status cancel();
    [[nodiscard]] Core::Status reset();
    [[nodiscard]] BehaviorTreeState state() const noexcept { return m_state; }
    [[nodiscard]] Core::usize nodeCount() const noexcept;

private:
    inline static constexpr Core::u32 InvalidNode = (std::numeric_limits<Core::u32>::max)();
    struct Storage;
    static void destroyStorage(Storage* storage) noexcept;
    using StorageOwner = std::unique_ptr<Storage, decltype(&destroyStorage)>;
    BehaviorTree(StorageOwner storage, Core::u32 root) noexcept;
    void haltActive(BehaviorHaltReason reason) noexcept;
    void completeNode(BehaviorStatus status) noexcept;
    void fault() noexcept;

    StorageOwner m_storage{nullptr, &destroyStorage};
    const Blackboard* m_blackboard = nullptr; // Identity only; never dereferenced on teardown.
    Core::u32 m_root = 0;
    Core::u32 m_activeNode = InvalidNode;
    BehaviorTreeState m_state = BehaviorTreeState::Idle;
    bool m_dispatching = false;
};

} // namespace Tina::AI
