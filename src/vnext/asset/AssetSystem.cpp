#include <tina/asset/AssetSystem.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/task/TaskErrors.hpp>

#include <filesystem>
#include <new>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool hasPathEscapeComponent(const std::filesystem::path& relative) noexcept
{
    for (const auto& part : relative)
    {
        if (part == "..")
        {
            return true;
        }
    }
    return false;
}

} // namespace

AssetSystem::AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                         Core::usize queueCapacity, Core::u32 defaultPumpBudget, Task::ITaskSystem* taskSystem,
                         Render::NullUploadLedger* uploadLedger, AssetGpuUploadConfig gpuUploadConfig,
                         bool autoGpuUpload)
    : m_store(std::move(store)), m_batch(batch), m_memoryResource(memoryResource), m_queueCapacity(queueCapacity),
      m_defaultPumpBudget(defaultPumpBudget), m_taskSystem(taskSystem), m_uploadLedger(uploadLedger),
      m_gpuUploadConfig(gpuUploadConfig), m_autoGpuUpload(autoGpuUpload), m_catalogRoot(memoryResource),
      m_index(memoryResource), m_queue(memoryResource)
{
    if (m_uploadLedger != nullptr)
    {
        m_gpuUpload =
            std::make_unique<AssetGpuUploadCoordinator>(m_store, *m_uploadLedger, m_gpuUploadConfig, &m_retirement);
    }
}

AssetSystem::~AssetSystem() noexcept = default;

AssetSystem::AssetSystem(AssetSystem&& other) noexcept
    : m_store(std::move(other.m_store)), m_batch(other.m_batch), m_memoryResource(other.m_memoryResource),
      m_queueCapacity(other.m_queueCapacity), m_defaultPumpBudget(other.m_defaultPumpBudget),
      m_taskSystem(other.m_taskSystem), m_uploadLedger(other.m_uploadLedger), m_gpuUploadConfig(other.m_gpuUploadConfig),
      m_autoGpuUpload(other.m_autoGpuUpload), m_catalog(std::move(other.m_catalog)),
      m_catalogRoot(std::move(other.m_catalogRoot)), m_index(std::move(other.m_index)),
      m_queue(std::move(other.m_queue)), m_inFlight(other.m_inFlight.load(std::memory_order_relaxed))
{
    // Rebuild coordinator against this->m_store and this->m_retirement.
    other.m_gpuUpload.reset();
    if (m_uploadLedger != nullptr)
    {
        m_gpuUpload =
            std::make_unique<AssetGpuUploadCoordinator>(m_store, *m_uploadLedger, m_gpuUploadConfig, &m_retirement);
    }
    other.m_memoryResource = nullptr;
    other.m_taskSystem = nullptr;
    other.m_uploadLedger = nullptr;
    other.m_queueCapacity = 0;
    other.m_defaultPumpBudget = 0;
    other.m_autoGpuUpload = true;
    other.m_inFlight.store(0, std::memory_order_relaxed);
}

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

    try
    {
        return AssetSystem(std::move(*store), config.batch, config.memoryResource, config.queueCapacity,
                           config.defaultPumpBudget, config.taskSystem, config.uploadLedger, config.gpuUpload,
                           config.autoGpuUpload);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "asset system construction failed");
    }
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

