#include <tina/asset/AssetSystem.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/task/TaskErrors.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>

namespace Tina::Asset {

struct AssetSystem::AsyncRequestState final {
    enum class Outcome : Core::u8 {
        Reading = 0,
        Succeeded = 1,
        Failed = 2,
    };

    AssetHandle handle{};
    Core::AssetId assetId{};
    std::pmr::vector<std::byte> bytes{std::pmr::new_delete_resource()};
    std::atomic<Outcome> outcome{Outcome::Reading};
};

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

struct AssetLeaseRetirementPinPayload final {
    AssetLease lease{};
    AssetRetirementLedger* ledger = nullptr;
    AssetHandle handle{};
    std::pmr::memory_resource* memoryResource = nullptr;
};

[[nodiscard]] AssetLeaseRetirementPinPayload*
allocateAssetLeaseRetirementPinPayload(std::pmr::memory_resource& memoryResource) noexcept
{
    try
    {
        void* storage = memoryResource.allocate(sizeof(AssetLeaseRetirementPinPayload),
                                                alignof(AssetLeaseRetirementPinPayload));
        return std::construct_at(static_cast<AssetLeaseRetirementPinPayload*>(storage),
                                 AssetLeaseRetirementPinPayload{
                                     .memoryResource = &memoryResource,
                                 });
    } catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

void releaseAssetLeaseRetirementPin(void* userData) noexcept
{
    auto* payload = static_cast<AssetLeaseRetirementPinPayload*>(userData);
    if (payload == nullptr)
    {
        return;
    }
    if (payload->ledger != nullptr)
    {
        payload->ledger->markReleased(payload->handle);
    }
    std::pmr::memory_resource* memoryResource = payload->memoryResource;
    std::destroy_at(payload);
    if (memoryResource != nullptr)
    {
        memoryResource->deallocate(payload, sizeof(AssetLeaseRetirementPinPayload),
                                   alignof(AssetLeaseRetirementPinPayload));
    }
}

[[nodiscard]] bool isGpuRetirementState(AssetLogicalState state) noexcept
{
    return state == AssetLogicalState::ReadyCpu || state == AssetLogicalState::UploadQueued ||
           state == AssetLogicalState::ReadyGpu || state == AssetLogicalState::UnloadPending;
}

[[nodiscard]] bool hasLiveGpuRetirements(const AssetRetirementLedger& ledger) noexcept
{
    for (const auto& record : ledger.records())
    {
        const bool gpuResource = record.kind == AssetRetirementKind::GpuTexture2D ||
                                 record.kind == AssetRetirementKind::GpuStaticMesh;
        if (gpuResource && record.state != AssetRetirementState::Released)
        {
            return true;
        }
    }
    return false;
}

struct StagedCatalogResident final {
    Core::AssetId assetId{};
    AssetHandle handle{};
};

[[nodiscard]] const CatalogChange* findCatalogChange(std::span<const CatalogChange> changes,
                                                     Core::AssetId assetId) noexcept
{
    const auto it = std::lower_bound(
        changes.begin(), changes.end(), assetId,
        [](const CatalogChange& change, Core::AssetId candidate) {
            return change.assetId < candidate;
        });
    return it != changes.end() && it->assetId == assetId ? std::addressof(*it) : nullptr;
}

[[nodiscard]] const StagedCatalogResident*
findStagedResident(std::span<const StagedCatalogResident> staged, Core::AssetId assetId) noexcept
{
    const auto it = std::find_if(staged.begin(), staged.end(), [assetId](const auto& resident) {
        return resident.assetId == assetId;
    });
    return it == staged.end() ? nullptr : std::addressof(*it);
}

[[nodiscard]] bool canReuseResidentState(AssetLogicalState state) noexcept
{
    return state == AssetLogicalState::ReadyCpu || state == AssetLogicalState::UploadQueued ||
           state == AssetLogicalState::ReadyGpu;
}

} // namespace

AssetSystem::AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                         Core::usize queueCapacity, Core::u32 defaultPumpBudget, Task::ITaskSystem* taskSystem,
                         Render::NullUploadLedger* uploadLedger, AssetGpuUploadConfig gpuUploadConfig,
                         bool autoGpuUpload, bool requireTyped2dPayloads)
    : m_store(std::move(store)), m_batch(batch), m_memoryResource(memoryResource), m_queueCapacity(queueCapacity),
      m_defaultPumpBudget(defaultPumpBudget), m_taskSystem(taskSystem), m_uploadLedger(uploadLedger),
      m_gpuUploadConfig(gpuUploadConfig), m_ownerThread(std::this_thread::get_id()), m_autoGpuUpload(autoGpuUpload),
      m_requireTyped2dPayloads(requireTyped2dPayloads), m_catalogRoot(memoryResource), m_index(memoryResource),
      m_queue(memoryResource), m_asyncRequests(memoryResource)
{
    m_queue.reserve(m_queueCapacity);
    m_asyncRequests.reserve(m_queueCapacity);
    if (m_uploadLedger != nullptr)
    {
        m_gpuUpload =
            std::make_unique<AssetGpuUploadCoordinator>(m_store, *m_uploadLedger, m_gpuUploadConfig, &m_retirement);
    }
}

