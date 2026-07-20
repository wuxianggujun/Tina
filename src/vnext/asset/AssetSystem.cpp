#include <tina/asset/AssetSystem.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CookedAssetFile.hpp>

#include <new>
#include <utility>

namespace Tina::Asset {

AssetSystem::AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource) noexcept
    : m_store(std::move(store)), m_batch(batch), m_memoryResource(memoryResource), m_catalogRoot(memoryResource),
      m_index(memoryResource)
{
}

AssetSystem::~AssetSystem() noexcept = default;

AssetSystem::AssetSystem(AssetSystem&&) noexcept = default;

Core::Result<AssetSystem> AssetSystem::Create(AssetSystemConfig config)
{
    if (config.memoryResource == nullptr || config.storeCapacity == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "asset system requires store capacity and memory resource");
    }
    if (config.batch.memoryResource == nullptr)
    {
        config.batch.memoryResource = config.memoryResource;
    }
    if (config.batch.file.memoryResource == nullptr)
    {
        config.batch.file.memoryResource = config.memoryResource;
    }

    auto store = AssetStore::Create(AssetStoreConfig{
        .capacity = config.storeCapacity,
        .memoryResource = config.memoryResource,
    });
    if (!store)
    {
        return Core::failure(std::move(store.error()).withContext("AssetSystem::Create", "store"));
    }
    return AssetSystem(std::move(*store), config.batch, config.memoryResource);
}

Core::Status AssetSystem::bindCatalog(std::string_view catalogRootUtf8, CatalogSnapshot catalog)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (catalogRootUtf8.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    try
    {
        m_catalogRoot.assign(catalogRootUtf8.begin(), catalogRootUtf8.end());
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog root allocation failed");
    }
    m_catalog = std::move(catalog);
    return Core::success();
}

bool AssetSystem::hasCatalog() const noexcept
{
    return static_cast<bool>(m_catalog);
}

const CatalogSnapshot* AssetSystem::catalog() const noexcept
{
    return m_catalog ? &m_catalog : nullptr;
}

std::string_view AssetSystem::catalogRoot() const noexcept
{
    return m_catalogRoot;
}

AssetStore& AssetSystem::store() noexcept
{
    return m_store;
}

const AssetStore& AssetSystem::store() const noexcept
{
    return m_store;
}

std::optional<AssetHandle> AssetSystem::find(Core::AssetId assetId) const noexcept
{
    const auto index = findIndex(assetId);
    if (!index)
    {
        return std::nullopt;
    }
    const auto handle = m_index[*index].handle;
    if (m_store.tryGet(handle) == nullptr)
    {
        return std::nullopt;
    }
    return handle;
}

Core::Result<std::pmr::vector<AssetHandle>>
AssetSystem::load(std::span<const Core::AssetId> requestedAssetIds)
{
    if (!m_catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset system has no bound catalog");
    }

    Core::Result<std::pmr::vector<CatalogLoadPlanEntry>> plan =
        Core::failure(AssetErrorCode::InvalidCatalogConfig, "plan not computed");
    if (requestedAssetIds.empty())
    {
        plan = planCatalogLoadsAll(m_catalog, CatalogLoadPlanConfig{.memoryResource = m_memoryResource});
    } else
    {
        plan = planCatalogLoads(m_catalog, requestedAssetIds, CatalogLoadPlanConfig{.memoryResource = m_memoryResource});
    }
    if (!plan)
    {
        return Core::failure(std::move(plan.error()).withContext("AssetSystem::load", "plan"));
    }

    // Budget applies to the dependency-expanded plan of this request (including already-resident
    // assets). Callers that only want incremental bytes should use maxTotalCookedFileBytes=0 and
    // separate accounting. Product path uses full plan size for deterministic fail-before-IO.
    if (m_batch.maxTotalCookedFileBytes != 0U)
    {
        auto total = totalCookedFileBytes(*plan);
        if (!total)
        {
            return Core::failure(std::move(total.error()).withContext("AssetSystem::load", "budget"));
        }
        if (*total > m_batch.maxTotalCookedFileBytes)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "asset system load exceeds maxTotalCookedFileBytes budget");
        }
    }

    std::pmr::vector<AssetHandle> publishedThisCall{m_memoryResource};
    auto rollback = [&]() noexcept {
        for (const auto handle : publishedThisCall)
        {
            (void)m_store.unload(handle);
            forgetHandle(handle);
        }
        publishedThisCall.clear();
    };

    try
    {
        publishedThisCall.reserve(plan->size());
        for (const auto& row : *plan)
        {
            if (find(row.assetId))
            {
                continue;
            }

            auto cooked = loadCookedAssetFromCatalog(m_catalogRoot, m_catalog, row.assetId, m_batch.file);
            if (!cooked)
            {
                rollback();
                return Core::failure(std::move(cooked.error()).withContext("AssetSystem::load", "cooked"));
            }
            auto handle = m_store.publish(std::move(*cooked));
            if (!handle)
            {
                rollback();
                return Core::failure(std::move(handle.error()).withContext("AssetSystem::load", "publish"));
            }
            if (const auto status = insertIndex(row.assetId, *handle); !status)
            {
                (void)m_store.unload(*handle);
                rollback();
                return Core::failure(std::move(status.error()));
            }
            publishedThisCall.push_back(*handle);
        }

        std::pmr::vector<AssetHandle> result{m_memoryResource};
        if (requestedAssetIds.empty())
        {
            result.reserve(plan->size());
            for (const auto& row : *plan)
            {
                const auto handle = find(row.assetId);
                if (!handle)
                {
                    rollback();
                    return Core::failure(AssetErrorCode::InvalidHandle, "loaded asset missing from index");
                }
                result.push_back(*handle);
            }
        } else
        {
            result.reserve(requestedAssetIds.size());
            for (const auto assetId : requestedAssetIds)
            {
                const auto handle = find(assetId);
                if (!handle)
                {
                    rollback();
                    return Core::failure(AssetErrorCode::InvalidHandle, "requested asset missing after load");
                }
                result.push_back(*handle);
            }
        }
        return result;
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(AssetErrorCode::AllocationFailed, "asset system load allocation failed");
    }
}

