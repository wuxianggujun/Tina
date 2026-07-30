#include "UIPaintSnapshotBuilder.hpp"

#include <tina/ui/UIErrors.hpp>

#include <limits>

namespace Tina::UI::Detail {

UIPaintSnapshotBuilder::UIPaintSnapshotBuilder(usize entryCapacity) noexcept : entryCapacity_(entryCapacity)
{
}

Core::Result<usize> UIPaintSnapshotBuilder::validateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries,
                                                             const void* sourceContext,
                                                             UIPaintSnapshotSourceAdapter sourceAdapter) const
{
    if (sourceContext == nullptr || sourceAdapter.countEntries == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI paint snapshot count source is not configured");
    }

    usize entryCount = 0;
    bool overflowed = false;
    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
        {
            continue;
        }

        auto nodeEntryCount = sourceAdapter.countEntries(sourceContext, layoutEntry);
        if (!nodeEntryCount)
        {
            return Core::failure(nodeEntryCount.error());
        }
        if (*nodeEntryCount > (std::numeric_limits<usize>::max)() - entryCount)
        {
            overflowed = true;
        } else if (!overflowed)
        {
            entryCount += *nodeEntryCount;
        }
    }

    if (overflowed || entryCount > entryCapacity_)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI committed paint snapshot capacity has been exhausted");
    }
    return entryCount;
}

Core::Status UIPaintSnapshotBuilder::build(std::pmr::vector<UICommittedPaintEntry>& output,
                                           std::span<const UICommittedLayoutEntry> layoutEntries, void* sourceContext,
                                           UIPaintSnapshotSourceAdapter sourceAdapter) const
{
    output.clear();
    if (sourceContext == nullptr || sourceAdapter.appendEntries == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI paint snapshot append source is not configured");
    }

    u32 nextPaintOrdinal = 1;
    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        if (layoutEntry.effectiveVisibility == UIVisibility::Visible)
        {
            sourceAdapter.appendEntries(sourceContext, output, layoutEntry, nextPaintOrdinal);
        }
    }
    return Core::success();
}

} // namespace Tina::UI::Detail