AssetSystem::~AssetSystem() noexcept
{
    if (m_gpuRetirementDevice != nullptr && hasLiveGpuRetirements(m_retirement))
    {
        const auto status = drainGpuRetirements();
        if (!status)
        {
            // Retirement pins reference this AssetSystem's store/ledger. The
            // RenderDevice-outlives-AssetSystem contract therefore requires a
            // proven drain before member destruction; continuing would be UAF.
            std::terminate();
        }
    }
    m_gpuRetirementDevice = nullptr;
}

AssetSystem::AssetSystem(AssetSystem&& other) noexcept
    : m_store(std::move(other.m_store)), m_batch(other.m_batch), m_memoryResource(other.m_memoryResource),
      m_queueCapacity(other.m_queueCapacity), m_defaultPumpBudget(other.m_defaultPumpBudget),
      m_taskSystem(other.m_taskSystem), m_uploadLedger(other.m_uploadLedger), m_gpuUploadConfig(other.m_gpuUploadConfig),
      m_retirement(std::move(other.m_retirement)),
      m_gpuRetirementDevice(std::exchange(other.m_gpuRetirementDevice, nullptr)),
      m_ownerThread(std::exchange(other.m_ownerThread, {})),
      m_autoGpuUpload(other.m_autoGpuUpload), m_requireTyped2dPayloads(other.m_requireTyped2dPayloads),
      m_catalog(std::move(other.m_catalog)), m_catalogRoot(std::move(other.m_catalogRoot)),
      m_index(std::move(other.m_index)), m_queue(std::move(other.m_queue)),
      m_asyncRequests(std::move(other.m_asyncRequests)),
      m_inFlight(other.m_inFlight.load(std::memory_order_relaxed))
{
    if (m_gpuRetirementDevice != nullptr && hasLiveGpuRetirements(m_retirement))
    {
        // An in-flight pin stores addresses into the source AssetSystem. Moving
        // that owner would invalidate the completion callback.
        std::terminate();
    }
    m_gpuRetirementDevice = nullptr;
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
    other.m_requireTyped2dPayloads = false;
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
                           config.autoGpuUpload, config.requireTyped2dPayloads);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "asset system construction failed");
    }
}

Core::Status AssetSystem::bindCatalog(std::string_view catalogRootUtf8, CatalogSnapshot catalog)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(AssetErrorCode::WrongOwnerThread,
                             "catalog binding must run on the AssetSystem owner thread");
    }
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (catalogRootUtf8.empty() || catalogRootUtf8.find('\0') != std::string_view::npos)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    if (m_catalog && !isCatalogReloadIdle())
    {
        return Core::failure(AssetErrorCode::CatalogReloadBusy,
                             "replacing a bound catalog requires an idle AssetSystem");
    }
    return commitCatalogWhenIdle(catalogRootUtf8, std::move(catalog));
}

bool AssetSystem::isCatalogReloadIdle() const noexcept
{
    return m_queue.empty() && m_asyncRequests.empty() && m_inFlight.load(std::memory_order_acquire) == 0U &&
           m_store.activeCount() == 0U && m_index.empty() &&
           (m_gpuUpload == nullptr || m_gpuUpload->trackedCount() == 0U) && m_retirement.liveCount() == 0U;
}

bool AssetSystem::isCatalogMigrationQuiescent() const noexcept
{
    return m_queue.empty() && m_asyncRequests.empty() &&
           m_inFlight.load(std::memory_order_acquire) == 0U &&
           (m_gpuUpload == nullptr || m_gpuUpload->trackedCount() == 0U) &&
           m_retirement.liveCount() == 0U;
}

void AssetSystem::prepareCatalogOpenConfig(CatalogPackageOpenConfig& config,
                                           bool requireFullValidation) const noexcept
{
    if (config.manifest.catalog.memoryResource == nullptr)
    {
        config.manifest.catalog.memoryResource = m_memoryResource;
    }
    if (config.manifest.catalog.maxEntries == 0U)
    {
        config.manifest.catalog.maxEntries = 1024U;
    }
    if (config.manifest.catalog.maxDependencies == 0U)
    {
        config.manifest.catalog.maxDependencies = 4096U;
    }
    if (config.manifest.catalog.maxDependenciesPerAsset == 0U)
    {
        config.manifest.catalog.maxDependenciesPerAsset = 64U;
    }
    if (config.validation.file.memoryResource == nullptr)
    {
        config.validation.file.memoryResource = m_memoryResource;
    }
    if (requireFullValidation)
    {
        config.validateOnOpen = true;
        config.validation.verifyContent = true;
    }
    if (m_requireTyped2dPayloads)
    {
        config.validateOnOpen = true;
        config.validation.verifyContent = true;
        config.validation.verifyTypedPayload = true;
    }
}

