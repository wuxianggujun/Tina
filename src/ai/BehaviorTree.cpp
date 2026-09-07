#include <tina/ai/BehaviorTree.hpp>

#include "DispatchGuard.hpp"

#include <cmath>
#include <exception>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace Tina::AI {

struct BehaviorTree::Storage final {
    struct Node final {
        BehaviorNodeKind kind = BehaviorNodeKind::Succeed;
        Core::u32 firstChild = 0;
        Core::u32 childCount = 0;
        BehaviorTick tick = nullptr;
        BehaviorHalt halt = nullptr;
        void* userData = nullptr;
    };
    struct Frame final { Core::u32 node = 0; Core::u32 child = 0; };
    Storage(Core::usize count, std::pmr::memory_resource& memory)
        : resource(&memory), nodes(count, &memory), children(0U, &memory), frames(0U, &memory),
          parents(count, Core::u32{0}, &memory)
    {
        children.reserve(count - 1U);
        frames.reserve(count);
    }
    std::pmr::memory_resource* resource;
    std::pmr::vector<Node> nodes;
    std::pmr::vector<Core::u32> children;
    std::pmr::vector<Frame> frames;
    std::pmr::vector<Core::u32> parents;
    std::optional<BehaviorStatus> childResult{};
};

void BehaviorTree::destroyStorage(Storage* storage) noexcept
{
    if (storage != nullptr) { std::pmr::polymorphic_allocator<Storage>{storage->resource}.delete_object(storage); }
}

BehaviorTree::BehaviorTree(StorageOwner storage, Core::u32 root) noexcept
    : m_storage(std::move(storage)), m_root(root) {}

BehaviorTree::BehaviorTree(BehaviorTree&& other) noexcept
    : m_storage(std::move(other.m_storage)), m_blackboard(std::exchange(other.m_blackboard, nullptr)),
      m_root(other.m_root), m_activeNode(std::exchange(other.m_activeNode, InvalidNode)),
      m_state(std::exchange(other.m_state, BehaviorTreeState::Idle))
{
    if (other.m_dispatching) { std::terminate(); }
}

BehaviorTree::~BehaviorTree() noexcept
{
    if (m_dispatching) { std::terminate(); }
    Detail::DispatchGuard guard{m_dispatching};
    haltActive(BehaviorHaltReason::Destroyed);
}

Core::Result<BehaviorTree> BehaviorTree::Create(
    std::span<const BehaviorNodeDesc> descriptions, Core::u32 root, std::pmr::memory_resource& resource)
{
    constexpr Core::usize maximumNodes = 4096;
    if (descriptions.empty() || descriptions.size() > maximumNodes || root >= descriptions.size())
    {
        return Core::failure(AIErrorCode::InvalidTree, "behavior tree requires 1..4096 nodes and an in-range root");
    }
    try
    {
        StorageOwner storage{std::pmr::polymorphic_allocator<Storage>{&resource}.new_object<Storage>(
                                 descriptions.size(), resource), &destroyStorage};
        for (Core::usize index = 0; index < descriptions.size(); ++index)
        {
            const auto& desc = descriptions[index];
            bool valid = false;
            switch (desc.kind)
            {
            case BehaviorNodeKind::Action: valid = desc.children.empty() && desc.tick; break;
            case BehaviorNodeKind::Condition: valid = desc.children.empty() && desc.tick && !desc.halt; break;
            case BehaviorNodeKind::Sequence:
            case BehaviorNodeKind::Selector: valid = !desc.children.empty() && !desc.tick && !desc.halt; break;
            case BehaviorNodeKind::Inverter: valid = desc.children.size() == 1 && !desc.tick && !desc.halt; break;
            case BehaviorNodeKind::Succeed:
            case BehaviorNodeKind::Fail: valid = desc.children.empty() && !desc.tick && !desc.halt; break;
            }
            if (!valid || desc.children.size() > descriptions.size() - 1U - storage->children.size())
            {
                return Core::failure(AIErrorCode::InvalidTree, "behavior node shape or total edge count is invalid");
            }
            storage->nodes[index] = {desc.kind, static_cast<Core::u32>(storage->children.size()),
                                     static_cast<Core::u32>(desc.children.size()), desc.tick, desc.halt, desc.userData};
            for (const auto child : desc.children)
            {
                if (child >= descriptions.size() || child == root || ++storage->parents[child] != 1)
                {
                    return Core::failure(AIErrorCode::InvalidTree, "behavior tree contains an invalid, repeated, or root child");
                }
                storage->children.push_back(child);
            }
        }
        if (storage->children.size() != descriptions.size() - 1U)
        {
            return Core::failure(AIErrorCode::InvalidTree, "behavior tree contains disconnected nodes");
        }
        storage->frames.push_back({root, 0});
        Core::usize visited = 0;
        while (!storage->frames.empty())
        {
            const auto index = storage->frames.back().node;
            storage->frames.pop_back();
            if (storage->parents[index] == 2) { return Core::failure(AIErrorCode::InvalidTree, "behavior tree contains a cycle"); }
            storage->parents[index] = 2;
            ++visited;
            const auto& node = storage->nodes[index];
            for (Core::u32 child = 0; child < node.childCount; ++child)
            {
                storage->frames.push_back({storage->children[node.firstChild + child], 0});
            }
        }
        if (visited != descriptions.size()) { return Core::failure(AIErrorCode::InvalidTree, "behavior tree contains a disconnected cycle"); }
        return BehaviorTree(std::move(storage), root);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AIErrorCode::AllocationFailed, "behavior tree fixed storage allocation failed");
    }
}

