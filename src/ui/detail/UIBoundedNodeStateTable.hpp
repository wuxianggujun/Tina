#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UINodeId.hpp>

#include <algorithm>
#include <cassert>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI::Detail {

// Fixed-capacity sparse state keyed by the owning retained node. Slots are kept
// sorted by node index: lookup is logarithmic in active states, while insertion
// and removal are mutation-time operations. reserve() happens at Context create,
// so publishing and steady-state updates never grow the allocator.
template <typename State>
class UIBoundedNodeStateTable final {
  public:
    UIBoundedNodeStateTable(usize capacity, std::pmr::memory_resource& resource)
        : capacity_(capacity), states_(&resource)
    {
        static_assert(std::is_nothrow_move_constructible_v<State>);
        static_assert(std::is_nothrow_move_assignable_v<State>);
        states_.reserve(capacity_);
    }

    [[nodiscard]] usize capacity() const noexcept { return capacity_; }
    [[nodiscard]] usize size() const noexcept { return states_.size(); }
    [[nodiscard]] usize availableCount() const noexcept
    {
        return capacity_ - states_.size();
    }

    [[nodiscard]] bool contains(UINodeId node) const noexcept
    {
        return tryGet(node) != nullptr;
    }

    [[nodiscard]] State* tryGet(UINodeId node) noexcept
    {
        return const_cast<State*>(std::as_const(*this).tryGet(node));
    }

    [[nodiscard]] const State* tryGet(UINodeId node) const noexcept
    {
        if (!node.hasValue())
        {
            return nullptr;
        }
        const auto found = lowerBound(node.index());
        return found != states_.end() && found->node == node ? &*found : nullptr;
    }

    [[nodiscard]] State* tryGetByIndex(u32 nodeIndex) noexcept
    {
        const auto found = lowerBound(nodeIndex);
        return found != states_.end() && found->node.index() == nodeIndex ? &*found : nullptr;
    }

    [[nodiscard]] const State* tryGetByIndex(u32 nodeIndex) const noexcept
    {
        const auto found = lowerBound(nodeIndex);
        return found != states_.end() && found->node.index() == nodeIndex ? &*found : nullptr;
    }

    [[nodiscard]] bool insertOrAssign(State state) noexcept
    {
        assert(state.node.hasValue());
        const auto found = lowerBound(state.node.index());
        if (found != states_.end() && found->node.index() == state.node.index())
        {
            *found = std::move(state);
            return true;
        }
        if (states_.size() == capacity_)
        {
            return false;
        }
        states_.insert(found, std::move(state));
        return true;
    }

    [[nodiscard]] bool erase(UINodeId node) noexcept
    {
        if (!node.hasValue())
        {
            return false;
        }
        const auto found = lowerBound(node.index());
        if (found == states_.end() || found->node != node)
        {
            return false;
        }
        states_.erase(found);
        return true;
    }

    [[nodiscard]] bool eraseByIndex(u32 nodeIndex) noexcept
    {
        const auto found = lowerBound(nodeIndex);
        if (found == states_.end() || found->node.index() != nodeIndex)
        {
            return false;
        }
        states_.erase(found);
        return true;
    }

    [[nodiscard]] std::span<State> states() noexcept { return states_; }
    [[nodiscard]] std::span<const State> states() const noexcept { return states_; }

  private:
    [[nodiscard]] auto lowerBound(u32 nodeIndex) noexcept
    {
        return std::lower_bound(
            states_.begin(), states_.end(), nodeIndex,
            [](const State& state, u32 index) noexcept { return state.node.index() < index; });
    }

    [[nodiscard]] auto lowerBound(u32 nodeIndex) const noexcept
    {
        return std::lower_bound(
            states_.begin(), states_.end(), nodeIndex,
            [](const State& state, u32 index) noexcept { return state.node.index() < index; });
    }

    usize capacity_ = 0;
    std::pmr::vector<State> states_;
};

} // namespace Tina::UI::Detail