Core::Status AssetSystem::commitCatalogWhenIdle(std::string_view catalogRootUtf8, CatalogSnapshot catalog)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(AssetErrorCode::WrongOwnerThread,
                             "catalog commit must run on the AssetSystem owner thread");
    }
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (catalogRootUtf8.empty() || catalogRootUtf8.find('\0') != std::string_view::npos)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }
    if (!isCatalogReloadIdle())
    {
        return Core::failure(AssetErrorCode::CatalogReloadBusy,
                             "catalog commit requires an idle AssetSystem");
    }

    try
    {
        std::pmr::string nextRoot{m_memoryResource};
        nextRoot.assign(catalogRootUtf8.begin(), catalogRootUtf8.end());

        // Both strings use m_memoryResource; swap and CatalogSnapshot move assignment are noexcept
        // after all fallible validation/allocation has completed.
        m_catalogRoot.swap(nextRoot);
        m_catalog = std::move(catalog);
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed, "catalog reload root allocation failed");
    }
}

Core::Result<CatalogReloadResult>
AssetSystem::reloadCatalog(std::string_view catalogRootUtf8, CatalogReloadConfig config)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(AssetErrorCode::WrongOwnerThread,
                             "catalog reload must run on the AssetSystem owner thread");
    }
    if (!m_catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "catalog reload requires an existing bound catalog");
    }
    if (!isCatalogMigrationQuiescent())
    {
        return Core::failure(AssetErrorCode::CatalogReloadBusy,
                             "catalog reload requires quiescent IO, upload, and retirement owners");
    }

    const auto validateParticipants = [](auto participants, const char* label) -> Core::Status {
        for (Core::usize left = 0; left < participants.size(); ++left)
        {
            if (participants[left] == nullptr)
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, label);
            }
            for (Core::usize right = left + 1U; right < participants.size(); ++right)
            {
                if (participants[left] == participants[right])
                {
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig, label);
                }
            }
        }
        return Core::success();
    };
    if (auto status = validateParticipants(
            config.bindings.sprite2D,
            "catalog reload Sprite2D participants contain a null or duplicate registry");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = validateParticipants(
            config.bindings.mesh3D,
            "catalog reload Mesh3D participants contain a null or duplicate registry");
        !status)
    {
        return Core::failure(std::move(status.error()));
    }

    prepareCatalogOpenConfig(config.package, true);

    auto replacement = openCatalogPackage(catalogRootUtf8, config.package);
    if (!replacement)
    {
        return Core::failure(
            std::move(replacement.error()).withContext("AssetSystem::reloadCatalog", "open"));
    }

    if (config.changePlan.memoryResource == nullptr)
    {
        config.changePlan.memoryResource = m_memoryResource;
    }
    auto plan = planCatalogChanges(m_catalog, *replacement, config.changePlan);
    if (!plan)
    {
        return Core::failure(
            std::move(plan.error()).withContext("AssetSystem::reloadCatalog", "plan"));
    }

    std::pmr::vector<Core::AssetId> reloadRoots{m_memoryResource};
    std::pmr::vector<CatalogLoadPlanEntry> candidateLoads{m_memoryResource};
    std::pmr::vector<StagedCatalogResident> staged{m_memoryResource};
    std::pmr::vector<CatalogResidentMigration> migrations{config.changePlan.memoryResource};
    std::pmr::vector<IndexEntry> nextIndex{m_memoryResource};
    std::pmr::string nextRoot{m_memoryResource};

    const auto rollbackStaged = [&]() noexcept {
        for (const auto& resident : staged)
        {
            (void)m_store.unload(resident.handle);
        }
        staged.clear();
    };

    try
    {
        nextRoot.assign(catalogRootUtf8.begin(), catalogRootUtf8.end());
        reloadRoots.reserve(m_index.size());
        Core::usize removedResidentCount = 0U;
        for (const auto& resident : m_index)
        {
            const CatalogChange* change = findCatalogChange(plan->changes, resident.assetId);
            if (change == nullptr)
            {
                continue;
            }
            if (change->kind == CatalogChangeKind::Removed)
            {
                ++removedResidentCount;
            }
            else
            {
                reloadRoots.push_back(resident.assetId);
            }
        }

        if (!reloadRoots.empty())
        {
            auto loads = planCatalogLoads(
                *replacement, reloadRoots,
                CatalogLoadPlanConfig{.memoryResource = m_memoryResource});
            if (!loads)
            {
                return Core::failure(std::move(loads.error()).withContext(
                    "AssetSystem::reloadCatalog", "residentLoadPlan"));
            }
            candidateLoads = std::move(*loads);
        }

        Core::usize stagedCount = 0U;
        Core::u64 stagedCookedFileBytes = 0U;
        for (const auto& row : candidateLoads)
        {
            const auto currentIndex = findIndex(row.assetId);
            const CatalogChange* change = findCatalogChange(plan->changes, row.assetId);
            const bool reusable = currentIndex && change == nullptr &&
                                  canReuseResidentState(m_store.state(m_index[*currentIndex].handle));
            if (!reusable)
            {
                ++stagedCount;
                if (row.cookedFileBytes >
                    (std::numeric_limits<Core::u64>::max)() - stagedCookedFileBytes)
                {
                    return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                         "catalog reload staged byte budget overflow");
                }
                stagedCookedFileBytes += row.cookedFileBytes;
            }
        }
        if (stagedCount > m_store.availableCount())
        {
            return Core::failure(
                AssetErrorCode::CatalogCapacityExceeded,
                "catalog reload requires double-residency headroom for replacement generations");
        }
        if (m_batch.maxTotalCookedFileBytes != 0U &&
            stagedCookedFileBytes > m_batch.maxTotalCookedFileBytes)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "catalog reload exceeds maxTotalCookedFileBytes budget");
        }
        const Core::usize projectedMigrationCount = stagedCount + removedResidentCount;
        if (projectedMigrationCount > config.maxResidentMigrations)
        {
            return Core::failure(AssetErrorCode::CatalogCapacityExceeded,
                                 "catalog resident migration capacity exceeded");
        }

        staged.reserve(stagedCount);
        nextIndex.reserve(m_index.size() + stagedCount);
        migrations.reserve(projectedMigrationCount);
        for (const auto& row : candidateLoads)
        {
            const auto currentIndex = findIndex(row.assetId);
            const CatalogChange* change = findCatalogChange(plan->changes, row.assetId);
            const bool reusable = currentIndex && change == nullptr &&
                                  canReuseResidentState(m_store.state(m_index[*currentIndex].handle));
            if (reusable)
            {
                continue;
            }

            auto cooked = loadCookedAssetFromCatalog(catalogRootUtf8, *replacement, row.assetId,
                                                      m_batch.file);
            if (!cooked)
            {
                rollbackStaged();
                return Core::failure(std::move(cooked.error()).withContext(
                    "AssetSystem::reloadCatalog", "stageResident"));
            }
            auto handle = m_store.publish(std::move(*cooked));
            if (!handle)
            {
                rollbackStaged();
                return Core::failure(std::move(handle.error()).withContext(
                    "AssetSystem::reloadCatalog", "publishStagedResident"));
            }
            staged.push_back(StagedCatalogResident{
                .assetId = row.assetId,
                .handle = *handle,
            });
        }

        for (const auto& resident : m_index)
        {
            const CatalogChange* change = findCatalogChange(plan->changes, resident.assetId);
            const StagedCatalogResident* replacementResident =
                findStagedResident(staged, resident.assetId);
            if (change != nullptr && change->kind == CatalogChangeKind::Removed)
            {
                migrations.push_back(CatalogResidentMigration{
                    .assetId = resident.assetId,
                    .kind = CatalogResidentMigrationKind::Removed,
                    .previous = resident.handle,
                });
                continue;
            }
            if (replacementResident != nullptr)
            {
                migrations.push_back(CatalogResidentMigration{
                    .assetId = resident.assetId,
                    .kind = CatalogResidentMigrationKind::Replaced,
                    .previous = resident.handle,
                    .current = replacementResident->handle,
                });
                nextIndex.push_back(IndexEntry{
                    .assetId = resident.assetId,
                    .handle = replacementResident->handle,
                });
                continue;
            }
            nextIndex.push_back(resident);
        }
        for (const auto& resident : staged)
        {
            if (findIndex(resident.assetId))
            {
                continue;
            }
            migrations.push_back(CatalogResidentMigration{
                .assetId = resident.assetId,
                .kind = CatalogResidentMigrationKind::LoadedDependency,
                .current = resident.handle,
            });
            nextIndex.push_back(IndexEntry{
                .assetId = resident.assetId,
                .handle = resident.handle,
            });
        }
        const auto indexLess = [](const IndexEntry& left, const IndexEntry& right) {
            return left.assetId < right.assetId;
        };
        const auto migrationLess = [](const CatalogResidentMigration& left,
                                      const CatalogResidentMigration& right) {
            return left.assetId < right.assetId;
        };
        std::sort(nextIndex.begin(), nextIndex.end(), indexLess);
        std::sort(migrations.begin(), migrations.end(), migrationLess);
        const auto duplicateIndex =
            std::adjacent_find(nextIndex.begin(), nextIndex.end(), [](const auto& left,
                                                                     const auto& right) {
                return left.assetId == right.assetId;
            });
        if (duplicateIndex != nextIndex.end())
        {
            rollbackStaged();
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "catalog reload produced duplicate resident AssetId");
        }
    }
    catch (const std::bad_alloc&)
    {
        rollbackStaged();
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "catalog resident migration allocation failed");
    }

    for (const auto& migration : migrations)
    {
        if (!migration.previous)
        {
            continue;
        }
        if (m_store.assetId(migration.previous) != migration.assetId ||
            m_store.state(migration.previous) == AssetLogicalState::Unloaded ||
            m_store.state(migration.previous) == AssetLogicalState::UnloadPending)
        {
            rollbackStaged();
            return Core::failure(AssetErrorCode::InvalidHandle,
                                 "catalog reload resident preflight found a stale handle");
        }
    }

    Core::usize preparedSpriteCount = 0;
    Core::usize preparedMeshCount = 0;
    const auto abortPreparedBindings = [&]() noexcept {
        while (preparedMeshCount != 0)
        {
            config.bindings.mesh3D[--preparedMeshCount]->abortPreparedCatalogReload();
        }
        while (preparedSpriteCount != 0)
        {
            config.bindings.sprite2D[--preparedSpriteCount]->abortPreparedCatalogReload();
        }
    };
    for (; preparedSpriteCount < config.bindings.sprite2D.size(); ++preparedSpriteCount)
    {
        if (auto status = config.bindings.sprite2D[preparedSpriteCount]->prepareCatalogReload(
                *this, migrations);
            !status)
        {
            abortPreparedBindings();
            rollbackStaged();
            return Core::failure(std::move(status.error()).withContext(
                "AssetSystem::reloadCatalog", "prepareSprite2D"));
        }
    }
    for (; preparedMeshCount < config.bindings.mesh3D.size(); ++preparedMeshCount)
    {
        if (auto status = config.bindings.mesh3D[preparedMeshCount]->prepareCatalogReload(
                *this, migrations);
            !status)
        {
            abortPreparedBindings();
            rollbackStaged();
            return Core::failure(std::move(status.error()).withContext(
                "AssetSystem::reloadCatalog", "prepareMesh3D"));
        }
    }

    // Every operation below is non-allocating after complete candidate/index/result staging.
    for (const auto& migration : migrations)
    {
        if (migration.previous)
        {
            const auto status = m_store.unload(migration.previous);
            if (!status)
            {
                std::terminate();
            }
        }
    }
    m_index.swap(nextIndex);
    m_catalogRoot.swap(nextRoot);
    m_catalog = std::move(*replacement);
    for (const auto& resident : staged)
    {
        noteReadyCpu(resident.handle);
    }
    for (Sprite2DBindingRegistry* registry : config.bindings.sprite2D)
    {
        registry->commitPreparedCatalogReload();
    }
    for (Mesh3DBindingRegistry* registry : config.bindings.mesh3D)
    {
        registry->commitPreparedCatalogReload();
    }
    for (Sprite2DBindingRegistry* registry : config.bindings.sprite2D)
    {
        (void)registry->drainPendingRetirements();
    }
    for (Mesh3DBindingRegistry* registry : config.bindings.mesh3D)
    {
        (void)registry->drainPendingRetirements();
    }

    return CatalogReloadResult{
        .changes = std::move(*plan),
        .residentMigrations = std::move(migrations),
    };
}

