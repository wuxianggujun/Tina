#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogChangePlan.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

using Core::u32;

enum class NewEntryState : Core::u8 {
    Unchanged = 0,
    Added = 1,
    Modified = 2,
    Affected = 3,
};

[[nodiscard]] bool dependenciesEqual(const CatalogSnapshot& oldCatalog, u32 oldIndex,
                                     const CatalogSnapshot& newCatalog, u32 newIndex,
                                     u32 dependencyCount) noexcept
{
    for (u32 dependencyIndex = 0; dependencyIndex < dependencyCount; ++dependencyIndex)
    {
        const auto oldDependency = oldCatalog.dependency(oldIndex, dependencyIndex);
        const auto newDependency = newCatalog.dependency(newIndex, dependencyIndex);
        if (!oldDependency || !newDependency || oldDependency->assetId != newDependency->assetId ||
            oldDependency->expectedKind != newDependency->expectedKind ||
            oldDependency->flags != newDependency->flags)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool entriesEqual(const CatalogSnapshot& oldCatalog, u32 oldIndex,
                                const CatalogSnapshot& newCatalog, u32 newIndex,
                                const CatalogEntry& oldEntry, const CatalogEntry& newEntry) noexcept
{
    return oldEntry.contentHash == newEntry.contentHash && oldEntry.assetKind == newEntry.assetKind &&
           oldEntry.assetTypeVersion == newEntry.assetTypeVersion &&
           oldEntry.cookedFileBytes == newEntry.cookedFileBytes &&
           oldEntry.dependencyCount == newEntry.dependencyCount &&
           dependenciesEqual(oldCatalog, oldIndex, newCatalog, newIndex, oldEntry.dependencyCount);
}

[[nodiscard]] Core::Result<CatalogChangePlan>
failureForCapacity(const char* message)
{
    return Core::failure(AssetErrorCode::CatalogCapacityExceeded, message);
}

} // namespace

Core::Result<CatalogChangePlan>
planCatalogChanges(const CatalogSnapshot& oldCatalog, const CatalogSnapshot& newCatalog,
                   CatalogChangePlanConfig config)
{
    if (!oldCatalog || !newCatalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog change plan requires two valid snapshots");
    }
    if (config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog change plan requires memory resource");
    }

    const auto oldEntryCount = oldCatalog.entryCount();
    const auto newEntryCount = newCatalog.entryCount();
    if (oldEntryCount > (std::numeric_limits<u32>::max)() - newEntryCount)
    {
        return failureForCapacity("catalog change count overflow");
    }
    const auto maximumPossibleChanges = oldEntryCount + newEntryCount;
    const auto directReserve = std::min<u32>(config.maxChanges, maximumPossibleChanges);

    try
    {
        std::pmr::vector<CatalogChange> changes{config.memoryResource};
        changes.reserve(directReserve);

        std::pmr::vector<NewEntryState> states{newEntryCount, NewEntryState::Unchanged,
                                               config.memoryResource};

        u32 oldIndex = 0;
        u32 newIndex = 0;
        u32 addedCount = 0;
        u32 removedCount = 0;
        u32 modifiedCount = 0;
        auto appendDirect = [&](Core::AssetId assetId, CatalogChangeKind kind) -> Core::Status {
            if (changes.size() >= config.maxChanges)
            {
                return Core::failure(AssetErrorCode::CatalogCapacityExceeded,
                                     "catalog change plan capacity exceeded");
            }
            changes.push_back(CatalogChange{.assetId = assetId, .kind = kind});
            switch (kind)
            {
            case CatalogChangeKind::Added:
                ++addedCount;
                break;
            case CatalogChangeKind::Removed:
                ++removedCount;
                break;
            case CatalogChangeKind::Modified:
                ++modifiedCount;
                break;
            case CatalogChangeKind::Affected:
                break;
            }
            return Core::success();
        };

        while (oldIndex < oldEntryCount || newIndex < newEntryCount)
        {
            const auto oldEntry = oldIndex < oldEntryCount ? oldCatalog.entry(oldIndex) : std::nullopt;
            const auto newEntry = newIndex < newEntryCount ? newCatalog.entry(newIndex) : std::nullopt;
            if ((oldIndex < oldEntryCount && !oldEntry) || (newIndex < newEntryCount && !newEntry))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "catalog entry missing during change planning");
            }

            if (!oldEntry)
            {
                if (const auto status = appendDirect(newEntry->assetId, CatalogChangeKind::Added); !status)
                {
                    return Core::failure(status.error());
                }
                states[newIndex] = NewEntryState::Added;
                ++newIndex;
                continue;
            }
            if (!newEntry)
            {
                if (const auto status = appendDirect(oldEntry->assetId, CatalogChangeKind::Removed); !status)
                {
                    return Core::failure(status.error());
                }
                ++oldIndex;
                continue;
            }

            if (oldEntry->assetId < newEntry->assetId)
            {
                if (const auto status = appendDirect(oldEntry->assetId, CatalogChangeKind::Removed); !status)
                {
                    return Core::failure(status.error());
                }
                ++oldIndex;
                continue;
            }
            if (newEntry->assetId < oldEntry->assetId)
            {
                if (const auto status = appendDirect(newEntry->assetId, CatalogChangeKind::Added); !status)
                {
                    return Core::failure(status.error());
                }
                states[newIndex] = NewEntryState::Added;
                ++newIndex;
                continue;
            }

            if (!entriesEqual(oldCatalog, oldIndex, newCatalog, newIndex, *oldEntry, *newEntry))
            {
                if (const auto status = appendDirect(newEntry->assetId, CatalogChangeKind::Modified); !status)
                {
                    return Core::failure(status.error());
                }
                states[newIndex] = NewEntryState::Modified;
            }
            ++oldIndex;
            ++newIndex;
        }

        if ((addedCount == 0U && modifiedCount == 0U) || newCatalog.dependencyCount() == 0U)
        {
            return CatalogChangePlan{
                .changes = std::move(changes),
                .addedCount = addedCount,
                .removedCount = removedCount,
                .modifiedCount = modifiedCount,
                .affectedCount = 0,
            };
        }

        std::pmr::vector<u32> reverseCounts{newEntryCount, 0U, config.memoryResource};
        for (u32 entryIndex = 0; entryIndex < newEntryCount; ++entryIndex)
        {
            const auto entry = newCatalog.entry(entryIndex);
            if (!entry)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                     "new catalog entry missing during reverse dependency planning");
            }
            for (u32 dependencyIndex = 0; dependencyIndex < entry->dependencyCount; ++dependencyIndex)
            {
                const auto dependency = newCatalog.dependency(entryIndex, dependencyIndex);
                if (!dependency || dependency->targetEntryIndex >= newEntryCount)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "new catalog dependency missing during reverse dependency planning");
                }
                if (reverseCounts[dependency->targetEntryIndex] == (std::numeric_limits<u32>::max)())
                {
                    return failureForCapacity("reverse dependency count overflow");
                }
                ++reverseCounts[dependency->targetEntryIndex];
            }
        }

        std::pmr::vector<u32> reverseOffsets{newEntryCount + 1U, 0U, config.memoryResource};
        for (u32 entryIndex = 0; entryIndex < newEntryCount; ++entryIndex)
        {
            if (reverseOffsets[entryIndex] > (std::numeric_limits<u32>::max)() - reverseCounts[entryIndex])
            {
                return failureForCapacity("reverse dependency offset overflow");
            }
            reverseOffsets[entryIndex + 1U] = reverseOffsets[entryIndex] + reverseCounts[entryIndex];
        }

        std::pmr::vector<u32> reverseEdges{reverseOffsets.back(), 0U, config.memoryResource};
        // Reuse reverseCounts as write cursors after all per-target counts are consumed.
        for (u32 entryIndex = 0; entryIndex < newEntryCount; ++entryIndex)
        {
            reverseCounts[entryIndex] = reverseOffsets[entryIndex];
        }
        for (u32 entryIndex = 0; entryIndex < newEntryCount; ++entryIndex)
        {
            const auto entry = newCatalog.entry(entryIndex);
            for (u32 dependencyIndex = 0; dependencyIndex < entry->dependencyCount; ++dependencyIndex)
            {
                const auto dependency = newCatalog.dependency(entryIndex, dependencyIndex);
                reverseEdges[reverseCounts[dependency->targetEntryIndex]++] = entryIndex;
            }
        }

        std::pmr::vector<u32> queue{config.memoryResource};
        queue.reserve(newEntryCount);
        for (u32 entryIndex = 0; entryIndex < newEntryCount; ++entryIndex)
        {
            if (states[entryIndex] == NewEntryState::Added || states[entryIndex] == NewEntryState::Modified)
            {
                queue.push_back(entryIndex);
            }
        }

        u32 queueIndex = 0;
        u32 affectedCount = 0;
        while (queueIndex < queue.size())
        {
            const auto changedEntry = queue[queueIndex++];
            for (u32 edgeIndex = reverseOffsets[changedEntry]; edgeIndex < reverseOffsets[changedEntry + 1U];
                 ++edgeIndex)
            {
                const auto dependentEntry = reverseEdges[edgeIndex];
                if (states[dependentEntry] != NewEntryState::Unchanged)
                {
                    continue;
                }
                if (changes.size() >= config.maxChanges)
                {
                    return failureForCapacity("catalog change plan capacity exceeded");
                }
                states[dependentEntry] = NewEntryState::Affected;
                changes.push_back(CatalogChange{
                    .assetId = newCatalog.entry(dependentEntry)->assetId,
                    .kind = CatalogChangeKind::Affected,
                });
                ++affectedCount;
                queue.push_back(dependentEntry);
            }
        }

        std::sort(changes.begin(), changes.end(), [](const CatalogChange& left, const CatalogChange& right) {
            return left.assetId < right.assetId;
        });

        return CatalogChangePlan{
            .changes = std::move(changes),
            .addedCount = addedCount,
            .removedCount = removedCount,
            .modifiedCount = modifiedCount,
            .affectedCount = affectedCount,
        };
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog change plan allocation failed");
    } catch (const std::exception& exception)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, exception.what());
    } catch (...)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog change plan construction failed");
    }
}

} // namespace Tina::Asset
