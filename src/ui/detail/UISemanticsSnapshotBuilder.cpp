#include "UISemanticsSnapshotBuilder.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cstring>

namespace Tina::UI::Detail {

UISemanticsSnapshotBuilder::UISemanticsSnapshotBuilder(usize nodeCapacity, usize entryCapacity,
                                                       std::pmr::memory_resource& resource)
    : nodeScratchByIndex_(&resource), entryScratch_(&resource)
{
    nodeScratchByIndex_.resize(nodeCapacity);
    entryScratch_.resize(entryCapacity);
}

Core::Status UISemanticsSnapshotBuilder::build(std::pmr::vector<UISemanticsEntry>& output,
                                               std::pmr::vector<char>& textOutput,
                                               std::span<const UICommittedLayoutEntry> layoutEntries,
                                               UISemanticsSnapshotSourceAdapter sourceAdapter)
{
    output.clear();
    std::fill(entryScratch_.begin(), entryScratch_.end(), EntryScratch{});
    if (sourceAdapter.context == nullptr || sourceAdapter.resolve == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI semantics snapshot source is not configured");
    }

    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        const u32 nodeIndex = layoutEntry.node.index();
        if (nodeIndex >= nodeScratchByIndex_.size())
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "UI semantics node scratch index is out of range");
        }

        UISemanticsSnapshotSource source{};
        if (!sourceAdapter.resolve(sourceAdapter.context, layoutEntry.node, source))
        {
            return Core::failure(UIErrorCode::InvalidNode, "UI semantics layout references a stale node");
        }

        NodeScratch& scratch = nodeScratchByIndex_[nodeIndex];
        scratch = {};
        const NodeScratch* parentScratch =
            source.parentNodeIndex != UISemanticsSnapshotSource::InvalidNodeIndex &&
                    source.parentNodeIndex < nodeScratchByIndex_.size()
                ? &nodeScratchByIndex_[source.parentNodeIndex]
                : nullptr;
        scratch.nearestPublishedEntryIndex =
            parentScratch == nullptr ? InvalidEntryIndex : parentScratch->nearestPublishedEntryIndex;
        scratch.mergeEntryIndex = parentScratch == nullptr ? InvalidEntryIndex : parentScratch->mergeEntryIndex;
        scratch.excluded = layoutEntry.effectiveVisibility != UIVisibility::Visible ||
                           source.mode == UISemanticsMode::Exclude ||
                           (parentScratch != nullptr && parentScratch->excluded);
        if (scratch.excluded)
        {
            continue;
        }

        const bool inheritedMerge = scratch.mergeEntryIndex != InvalidEntryIndex;
        const bool publishes = !inheritedMerge &&
                               (source.mode == UISemanticsMode::Publish ||
                                source.mode == UISemanticsMode::MergeDescendants);
        if (publishes)
        {
            if (output.size() >= entryScratch_.size())
            {
                return Core::failure(UIErrorCode::CapacityExceeded,
                                     "UI committed semantics snapshot capacity has been exhausted");
            }
            const u32 entryIndex = static_cast<u32>(output.size());
            const UINodeId semanticParent =
                scratch.nearestPublishedEntryIndex < output.size()
                    ? output[scratch.nearestPublishedEntryIndex].node
                    : UINodeId{};
            UISemanticsEntry entry = source.entry;
            entry.node = layoutEntry.node;
            entry.parent = semanticParent;
            entry.worldRect = layoutEntry.worldRect;
            entry.name = {};
            entry.description = {};
            entry.valueText = {};
            output.push_back(entry);
            scratch.nearestPublishedEntryIndex = entryIndex;
            if (source.mode == UISemanticsMode::MergeDescendants)
            {
                scratch.mergeEntryIndex = entryIndex;
            }
            scratch.nameTargetEntryIndex = entryIndex;
        } else if (inheritedMerge)
        {
            scratch.nameTargetEntryIndex = scratch.mergeEntryIndex;
        }

        if (!source.name.empty() && scratch.nameTargetEntryIndex < entryScratch_.size())
        {
            EntryScratch& entryScratch = entryScratch_[scratch.nameTargetEntryIndex];
            const usize separatorBytes = entryScratch.hasNamePart ? 1U : 0U;
            if (entryScratch.nameByteCount > (std::numeric_limits<usize>::max)() - separatorBytes ||
                entryScratch.nameByteCount + separatorBytes >
                    (std::numeric_limits<usize>::max)() - source.name.size())
            {
                return Core::failure(UIErrorCode::CapacityExceeded, "UI merged semantics name size overflowed");
            }
            entryScratch.nameByteCount += separatorBytes + source.name.size();
            entryScratch.hasNamePart = true;
        }
    }

    usize textOutputSize = 0;
    for (usize entryIndex = 0; entryIndex < output.size(); ++entryIndex)
    {
        EntryScratch& scratch = entryScratch_[entryIndex];
        if (textOutputSize > textOutput.size() || scratch.nameByteCount > textOutput.size() - textOutputSize)
        {
            return Core::failure(UIErrorCode::CapacityExceeded,
                                 "UI committed semantics text snapshot capacity has been exhausted");
        }
        scratch.nameOffset = textOutputSize;
        scratch.nameBytesWritten = 0;
        if (scratch.nameByteCount != 0)
        {
            output[entryIndex].name = std::string_view(textOutput.data() + textOutputSize, scratch.nameByteCount);
            textOutputSize += scratch.nameByteCount;
        }
    }

    for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
    {
        const u32 nodeIndex = layoutEntry.node.index();
        UISemanticsSnapshotSource source{};
        if (!sourceAdapter.resolve(sourceAdapter.context, layoutEntry.node, source))
        {
            return Core::failure(UIErrorCode::InvalidNode, "UI semantics layout references a stale node");
        }
        const NodeScratch& nodeScratch = nodeScratchByIndex_[nodeIndex];
        if (source.name.empty() || nodeScratch.nameTargetEntryIndex >= output.size())
        {
            continue;
        }
        EntryScratch& entryScratch = entryScratch_[nodeScratch.nameTargetEntryIndex];
        char* destination = textOutput.data() + entryScratch.nameOffset + entryScratch.nameBytesWritten;
        if (entryScratch.nameBytesWritten != 0)
        {
            *destination++ = ' ';
            ++entryScratch.nameBytesWritten;
        }
        std::memcpy(destination, source.name.data(), source.name.size());
        entryScratch.nameBytesWritten += source.name.size();
    }

    const auto copyText = [&](std::string_view source, std::string_view& destination) -> Core::Status {
        destination = {};
        if (source.empty())
        {
            return Core::success();
        }
        if (textOutputSize > textOutput.size() || source.size() > textOutput.size() - textOutputSize)
        {
            return Core::failure(UIErrorCode::CapacityExceeded,
                                 "UI committed semantics text snapshot capacity has been exhausted");
        }
        char* const destinationBytes = textOutput.data() + textOutputSize;
        std::memcpy(destinationBytes, source.data(), source.size());
        destination = std::string_view(destinationBytes, source.size());
        textOutputSize += source.size();
        return Core::success();
    };

    for (UISemanticsEntry& entry : output)
    {
        UISemanticsSnapshotSource source{};
        if (!sourceAdapter.resolve(sourceAdapter.context, entry.node, source))
        {
            return Core::failure(UIErrorCode::InvalidNode, "UI semantics entry references a stale node");
        }
        if (Core::Status status = copyText(source.description, entry.description); !status)
        {
            return status;
        }
        if (Core::Status status = copyText(source.valueText, entry.valueText); !status)
        {
            return status;
        }
    }
    return Core::success();
}

} // namespace Tina::UI::Detail