Core::Status AssetSystem::openAndBindCatalog(std::string_view catalogRootUtf8, CatalogPackageOpenConfig openConfig)
{
    if (m_memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset system has no memory resource");
    }
    prepareCatalogOpenConfig(openConfig, false);
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
    const auto state = m_store.state(handle);
    if (!handle || state == AssetLogicalState::UnloadPending || state == AssetLogicalState::Unloaded)
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
    const auto root = Detail::pathFromUtf8Bytes(std::string_view(m_catalogRoot));
    const auto relative = Detail::pathFromUtf8Bytes(artifactPath->view());
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
            if (auto status = insertIndex(row.assetId, *handle); !status)
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
    if (auto status = insertIndex(row.assetId, *handle); !status)
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
    if (auto status = mergeGpuStats(stats); !status)
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

    // Commit only a completed dispatch-order prefix. Worker completion timing must not
    // determine the order in which asset generations become visible on the owner thread.
    stats.mainCompletions = commitAsyncCompletions(limit, stats);
    Core::u32 consumedWork = stats.mainCompletions;

    // Completion commits and queued-request advancement share one pump budget. Retain
    // the queue head on transient Task QueueFull. Active request state is independently
    // bounded by queueCapacity.
    while ((limit == 0U || consumedWork < limit) && !m_queue.empty() &&
           m_asyncRequests.size() < m_queueCapacity)
    {
        const auto item = m_queue.front();

        if (m_store.state(item.handle) != AssetLogicalState::Queued)
        {
            m_queue.erase(m_queue.begin());
            ++stats.processed;
            ++consumedWork;
            continue;
        }

        auto pathResult = resolveObjectPath(item.assetId, item.assetKind);
        if (!pathResult)
        {
            m_queue.erase(m_queue.begin());
            ++stats.processed;
            ++consumedWork;
            auto failStatus = m_store.fail(item.handle);
            if (!failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext("AssetSystem::pump", "fail"));
            }
            ++stats.becameFailed;
            continue;
        }

        std::shared_ptr<AsyncRequestState> request;
        Task::TaskCallable ioWork;
        try
        {
            request = std::make_shared<AsyncRequestState>();
            request->handle = item.handle;
            request->assetId = item.assetId;

            const auto maxBytes = m_batch.file.maxFileBytes;
            ioWork = [request, path = std::move(*pathResult), maxBytes]() noexcept {
                bool ok = false;
                try
                {
                    auto bytes = Core::readFile(
                        path, Core::ReadFileConfig{
                                  .maxBytes = maxBytes,
                                  .memoryResource = std::pmr::new_delete_resource(),
                              });
                    if (bytes)
                    {
                        request->bytes = std::move(*bytes);
                        ok = true;
                    }
                } catch (...)
                {
                    ok = false;
                }
                request->outcome.store(ok ? AsyncRequestState::Outcome::Succeeded
                                          : AsyncRequestState::Outcome::Failed,
                                       std::memory_order_release);
            };
            m_asyncRequests.push_back(request);
        } catch (const std::bad_alloc&)
        {
            return Core::failure(AssetErrorCode::AllocationFailed,
                                 "asset async request state allocation failed");
        }

        Core::Status scheduleStatus = Core::success();
        try
        {
            scheduleStatus = m_taskSystem->scheduleIo(std::move(ioWork));
        } catch (const std::bad_alloc&)
        {
            m_asyncRequests.pop_back();
            return Core::failure(AssetErrorCode::AllocationFailed,
                                 "asset IO scheduling allocation failed");
        } catch (const std::exception&)
        {
            m_asyncRequests.pop_back();
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "task system threw while scheduling asset IO");
        } catch (...)
        {
            m_asyncRequests.pop_back();
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "task system threw an unknown exception while scheduling asset IO");
        }
        if (!scheduleStatus)
        {
            m_asyncRequests.pop_back();
            // QueueFull is transient backpressure. Preserve both the queue head and
            // Queued generation so the next pump retries in exactly the same order.
            if (scheduleStatus.error().code == Task::TaskErrorCode::QueueFull)
            {
                break;
            }

            m_queue.erase(m_queue.begin());
            ++stats.processed;
            ++consumedWork;
            auto failStatus = m_store.fail(item.handle);
            if (!failStatus)
            {
                return Core::failure(std::move(failStatus.error()).withContext("AssetSystem::pump", "failAfterQueue"));
            }
            ++stats.becameFailed;
            continue;
        }

        // The worker only publishes into request state, so it is safe for it to finish
        // before this owner-thread transition; commit cannot run concurrently with pump().
        auto markStatus = m_store.markLoading(item.handle);
        m_queue.erase(m_queue.begin());
        m_inFlight.fetch_add(1U, std::memory_order_acq_rel);
        ++stats.processed;
        ++stats.dispatchedIo;
        ++consumedWork;
        if (!markStatus)
        {
            return Core::failure(std::move(markStatus.error()).withContext("AssetSystem::pump", "markLoading"));
        }
    }

    // Fast IO may have finished during dispatch. Preserve the same prefix ordering while
    // consuming only budget left after the initial commits and queued-request advancement.
    if (limit == 0U)
    {
        stats.mainCompletions += commitAsyncCompletions(0U, stats);
    } else if (consumedWork < limit)
    {
        stats.mainCompletions += commitAsyncCompletions(limit - consumedWork, stats);
    }

    if (auto status = mergeGpuStats(stats); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    stats.remaining = static_cast<Core::u32>(m_queue.size());
    stats.inFlight = m_inFlight.load(std::memory_order_acquire);
    return stats;
}

