#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIPaint.hpp>

#include <limits>
#include <memory_resource>
#include <span>
#include <utility>
#include <vector>

namespace Tina::UI::Detail {

class UICanvasCommandStorage final {
  public:
    UICanvasCommandStorage(usize nodeCapacity, usize commandCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Status assign(u32 nodeIndex, std::span<const UICanvasCommand> commands);
    void release(u32 nodeIndex) noexcept;

    template <typename Visitor>
    void forEach(u32 nodeIndex, Visitor&& visitor) const
        noexcept(noexcept(std::forward<Visitor>(visitor)(std::declval<const UICanvasCommand&>())))
    {
        if (nodeIndex >= statesByNodeIndex_.size())
        {
            return;
        }

        const NodeState& state = statesByNodeIndex_[nodeIndex];
        u32 commandIndex = state.first;
        for (u32 visited = 0; commandIndex != InvalidCommandIndex && visited < state.count; ++visited)
        {
            if (commandIndex >= slots_.size())
            {
                return;
            }
            const CommandSlot& slot = slots_[commandIndex];
            std::forward<Visitor>(visitor)(slot.command);
            commandIndex = slot.next;
        }
    }

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;

  private:
    static constexpr u32 InvalidCommandIndex = (std::numeric_limits<u32>::max)();

    struct CommandSlot final {
        UICanvasCommand command{};
        u32 next = InvalidCommandIndex;
    };

    struct NodeState final {
        u32 first = InvalidCommandIndex;
        u32 count = 0;
    };

    std::pmr::vector<NodeState> statesByNodeIndex_;
    std::pmr::vector<CommandSlot> slots_;
    u32 freeHead_ = InvalidCommandIndex;
    usize activeCount_ = 0;
    usize highWater_ = 0;
};

} // namespace Tina::UI::Detail
