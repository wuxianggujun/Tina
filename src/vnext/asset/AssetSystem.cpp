#include <tina/asset/AssetSystem.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CookedAssetFile.hpp>

#include <new>
#include <utility>

namespace Tina::Asset {

AssetSystem::AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                         Core::usize queueCapacity, Core::u32 defaultPumpBudget) noexcept
    : m_store(std::move(store)), m_batch(batch), m_memoryResource(memoryResource), m_queueCapacity(queueCapacity),
      m_defaultPumpBudget(defaultPumpBudget), m_catalogRoot(memoryResource), m_index(memoryResource),
      m_queue(memoryResource)
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
    if (config.queueCapacity == 0)
    {
        config.queueCapacity = config.storeCapacity;
    }

    auto store = AssetStore::Create(AssetStoreConfig{
        .capacity = config.storeCapacity,
        .memoryResource = config.memoryResource,
    });
    if (!store)
    {
        return Core::failure(std::move(store.error()).withContext("AssetSystem::Create", "store"));
    }
    return AssetSystem(std::move(*store), config.batch, config.memoryResource, config.queueCapacity,
                       config.defaultPumpBudget);
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

Core::u32 AssetSystem::pendingCount() const noexcept
{
    return static_cast<Core::u32>(m_queue.size());
}

std::optional<AssetHandle> AssetSystem::find(Core::AssetId assetId) const noexcept
{
    const auto index = findIndex(assetId);
    if (!index)
    {
        return std::nullopt;
    }
    const auto handle = m_index[*index].handle;
    // Keep find() working for in-flight Queued/Loading/Failed slots (handle still valid).
    if (!handle)
    {
        return std::nullopt;
    }
    if (m_store.state(handle) == AssetLogicalState::Unloaded)
    {
        return std::nullopt;
    }
    return handle;
}

Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
AssetSystem::planForRequest(std::span<const Core::AssetId> requestedAssetIds)
{
    if (!m_catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset system has no bound catalog");
    }
    if (requestedAssetIds.empty())
    {
        return planCatalogLoadsAll(m_catalog, CatalogLoadPlanConfig{.memoryResource = m_memoryResource});
    }
    return planCatalogLoads(m_catalog, requestedAssetIds, CatalogLoadPlanConfig{.memoryResource = m_memoryResource});
}

Core::Result<std::pmr::vector<AssetHandle>>
AssetSystem::load(std::span<const Core::AssetId> requestedAssetIds)
{
    auto plan = planForRequest(requestedAssetIds);
    if (!plan)
    {
        return Core::failure(std::move(plan.error()).withContext("AssetSystem::load", "plan"));
    }

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
            if (const auto existing = find(row.assetId))
            {
                if (m_store.state(*existing) == AssetLogicalState::Ready ||
                    m_store.state(*existing) == AssetLogicalState::UnloadPending)
                {
                    continue;
                }
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

Core::Result<AssetHandle> AssetSystem::ensureQueued(const CatalogLoadPlanEntry& row)
{
    if (const auto existing = find(row.assetId))
    {
        const auto st = m_store.state(*existing);
        if (st == AssetLogicalState::Ready || st == AssetLogicalState::UnloadPending ||
            st == AssetLogicalState::Queued || st == AssetLogicalState::Loading || st == AssetLogicalState::Failed)
        {
            return *existing;
        }
    }

    if (m_queue.size() >= m_queueCapacity)
    {
        return Core::failure(AssetErrorCode::AssetQueueFull, "asset completion queue is full");
    }

    auto handle = m_store.beginQueued(row.assetId, row.assetKind);
    if (!handle)
    {
        return Core::failure(std::move(handle.error()).withContext("AssetSystem::request", "beginQueued"));
    }
    if (const auto status = insertIndex(row.assetId, *handle); !status)
    {
        (void)m_store.unload(*handle);
        return Core::failure(std::move(status.error()));
    }
    try
    {
        m_queue.push_back(WorkItem{.handle = *handle, .assetId = row.assetId});
    } catch (const std::bad_alloc&)
    {
        (void)m_store.unload(*handle);
        forgetHandle(*handle);
        return Core::failure(AssetErrorCode::AllocationFailed, "asset queue allocation failed");
    }
    return *handle;
}

Core::Result<std::pmr::vector<AssetHandle>>
AssetSystem::request(std::span<const Core::AssetId> requestedAssetIds)
{
    auto plan = planForRequest(requestedAssetIds);
    if (!plan)
    {
        return Core::failure(std::move(plan.error()).withContext("AssetSystem::request", "plan"));
    }

    if (m_batch.maxTotalCookedFileBytes != 0U)
    {
        auto total = totalCookedFileBytes(*plan);
        if (!total)
        {
            return Core::failure(std::move(total.error()).withContext("AssetSystem::request", "budget"));
        }
        if (*total > m_batch.maxTotalCookedFileBytes)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "asset system request exceeds maxTotalCookedFileBytes budget");
        }
    }

    std::pmr::vector<AssetHandle> queuedThisCall{m_memoryResource};
    auto rollback = [&]() noexcept {
        for (const auto handle : queuedThisCall)
        {
            (void)m_store.unload(handle);
            forgetHandle(handle);
            // Drop matching queue tails created by this call.
            for (auto it = m_queue.begin(); it != m_queue.end();)
            {
                if (it->handle == handle)
                {
                    it = m_queue.erase(it);
                } else
                {
                    ++it;
                }
            }
        }
        queuedThisCall.clear();
    };

    try
    {
        for (const auto& row : *plan)
        {
            const auto before = find(row.assetId);
            auto handle = ensureQueued(row);
            if (!handle)
            {
                rollback();
                return Core::failure(std::move(handle.error()));
            }
            if (!before || m_store.state(*handle) == AssetLogicalState::Queued)
            {
                // Track only newly queued handles for rollback (not already Ready).
                if (!before)
                {
                    queuedThisCall.push_back(*handle);
                } else if (m_store.state(*before) == AssetLogicalState::Queued &&
                           std::find_if(queuedThisCall.begin(), queuedThisCall.end(),
                                        [&](AssetHandle h) { return h == *handle; }) == queuedThisCall.end())
                {
                    // already queued previously — not this call
                }
            }
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
                    return Core::failure(AssetErrorCode::InvalidHandle, "requested plan asset missing from index");
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
                    return Core::failure(AssetErrorCode::InvalidHandle, "requested asset missing after request");
                }
                result.push_back(*handle);
            }
        }
        return result;
    } catch (const std::bad_alloc&)
    {
        rollback();
        return Core::failure(AssetErrorCode::AllocationFailed, "asset system request allocation failed");
    }
}

