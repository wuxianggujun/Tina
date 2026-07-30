#pragma once

#include <tina/ui/UICommittedHit.hpp>

#include <span>

namespace Tina::UI::Detail {

enum class UIPointerRouteInspectionError : u8 {
    None,
    PhysicalAncestryCycle,
    InvalidRouteEntryIndex,
};

struct UIPointerRouteInspection final {
    UINodeId physicalNearestActivatable{};
    UINodeId routedNearestActivatable{};
    UINodeId routedNearestRangeInput{};
    bool pointWithinArmedActivatable = false;
    UIPointerRouteInspectionError error = UIPointerRouteInspectionError::None;
};

template <typename IsEnabled>
[[nodiscard]] UIPointerRouteInspection inspectPointerRouteTargets(
    const UIPointerHitTarget& physicalTarget,
    std::span<const u32> routePath,
    std::span<const UICommittedHitEntry> entries,
    UINodeId armedActivatable,
    IsEnabled&& isEnabled)
{
    UIPointerRouteInspection inspection{};
    u32 physicalEntryIndex = physicalTarget.hitEntryIndex;
    usize physicalDepth = 0;
    while (physicalEntryIndex < entries.size())
    {
        const UICommittedHitEntry& physicalEntry = entries[physicalEntryIndex];
        if (physicalEntry.node == armedActivatable)
        {
            inspection.pointWithinArmedActivatable = true;
        }
        if (!inspection.physicalNearestActivatable.hasValue() &&
            isEnabled(physicalEntry.node) &&
            hasBehavior(physicalEntry.behaviors, UIElementBehavior::Activate))
        {
            inspection.physicalNearestActivatable = physicalEntry.node;
        }
        if (physicalEntryIndex == physicalTarget.rootEntryIndex)
        {
            break;
        }
        physicalEntryIndex = physicalEntry.parentEntryIndex;
        if (++physicalDepth > entries.size())
        {
            inspection.error =
                UIPointerRouteInspectionError::PhysicalAncestryCycle;
            return inspection;
        }
    }

    for (const u32 routeEntryIndex : routePath)
    {
        if (routeEntryIndex >= entries.size())
        {
            inspection.error =
                UIPointerRouteInspectionError::InvalidRouteEntryIndex;
            return inspection;
        }
        const UICommittedHitEntry& routeEntry = entries[routeEntryIndex];
        if (!isEnabled(routeEntry.node))
        {
            continue;
        }
        if (!inspection.routedNearestActivatable.hasValue() &&
            hasBehavior(routeEntry.behaviors, UIElementBehavior::Activate))
        {
            inspection.routedNearestActivatable = routeEntry.node;
        }
        if (!inspection.routedNearestRangeInput.hasValue() &&
            hasBehavior(routeEntry.behaviors, UIElementBehavior::RangeInput))
        {
            inspection.routedNearestRangeInput = routeEntry.node;
        }
    }
    return inspection;
}

} // namespace Tina::UI::Detail
