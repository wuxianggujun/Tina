#pragma once

#include <tina/ui/UICommittedHit.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

enum class UIPointerRoutePathError : u8 {
    None,
    InvalidEntryIndex,
    CapacityExceeded,
    AncestryCycle,
    InvalidRoot,
};

struct UIPointerRoutePathBuildResult final {
    usize depth = 0;
    UIPointerRoutePathError error = UIPointerRoutePathError::None;

    [[nodiscard]] constexpr bool succeeded() const noexcept
    {
        return error == UIPointerRoutePathError::None;
    }
};

[[nodiscard]] constexpr UIPointerHitTarget pointerHitTargetForEntry(
    std::span<const UICommittedHitEntry> entries,
    u32 entryIndex) noexcept
{
    if (entryIndex >= entries.size())
    {
        return {};
    }
    const UICommittedHitEntry& entry = entries[entryIndex];
    if (entry.rootEntryIndex >= entries.size())
    {
        return {};
    }
    return UIPointerHitTarget{
        .node = entry.node,
        .rootNode = entries[entry.rootEntryIndex].node,
        .hitEntryIndex = entryIndex,
        .rootEntryIndex = entry.rootEntryIndex,
        .worldRect = entry.worldRect,
        .effectiveClip = entry.effectiveClip,
        .paintOrdinal = entry.paintOrdinal,
    };
}

[[nodiscard]] inline UIPointerRoutePathBuildResult buildPointerRoutePath(
    const UIPointerHitTarget& target,
    std::span<const UICommittedHitEntry> entries,
    usize routePathCapacity,
    std::pmr::vector<u32>& output)
{
    output.clear();
    u32 entryIndex = target.hitEntryIndex;
    while (entryIndex != InvalidUIHitEntryIndex)
    {
        if (entryIndex >= entries.size())
        {
            output.clear();
            return {.error = UIPointerRoutePathError::InvalidEntryIndex};
        }
        if (output.size() >= routePathCapacity)
        {
            output.clear();
            return {.error = UIPointerRoutePathError::CapacityExceeded};
        }
        output.push_back(entryIndex);
        if (entryIndex == target.rootEntryIndex)
        {
            break;
        }
        entryIndex = entries[entryIndex].parentEntryIndex;
        if (output.size() > entries.size())
        {
            output.clear();
            return {.error = UIPointerRoutePathError::AncestryCycle};
        }
    }

    if (output.empty() || output.back() != target.rootEntryIndex ||
        entries[output.back()].node != target.rootNode)
    {
        output.clear();
        return {.error = UIPointerRoutePathError::InvalidRoot};
    }
    return {.depth = output.size()};
}

} // namespace Tina::UI::Detail