Core::Result<AssetHandle> AssetSystem::requestOne(Core::AssetId assetId)
{
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset id is invalid");
    }
    auto handles = request(std::span<const Core::AssetId>(&assetId, 1U));
    if (!handles)
    {
        return Core::failure(std::move(handles.error()));
    }
    if (handles->size() != 1U)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "expected exactly one requested handle");
    }
    return (*handles)[0];
}

Core::Result<AssetPumpStats> AssetSystem::pump(Core::u32 budget)
{
    Core::u32 limit = budget;
    if (limit == 0U)
    {
        limit = m_defaultPumpBudget;
    }
    if (limit == 0U)
    {
        limit = static_cast<Core::u32>(m_queue.size());
    }

    AssetPumpStats stats{};
    while (stats.processed < limit && !m_queue.empty())
    {
        const auto item = m_queue.front();
        m_queue.erase(m_queue.begin());
        ++stats.processed;

        if (m_store.state(item.handle) != AssetLogicalState::Queued)
        {
            // Already advanced/cancelled; skip.
            continue;
        }
        auto markStatus = m_store.markLoading(item.handle);
        if (!markStatus)
        {
            return Core::failure(std::move(markStatus.error()).withContext("AssetSystem::pump", "markLoading"));
        }

        auto cooked = loadCookedAssetFromCatalog(m_catalogRoot, m_catalog, item.assetId, m_batch.file);
        if (!cooked)
        {
            auto failStatus = m_store.fail(item.handle);
            if (!failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext("AssetSystem::pump", "fail"));
            }
            ++stats.becameFailed;
            continue;
        }
        auto completeStatus = m_store.complete(item.handle, std::move(*cooked));
        if (!completeStatus)
        {
            return Core::failure(std::move(completeStatus.error()).withContext("AssetSystem::pump", "complete"));
        }
        ++stats.becameReady;
    }
    stats.remaining = static_cast<Core::u32>(m_queue.size());
    return stats;
}

const CookedAssetFile* AssetSystem::tryGet(AssetHandle handle) const noexcept
{
    return m_store.tryGet(handle);
}

AssetLogicalState AssetSystem::state(AssetHandle handle) const noexcept
{
    return m_store.state(handle);
}

Core::Result<AssetLease> AssetSystem::acquire(AssetHandle handle)
{
    return m_store.acquire(handle);
}

Core::Status AssetSystem::unload(AssetHandle handle) noexcept
{
    // Remove pending queue work for this handle first.
    for (auto it = m_queue.begin(); it != m_queue.end();)
    {
        if (it->handle == handle)
        {
            it = m_queue.erase(it);
        } else
        {
            ++it;
        }
    }
    const auto status = m_store.unload(handle);
    if (status && m_store.state(handle) == AssetLogicalState::Unloaded)
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