Core::u32 AssetSystem::commitAsyncCompletions(Core::u32 limit, AssetPumpStats& stats) noexcept
{
    Core::u32 committed = 0;
    Core::usize completedPrefix = 0;
    while (completedPrefix < m_asyncRequests.size() && (limit == 0U || committed < limit))
    {
        const auto& request = m_asyncRequests[completedPrefix];
        const auto outcome = request->outcome.load(std::memory_order_acquire);
        if (outcome == AsyncRequestState::Outcome::Reading)
        {
            break;
        }

        const auto stateBefore = m_store.state(request->handle);
        bool ok = outcome == AsyncRequestState::Outcome::Succeeded;
        std::pmr::vector<std::byte> ownerBytes{m_memoryResource};
        if (ok)
        {
            try
            {
                ownerBytes.assign(request->bytes.begin(), request->bytes.end());
            } catch (...)
            {
                ok = false;
            }
        }
        completeOnMain(request->handle, request->assetId, std::move(ownerBytes), ok);

        if (stateBefore == AssetLogicalState::Loading)
        {
            if (m_store.hasCpuPayload(request->handle))
            {
                ++stats.becameReady;
            } else if (m_store.state(request->handle) == AssetLogicalState::Failed)
            {
                ++stats.becameFailed;
            }
        }

        const auto previous = m_inFlight.fetch_sub(1U, std::memory_order_acq_rel);
        if (previous == 0U)
        {
            m_inFlight.store(0U, std::memory_order_release);
        }
        ++completedPrefix;
        ++committed;
    }

    if (completedPrefix != 0U)
    {
        m_asyncRequests.erase(m_asyncRequests.begin(),
                              m_asyncRequests.begin() + static_cast<std::ptrdiff_t>(completedPrefix));
    }
    return committed;
}