Core::Result<AssetHandle> AssetSystem::loadOne(Core::AssetId assetId)
{
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset id is invalid");
    }
    auto handles = load(std::span<const Core::AssetId>(&assetId, 1U));
    if (!handles)
    {
        return Core::failure(std::move(handles.error()));
    }
    if (handles->size() != 1U)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "expected exactly one loaded handle");
    }
    return (*handles)[0];
}

const CookedAssetFile* AssetSystem::tryGet(AssetHandle handle) const noexcept
{
    return m_store.tryGet(handle);
}

Core::Result<AssetLease> AssetSystem::acquire(AssetHandle handle)
{
    return m_store.acquire(handle);
}

Core::Status AssetSystem::unload(AssetHandle handle) noexcept
{
    const auto status = m_store.unload(handle);
    if (status && m_store.tryGet(handle) == nullptr)
    {
        forgetHandle(handle);
    }
    return status;
}

void AssetSystem::forgetHandle(AssetHandle handle) noexcept
{
    for (Core::u32 index = 0; index < static_cast<Core::u32>(m_index.size()); ++index)
    {
        if (m_index[index].handle == handle)
        {
            eraseIndexAt(index);
            return;
        }
    }
}

std::optional<Core::u32> AssetSystem::findIndex(Core::AssetId assetId) const noexcept
{
    if (!assetId || m_index.empty())
    {
        return std::nullopt;
    }
    Core::u32 begin = 0;
    Core::u32 end = static_cast<Core::u32>(m_index.size());
    while (begin < end)
    {
        const auto middle = begin + (end - begin) / 2U;
        if (m_index[middle].assetId < assetId)
        {
            begin = middle + 1U;
        } else
        {
            end = middle;
        }
    }
    if (begin >= m_index.size() || m_index[begin].assetId != assetId)
    {
        return std::nullopt;
    }
    return begin;
}

Core::Status AssetSystem::insertIndex(Core::AssetId assetId, AssetHandle handle)
{
    try
    {
        const auto existing = findIndex(assetId);
        if (existing)
        {
            m_index[*existing].handle = handle;
            return Core::success();
        }
        IndexEntry entry{.assetId = assetId, .handle = handle};
        auto it = m_index.begin();
        while (it != m_index.end() && it->assetId < assetId)
        {
            ++it;
        }
        m_index.insert(it, entry);
        return Core::success();
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "asset id index allocation failed");
    }
}

void AssetSystem::eraseIndexAt(Core::u32 index) noexcept
{
    if (index >= m_index.size())
    {
        return;
    }
    m_index.erase(m_index.begin() + static_cast<std::ptrdiff_t>(index));
}

} // namespace Tina::Asset