Core::usize BehaviorTree::nodeCount() const noexcept { return m_storage ? m_storage->nodes.size() : 0; }

void BehaviorTree::haltActive(BehaviorHaltReason reason) noexcept
{
    const auto index = std::exchange(m_activeNode, InvalidNode);
    if (index != InvalidNode && m_storage)
    {
        const auto& node = m_storage->nodes[index];
        if (node.halt) { node.halt(node.userData, reason); }
    }
}

void BehaviorTree::completeNode(BehaviorStatus status) noexcept
{
    m_storage->frames.pop_back();
    if (m_storage->frames.empty())
    {
        m_state = status == BehaviorStatus::Success ? BehaviorTreeState::Succeeded : BehaviorTreeState::Failed;
        m_storage->childResult.reset();
        m_blackboard = nullptr;
    }
    else { m_storage->childResult = status; }
}

void BehaviorTree::fault() noexcept
{
    haltActive(BehaviorHaltReason::Faulted);
    m_storage->frames.clear();
    m_storage->childResult.reset();
    m_blackboard = nullptr;
    m_state = BehaviorTreeState::Faulted;
}

Core::Result<BehaviorTickResult> BehaviorTree::tick(Blackboard& blackboard, double deltaSeconds, Core::usize nodeBudget)
{
    if (m_dispatching) { return Core::failure(AIErrorCode::ReentrantDispatch, "behavior tree dispatch cannot be reentered"); }
    if (!m_storage || !blackboard || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || nodeBudget == 0)
    {
        return Core::failure(AIErrorCode::InvalidConfig, "behavior tick requires live owners, nonnegative finite delta and positive budget");
    }
    if (m_blackboard && m_blackboard != &blackboard)
    {
        return Core::failure(AIErrorCode::InvalidState, "running behavior tree belongs to another blackboard");
    }
    if (m_state != BehaviorTreeState::Idle && m_state != BehaviorTreeState::Running)
    {
        return BehaviorTickResult{.state = m_state};
    }
    Detail::DispatchGuard guard{m_dispatching};
    if (m_state == BehaviorTreeState::Idle)
    {
        m_storage->frames.push_back({m_root, 0});
        m_blackboard = &blackboard;
        m_state = BehaviorTreeState::Running;
    }
    Core::usize visited = 0;
    try
    {
        while (visited < nodeBudget && m_state == BehaviorTreeState::Running)
        {
            ++visited;
            auto& frame = m_storage->frames.back();
            const auto& node = m_storage->nodes[frame.node];
            if (node.kind == BehaviorNodeKind::Action || node.kind == BehaviorNodeKind::Condition)
            {
                if (node.kind == BehaviorNodeKind::Action) { m_activeNode = frame.node; }
                auto result = node.tick(blackboard, deltaSeconds, node.userData);
                if (!result)
                {
                    fault();
                    return Core::failure(std::move(result.error()).withContext("BehaviorTree::tick", "leaf callback"));
                }
                if (*result != BehaviorStatus::Success && *result != BehaviorStatus::Failure &&
                    (*result != BehaviorStatus::Running || node.kind == BehaviorNodeKind::Condition))
                {
                    fault();
                    return Core::failure(AIErrorCode::CallbackFailed, "behavior leaf returned an invalid status");
                }
                if (*result == BehaviorStatus::Running)
                {
                    if (node.kind != BehaviorNodeKind::Action)
                    {
                        fault();
                        return Core::failure(AIErrorCode::CallbackFailed,
                                             "only action nodes may remain Running");
                    }
                    return BehaviorTickResult{m_state, visited, false};
                }
                m_activeNode = InvalidNode;
                completeNode(*result);
            }
            else if (node.kind == BehaviorNodeKind::Succeed || node.kind == BehaviorNodeKind::Fail)
            {
                completeNode(node.kind == BehaviorNodeKind::Succeed ? BehaviorStatus::Success : BehaviorStatus::Failure);
            }
            else if (m_storage->childResult)
            {
                const auto child = *m_storage->childResult;
                m_storage->childResult.reset();
                if (node.kind == BehaviorNodeKind::Inverter)
                {
                    completeNode(child == BehaviorStatus::Success ? BehaviorStatus::Failure : BehaviorStatus::Success);
                }
                else if ((node.kind == BehaviorNodeKind::Sequence && child == BehaviorStatus::Failure) ||
                         (node.kind == BehaviorNodeKind::Selector && child == BehaviorStatus::Success))
                {
                    completeNode(child);
                }
                else if (++frame.child == node.childCount)
                {
                    completeNode(node.kind == BehaviorNodeKind::Sequence ? BehaviorStatus::Success : BehaviorStatus::Failure);
                }
                else { m_storage->frames.push_back({m_storage->children[node.firstChild + frame.child], 0}); }
            }
            else { m_storage->frames.push_back({m_storage->children[node.firstChild], 0}); }
        }
    }
    catch (const std::bad_alloc&)
    {
        fault();
        return Core::failure(AIErrorCode::AllocationFailed, "behavior callback allocation failed");
    }
    catch (...)
    {
        fault();
        return Core::failure(AIErrorCode::CallbackFailed, "behavior callback threw an exception");
    }
    return BehaviorTickResult{m_state, visited, m_state == BehaviorTreeState::Running};
}

Core::Status BehaviorTree::cancel()
{
    if (m_dispatching) { return Core::failure(AIErrorCode::ReentrantDispatch, "behavior cancellation cannot reenter dispatch"); }
    Detail::DispatchGuard guard{m_dispatching};
    haltActive(BehaviorHaltReason::Cancelled);
    if (m_storage) { m_storage->frames.clear(); m_storage->childResult.reset(); }
    m_blackboard = nullptr;
    m_state = BehaviorTreeState::Cancelled;
    return Core::success();
}

Core::Status BehaviorTree::reset()
{
    if (m_dispatching) { return Core::failure(AIErrorCode::ReentrantDispatch, "behavior reset cannot reenter dispatch"); }
    Detail::DispatchGuard guard{m_dispatching};
    haltActive(BehaviorHaltReason::Reset);
    if (m_storage) { m_storage->frames.clear(); m_storage->childResult.reset(); }
    m_blackboard = nullptr;
    m_state = BehaviorTreeState::Idle;
    return Core::success();
}

} // namespace Tina::AI