void AssetSystem::completeOnMain(AssetHandle handle, Core::AssetId assetId, std::pmr::vector<std::byte> bytes,
                                 bool ok) noexcept
{
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
        (void)m_retirement.enqueueDestroy(handle, m_store.assetId(handle), {});
        m_retirement.markReleased(handle);
    }

    const auto status = m_store.unload(handle);
    if (status)
    {
        // AssetId lookup is a logical-residency index. Hide it immediately even when
        // a live AssetLease keeps the old generation payload in UnloadPending.
        forgetHandle(handle);
    }
    return status;
}

Core::Status AssetSystem::retireTexture2D(Render::IRenderDevice& device, AssetHandle handle,
                                          Render::GpuTextureId texture)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "GPU texture retirement must run on the AssetSystem owner thread");
    }
    if (!handle || !texture)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU texture retirement requires valid asset and texture handles");
    }

    auto lease = m_store.acquire(handle);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext("AssetSystem::retireTexture2D", "acquire"));
    }
    auto status = retireTexture2D(device, *lease, texture);
    if (!status)
    {
        return Core::failure(std::move(status.error()).withContext("AssetSystem::retireTexture2D", "lease"));
    }
    return Core::success();
}

Core::Status AssetSystem::retireTexture2D(Render::IRenderDevice& device, AssetLease& lease,
                                          Render::GpuTextureId& texture)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "GPU texture retirement must run on the AssetSystem owner thread");
    }
    if (!lease || !texture)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU texture retirement requires a valid lease and texture handle");
    }

    const AssetHandle handle = lease.handle();
    const Core::AssetId storeAssetId = m_store.assetId(handle);
    if (!storeAssetId || storeAssetId != lease.assetId())
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU texture retirement lease does not belong to this AssetSystem");
    }
    if (m_store.assetKind(handle) != AssetFormat::AssetKind::Texture2D ||
        lease.assetKind() != AssetFormat::AssetKind::Texture2D)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU texture retirement requires a Texture2D lease");
    }
    if (!isGpuRetirementState(m_store.state(handle)))
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "GPU texture retirement requires a resident Texture2D lease");
    }
    const bool hasLiveRetirement = std::ranges::any_of(
        m_retirement.records(),
        [handle](const AssetRetirementRecord& record) noexcept {
            return record.handle == handle && record.state != AssetRetirementState::Released;
        });
    if (hasLiveRetirement)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "GPU texture retirement is already tracked for this lease");
    }
    if (m_gpuRetirementDevice != nullptr && m_gpuRetirementDevice != &device)
    {
        if (auto status = drainGpuRetirements(); !status)
        {
            return Core::failure(std::move(status.error()).withContext("AssetSystem::retireTexture2D",
                                                                       "previousDeviceDrain"));
        }
    }

    if (auto status = m_retirement.enqueueTexture2D(handle, storeAssetId, texture); !status)
    {
        return status;
    }

    AssetLeaseRetirementPinPayload* payload =
        allocateAssetLeaseRetirementPinPayload(*m_memoryResource);
    if (payload == nullptr)
    {
        m_retirement.cancel(handle);
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "GPU texture retirement pin allocation failed");
    }
    payload->lease = std::move(lease);
    payload->ledger = &m_retirement;
    payload->handle = handle;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, handle.id.index(), payload,
                                   &releaseAssetLeaseRetirementPin};
    m_retirement.markRetiring(handle);

    if (auto status = device.retireTexture2D(texture, completionPin); !status)
    {
        lease = std::move(payload->lease);
        payload->ledger = nullptr;
        m_retirement.cancel(handle);
        completionPin.release();
        return Core::failure(std::move(status.error()).withContext("AssetSystem::retireTexture2D", "render"));
    }

    texture = {};
    m_gpuRetirementDevice = &device;
    if (m_gpuUpload != nullptr)
    {
        // The render backend has accepted ownership of the AssetLease pin.
        // Retire any Null staging now without overwriting the GPU record.
        (void)m_gpuUpload->cancelUpload(handle);
    }
    // A synchronous backend may release the completion pin before returning. If
    // this was an already-UnloadPending generation's final lease, releaseLease()
    // has already erased the record and completed the logical unload.
    if (m_store.tryGet(handle) != nullptr)
    {
        if (auto status = m_store.unload(handle); !status)
        {
            // The live exact lease and state preflight make every remaining
            // record unloadable after backend ownership commits.
            std::terminate();
        }
    }
    // Logical lookup becomes stale immediately even while the strong lease
    // keeps the cooked payload in UnloadPending until GPU completion.
    forgetHandle(handle);
    return Core::success();
}