Core::Status AssetSystem::openAndBindCatalog(std::string_view catalogRootUtf8, CatalogPackageOpenConfig openConfig)
{
    if (m_memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset system has no memory resource");
    }
    if (openConfig.manifest.catalog.memoryResource == nullptr)
    {
        openConfig.manifest.catalog.memoryResource = m_memoryResource;
    }
    // Provide sane defaults when caller left zeros (common for {} openConfig).
    if (openConfig.manifest.catalog.maxEntries == 0)
    {
        openConfig.manifest.catalog.maxEntries = 1024;
    }
    if (openConfig.manifest.catalog.maxDependencies == 0)
    {
        openConfig.manifest.catalog.maxDependencies = 4096;
    }
    if (openConfig.manifest.catalog.maxDependenciesPerAsset == 0)
    {
        openConfig.manifest.catalog.maxDependenciesPerAsset = 64;
    }
    if (openConfig.validation.file.memoryResource == nullptr)
    {
        openConfig.validation.file.memoryResource = m_memoryResource;
    }
    auto catalog = openCatalogPackage(catalogRootUtf8, openConfig);
    if (!catalog)
    {
        return Core::failure(std::move(catalog.error()).withContext("AssetSystem::openAndBindCatalog", "open"));
    }
    return bindCatalog(catalogRootUtf8, std::move(*catalog));
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

Core::u32 AssetSystem::inFlightCount() const noexcept
{
    return m_inFlight.load(std::memory_order_acquire);
}

bool AssetSystem::hasGpuUpload() const noexcept
{
    return m_gpuUpload != nullptr;
}

const AssetRetirementLedger& AssetSystem::retirement() const noexcept
{
    return m_retirement;
}

AssetRetirementStats AssetSystem::retirementStats() const noexcept
{
    return m_retirement.stats();
}

std::optional<AssetHandle> AssetSystem::find(Core::AssetId assetId) const noexcept
{
    const auto index = findIndex(assetId);
    if (!index)
    {
        return std::nullopt;
    }
    const auto handle = m_index[*index].handle;
    if (!handle || m_store.state(handle) == AssetLogicalState::Unloaded)
    {
        return std::nullopt;
    }
    return handle;
}

std::optional<Core::AssetId> AssetSystem::catalogFirstIdOfKind(AssetFormat::AssetKind kind) const noexcept
{
    if (!m_catalog)
    {
        return std::nullopt;
    }
    for (Core::u32 index = 0; index < m_catalog.entryCount(); ++index)
    {
        const auto entry = m_catalog.entry(index);
        if (entry && entry->assetKind == kind)
        {
            return entry->assetId;
        }
    }
    return std::nullopt;
}

std::optional<AssetHandle> AssetSystem::findFirstLoadedOfKind(AssetFormat::AssetKind kind) const noexcept
{
    if (!m_catalog)
    {
        return std::nullopt;
    }
    for (Core::u32 index = 0; index < m_catalog.entryCount(); ++index)
    {
        const auto entry = m_catalog.entry(index);
        if (!entry || entry->assetKind != kind)
        {
            continue;
        }
        if (auto handle = find(entry->assetId))
        {
            return handle;
        }
    }
    return std::nullopt;
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

Core::Result<std::string> AssetSystem::resolveObjectPath(Core::AssetId assetId, AssetFormat::AssetKind kind) const
{
    auto artifactPath = AssetFormat::makeCookedArtifactPath(kind, assetId);
    if (!artifactPath)
    {
        return Core::failure(std::move(artifactPath.error()).withContext("AssetSystem", "artifactPath"));
    }
    const auto root = std::filesystem::u8path(std::string_view(m_catalogRoot));
    const auto relative = std::filesystem::u8path(artifactPath->view());
    if (relative.is_absolute() || hasPathEscapeComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "artifact relative path is not safe");
    }
    const auto fullPath = root / relative;
    const auto generic = fullPath.generic_u8string();
    return std::string(generic.begin(), generic.end());
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
                const auto st = m_store.state(*existing);
                if (st == AssetLogicalState::ReadyCpu || st == AssetLogicalState::UploadQueued ||
                    st == AssetLogicalState::ReadyGpu || st == AssetLogicalState::UnloadPending)
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
            noteReadyCpu(*handle);
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

        // Advance Null GPU path immediately for sync load when configured.
        if (m_gpuUpload != nullptr)
        {
            auto gpu = m_gpuUpload->pumpUploads();
            if (!gpu)
            {
                rollback();
                return Core::failure(std::move(gpu.error()).withContext("AssetSystem::load", "pumpUploads"));
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
        if (st == AssetLogicalState::ReadyCpu || st == AssetLogicalState::UploadQueued ||
            st == AssetLogicalState::ReadyGpu || st == AssetLogicalState::UnloadPending ||
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
        m_queue.push_back(WorkItem{.handle = *handle, .assetId = row.assetId, .assetKind = row.assetKind});
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
            if (!before)
            {
                queuedThisCall.push_back(*handle);
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

Core::Result<AssetPumpStats> AssetSystem::pumpSync(Core::u32 limit)
{
    AssetPumpStats stats{};
    while (stats.processed < limit && !m_queue.empty())
    {
        const auto item = m_queue.front();
        m_queue.erase(m_queue.begin());
        ++stats.processed;

        if (m_store.state(item.handle) != AssetLogicalState::Queued)
        {
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
        noteReadyCpu(item.handle);
        ++stats.becameReady;
    }
    if (const auto status = mergeGpuStats(stats); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    stats.remaining = static_cast<Core::u32>(m_queue.size());
    stats.inFlight = m_inFlight.load(std::memory_order_acquire);
    return stats;
}

Core::Result<AssetPumpStats> AssetSystem::pumpAsync(Core::u32 limit)
{
    AssetPumpStats stats{};

    // 1) Drain completed Main work first so Ready is visible this frame.
    auto mainResult = m_taskSystem->pumpMain(limit);
    if (!mainResult)
    {
        return Core::failure(std::move(mainResult.error()).withContext("AssetSystem::pump", "pumpMain"));
    }
    stats.mainCompletions = *mainResult;

    // 2) Dispatch up to remaining budget items to IO workers.
    Core::u32 dispatchBudget = limit;
    if (dispatchBudget == 0U)
    {
        dispatchBudget = static_cast<Core::u32>(m_queue.size());
    }
    while (stats.dispatchedIo < dispatchBudget && !m_queue.empty())
    {
        const auto item = m_queue.front();
        m_queue.erase(m_queue.begin());
        ++stats.processed;

        if (m_store.state(item.handle) != AssetLogicalState::Queued)
        {
            continue;
        }
        auto markStatus = m_store.markLoading(item.handle);
        if (!markStatus)
        {
            return Core::failure(std::move(markStatus.error()).withContext("AssetSystem::pump", "markLoading"));
        }

        auto pathResult = resolveObjectPath(item.assetId, item.assetKind);
        if (!pathResult)
        {
            auto failStatus = m_store.fail(item.handle);
            if (!failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext("AssetSystem::pump", "fail"));
            }
            ++stats.becameFailed;
            continue;
        }

        // Capture values for worker. Use heap-backed path string for thread safety.
        const auto handle = item.handle;
        const auto assetId = item.assetId;
        const auto path = *pathResult;
        const auto maxBytes = m_batch.file.maxFileBytes;
        auto* memoryResource = m_memoryResource;
        auto* self = this;

        m_inFlight.fetch_add(1U, std::memory_order_acq_rel);
        auto scheduleStatus = m_taskSystem->scheduleIo([self, handle, assetId, path, maxBytes, memoryResource]() {
            Core::ReadFileConfig readConfig{.maxBytes = maxBytes, .memoryResource = memoryResource};
            auto bytes = Core::readFile(path, readConfig);
            std::pmr::vector<std::byte> payload{memoryResource};
            bool ok = false;
            if (bytes)
            {
                try
                {
                    payload = std::move(*bytes);
                    ok = true;
                } catch (...)
                {
                    ok = false;
                }
            }
            // Main completion posts owning bytes; parse happens on main.
            (void)self->m_taskSystem->postMain([self, handle, assetId, payload = std::move(payload), ok]() mutable {
                self->completeOnMain(handle, assetId, std::move(payload), ok, nullptr);
                self->m_inFlight.fetch_sub(1U, std::memory_order_acq_rel);
            });
        });
        if (!scheduleStatus)
        {
            m_inFlight.fetch_sub(1U, std::memory_order_acq_rel);
            auto failStatus = m_store.fail(item.handle);
            if (!failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext("AssetSystem::pump", "failAfterQueue"));
            }
            ++stats.becameFailed;
            // If IO queue is full, stop dispatching more this frame.
            if (scheduleStatus.error().code == Task::TaskErrorCode::QueueFull)
            {
                break;
            }
            continue;
        }
        ++stats.dispatchedIo;
    }

    // 3) Drain any completions that arrived during dispatch.
    auto mainResult2 = m_taskSystem->pumpMain(0);
    if (!mainResult2)
    {
        return Core::failure(std::move(mainResult2.error()).withContext("AssetSystem::pump", "pumpMain2"));
    }
    stats.mainCompletions += *mainResult2;

    if (const auto status = mergeGpuStats(stats); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    stats.remaining = static_cast<Core::u32>(m_queue.size());
    stats.inFlight = m_inFlight.load(std::memory_order_acquire);
    return stats;
}

void AssetSystem::completeOnMain(AssetHandle handle, Core::AssetId assetId, std::pmr::vector<std::byte> bytes, bool ok,
                                 AssetPumpStats* stats) noexcept
{
    static_cast<void>(stats);
    if (m_store.state(handle) != AssetLogicalState::Loading)
    {
        return;
    }
    if (!ok)
    {
        (void)m_store.fail(handle);
        return;
    }
    auto cookedResult = makeCookedAssetFileFromBytes(std::move(bytes), m_batch.file);
    if (!cookedResult)
    {
        (void)m_store.fail(handle);
        return;
    }
    if (cookedResult->header().assetId != assetId)
    {
        (void)m_store.fail(handle);
        return;
    }
    if (m_catalog)
    {
        const auto entryIndex = m_catalog.find(assetId);
        if (!entryIndex)
        {
            (void)m_store.fail(handle);
            return;
        }
        const auto entry = m_catalog.entry(*entryIndex);
        if (!entry || entry->assetKind != cookedResult->header().assetKind ||
            entry->contentHash != cookedResult->header().contentHash ||
            entry->cookedFileBytes != cookedResult->header().fileBytes)
        {
            (void)m_store.fail(handle);
            return;
        }
    }
    if (!m_store.complete(handle, std::move(*cookedResult)))
    {
        return;
    }
    noteReadyCpu(handle);
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
        limit = static_cast<Core::u32>(m_queue.size()) + m_inFlight.load(std::memory_order_acquire);
    }
    if (m_taskSystem == nullptr)
    {
        return pumpSync(limit);
    }
    return pumpAsync(limit);
}

Core::Status AssetSystem::trackForGpuUpload(AssetHandle handle)
{
    if (m_gpuUpload == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset system has no upload ledger configured");
    }
    return m_gpuUpload->track(handle);
}

Core::Result<AssetGpuUploadStats> AssetSystem::pumpGpuUploads()
{
    if (m_gpuUpload == nullptr)
    {
        return AssetGpuUploadStats{};
    }
    return m_gpuUpload->pumpUploads();
}

void AssetSystem::noteReadyCpu(AssetHandle handle) noexcept
{
    if (!m_autoGpuUpload || m_gpuUpload == nullptr || !handle)
    {
        return;
    }
    (void)m_gpuUpload->track(handle);
}

Core::Status AssetSystem::mergeGpuStats(AssetPumpStats& stats) noexcept
{
    if (m_gpuUpload == nullptr)
    {
        return Core::success();
    }
    auto gpu = m_gpuUpload->pumpUploads();
    if (!gpu)
    {
        return Core::failure(std::move(gpu.error()).withContext("AssetSystem::pump", "pumpUploads"));
    }
    stats.gpuSubmitted += gpu->submitted;
    stats.becameGpuReady += gpu->becameGpuReady;
    stats.gpuFailed += gpu->failed;
    return Core::success();
}

const CookedAssetFile* AssetSystem::tryGet(AssetHandle handle) const noexcept
{
    return m_store.tryGet(handle);
}

AssetLogicalState AssetSystem::state(AssetHandle handle) const noexcept
{
    return m_store.state(handle);
}

bool AssetSystem::isGpuReady(AssetHandle handle) const noexcept
{
    return m_store.isGpuReady(handle);
}

Core::Result<AssetLease> AssetSystem::acquire(AssetHandle handle)
{
    return m_store.acquire(handle);
}

Core::Status AssetSystem::unload(AssetHandle handle) noexcept
{
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

    // Retire outstanding upload staging before/while logical unload.
    if (m_gpuUpload != nullptr)
    {
        (void)m_gpuUpload->cancelUpload(handle);
    } else
    {
        m_retirement.enqueueDestroy(handle, m_store.assetId(handle), {});
        m_retirement.markReleased(handle);
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
