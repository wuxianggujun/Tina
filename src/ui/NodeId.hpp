#pragma once

#include <cstdint>
#include <limits>

namespace Tina::UI {

// Window-local retained UI handle. A slot can be reused only after its
// generation changes, so stale handles never resolve to a replacement node.
struct NodeId {
    static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

    uint32_t index = InvalidIndex;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return index != InvalidIndex && generation != 0;
    }

    explicit constexpr operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(NodeId lhs, NodeId rhs) noexcept {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend constexpr bool operator!=(NodeId lhs, NodeId rhs) noexcept {
        return !(lhs == rhs);
    }
};

} // namespace Tina::UI