Core::Status AssetSystem::retireStaticMesh(Render::IRenderDevice& device, AssetHandle handle,
                                           Render::GpuMeshId mesh)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "GPU mesh retirement must run on the AssetSystem owner thread");
    }
    if (!handle || !mesh)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU mesh retirement requires valid asset and mesh handles");
    }

    auto lease = m_store.acquire(handle);
    if (!lease)
    {
        return Core::failure(std::move(lease.error()).withContext("AssetSystem::retireStaticMesh", "acquire"));
    }
    auto status = retireStaticMesh(device, *lease, mesh);
    if (!status)
    {
        return Core::failure(std::move(status.error()).withContext("AssetSystem::retireStaticMesh", "lease"));
    }
    return Core::success();
}

Core::Status AssetSystem::retireStaticMesh(Render::IRenderDevice& device, AssetLease& lease,
                                           Render::GpuMeshId& mesh)
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Core::failure(Render::RenderErrorCode::WrongOwnerThread,
                             "GPU mesh retirement must run on the AssetSystem owner thread");
    }
    if (!lease || !mesh)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU mesh retirement requires a valid lease and mesh handle");
    }

    const AssetHandle handle = lease.handle();
    const Core::AssetId storeAssetId = m_store.assetId(handle);
    if (!storeAssetId || storeAssetId != lease.assetId())
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU mesh retirement lease does not belong to this AssetSystem");
    }
    const AssetFormat::AssetKind meshKind = m_store.assetKind(handle);
    if ((meshKind != AssetFormat::AssetKind::StaticMesh &&
         meshKind != AssetFormat::AssetKind::SkinnedMesh) ||
        lease.assetKind() != meshKind)
    {
        return Core::failure(AssetErrorCode::InvalidHandle,
                             "GPU mesh retirement requires a StaticMesh or SkinnedMesh lease");
    }
    if (!isGpuRetirementState(m_store.state(handle)))
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "GPU mesh retirement requires a resident mesh lease");
    }
    const bool hasLiveRetirement = std::ranges::any_of(
        m_retirement.records(),
        [handle](const AssetRetirementRecord& record) noexcept {
            return record.handle == handle && record.state != AssetRetirementState::Released;
        });
    if (hasLiveRetirement)
    {
        return Core::failure(AssetErrorCode::AssetNotReady,
                             "GPU mesh retirement is already tracked for this lease");
    }
    if (m_gpuRetirementDevice != nullptr && m_gpuRetirementDevice != &device)
    {
        if (auto status = drainGpuRetirements(); !status)
        {
            return Core::failure(std::move(status.error()).withContext("AssetSystem::retireStaticMesh",
                                                                       "previousDeviceDrain"));
        }
    }

    if (auto status = m_retirement.enqueueStaticMesh(handle, storeAssetId, mesh); !status)
    {
        return status;
    }
    auto* payload = allocateAssetLeaseRetirementPinPayload(*m_memoryResource);
    if (payload == nullptr)
    {
        m_retirement.cancel(handle);
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "GPU mesh retirement pin allocation failed");
    }
    payload->lease = std::move(lease);
    payload->ledger = &m_retirement;
    payload->handle = handle;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, handle.id.index(), payload,
                                   &releaseAssetLeaseRetirementPin};
    m_retirement.markRetiring(handle);

    if (auto status = device.retireStaticMesh(mesh, completionPin); !status)
    {
        lease = std::move(payload->lease);
        payload->ledger = nullptr;
        m_retirement.cancel(handle);
        completionPin.release();
        return Core::failure(std::move(status.error()).withContext("AssetSystem::retireStaticMesh", "render"));
    }

    mesh = {};
    m_gpuRetirementDevice = &device;
    if (m_gpuUpload != nullptr)
    {
        (void)m_gpuUpload->cancelUpload(handle);
    }
    if (m_store.tryGet(handle) != nullptr)
    {
        if (auto status = m_store.unload(handle); !status)
        {
            std::terminate();
        }
    }
    forgetHandle(handle);
    return Core::success();
}

Core::Status AssetSystem::drainGpuRetirements() noexcept
{
    if (m_gpuRetirementDevice == nullptr)
    {
        return Core::success();
    }
    // The backend may have completed pins during ordinary present or its own
    // shutdown before AssetSystem observes the device again. The callback has
    // already released every AssetLease, so do not call a potentially stopped
    // or already-destroyed device merely to clear this non-owning pointer.
    if (!hasLiveGpuRetirements(m_retirement))
    {
        m_gpuRetirementDevice = nullptr;
        return Core::success();
    }
    auto status = m_gpuRetirementDevice->drainGpuRetirements();
    if (status)
    {
        if (hasLiveGpuRetirements(m_retirement))
        {
            return Core::failure(Render::RenderErrorCode::GpuRetirementDrainFailed,
                                 "render device reported a completed drain while AssetLease pins remain live");
        }
        m_gpuRetirementDevice = nullptr;
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
