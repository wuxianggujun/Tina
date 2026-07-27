#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <limits>

namespace Tina::UI {

// Snapshot-local entry index. It is never a persistent node identity.
inline constexpr u32 InvalidUIHitEntryIndex = (std::numeric_limits<u32>::max)();

// Whether a committed, effectively visible node may become the target of a
// pointer hit. Ignore only excludes the node itself; targetable descendants
// remain eligible and the ignored node stays in the committed route ancestry.
enum class UIPointerHitPolicy : u8 {
    Ignore,
    Targetable,
};

struct UIPointerHitTarget final {
    UINodeId node{};
    UINodeId rootNode{};
    u32 hitEntryIndex = InvalidUIHitEntryIndex;
    u32 rootEntryIndex = InvalidUIHitEntryIndex;
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    u32 paintOrdinal = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return node.hasValue() && rootNode.hasValue() && hitEntryIndex != InvalidUIHitEntryIndex &&
               rootEntryIndex != InvalidUIHitEntryIndex;
    }
};

// Pure query result over one committed hit snapshot. A miss is a successful
// value with hasTarget() == false. Revisions bind the route indices to the
// exact snapshot that produced them; visitedEntryCount is profiling evidence.
struct UIPointerHitQueryResult final {
    UIPointerHitTarget target{};
    usize visitedEntryCount = 0;
    u64 structureRevision = 0;
    u64 layoutRevision = 0;
    u64 paintOrderRevision = 0;
    u64 hitRevision = 0;
    // True when a visible committed Modal limits this query to its subtree.
    bool modalBarrierActive = false;

    [[nodiscard]] constexpr bool hasTarget() const noexcept
    {
        return target.hasValue();
    }
};

} // namespace Tina::UI
