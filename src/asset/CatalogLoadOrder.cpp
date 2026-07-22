#include <tina/asset/CatalogLoadOrder.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

enum class VisitState : Core::u8 {
    Unvisited = 0,
    Visiting = 1,
    Done = 2,
};

struct StackFrame final {
    Core::u32 entryIndex = 0;
    Core::u32 nextDependency = 0;
};

} // namespace

Core::Result<std::pmr::vector<Core::u32>>
computeCatalogLoadOrder(const CatalogSnapshot& catalog, std::span<const Core::AssetId> requestedAssetIds,
                        CatalogLoadOrderConfig config)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "load order requires memory resource");
    }

    const auto entryCount = catalog.entryCount();
    if (entryCount == 0U)
    {
        if (requestedAssetIds.empty())
        {
            return std::pmr::vector<Core::u32>{config.memoryResource};
        }
        return Core::failure(Core::CoreErrorCode::NotFound, "requested asset not present in empty catalog");
    }

    try
    {
        std::pmr::vector<VisitState> state{entryCount, VisitState::Unvisited, config.memoryResource};
        std::pmr::vector<StackFrame> stack{config.memoryResource};
        stack.reserve(entryCount);
        std::pmr::vector<Core::u32> order{config.memoryResource};
        order.reserve(entryCount);

        auto pushIfNeeded = [&](Core::u32 entryIndex) -> Core::Status {
            if (state[entryIndex] == VisitState::Done)
            {
                return Core::success();
            }
            if (state[entryIndex] == VisitState::Visiting)
            {
                // CatalogSnapshot Create already rejects cycles; defensive path.
                return Core::failure(AssetErrorCode::DependencyCycle, "catalog load order encountered a cycle");
            }
            state[entryIndex] = VisitState::Visiting;
            stack.push_back(StackFrame{.entryIndex = entryIndex, .nextDependency = 0});
            return Core::success();
        };

        for (const auto assetId : requestedAssetIds)
        {
            if (!assetId)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "requested asset id is invalid");
            }
            const auto found = catalog.find(assetId);
            if (!found)
            {
                return Core::failure(Core::CoreErrorCode::NotFound, "requested asset id is not in catalog");
            }
            if (const auto status = pushIfNeeded(*found); !status)
            {
                return Core::failure(std::move(status.error()));
            }

            while (!stack.empty())
            {
                auto& frame = stack.back();
                const auto entry = catalog.entry(frame.entryIndex);
                if (!entry)
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry missing during load order");
                }

                if (frame.nextDependency < entry->dependencyCount)
                {
                    const auto dependency = catalog.dependency(frame.entryIndex, frame.nextDependency);
                    ++frame.nextDependency;
                    if (!dependency)
                    {
                        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                             "catalog dependency missing during load order");
                    }
                    if (const auto status = pushIfNeeded(dependency->targetEntryIndex); !status)
                    {
                        return Core::failure(std::move(status.error()));
                    }
                    continue;
                }

                state[frame.entryIndex] = VisitState::Done;
                order.push_back(frame.entryIndex);
                stack.pop_back();
            }
        }

        return order;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog load order allocation failed");
    }
}

} // namespace Tina::Asset
