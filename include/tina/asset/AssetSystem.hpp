#pragma once

#include <tina/asset/AssetGpuUpload.hpp>
#include <tina/asset/AssetRetirement.hpp>
#include <tina/asset/AssetStore.hpp>
#include <tina/asset/CatalogChangePlan.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetBatch.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/UploadTicket.hpp>
#include <tina/task/TaskSystem.hpp>

#include <atomic>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Tina::Asset {

class Sprite2DBindingRegistry;
class Mesh3DBindingRegistry;

struct AssetSystemConfig final {
    Core::usize storeCapacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
    CookedAssetBatchLoadConfig batch{};
    // Bounded pending-request queue capacity. 0 defaults to storeCapacity.
    Core::usize queueCapacity = 0;
    // Default max work items advanced per pump() call. An async completion commit and a
    // queued request advance each consume one item from the same budget. 0 means process
    // all pending work.
    Core::u32 defaultPumpBudget = 8;
    // Optional non-owning task system for IO dispatch. Completed reads are retained in bounded
    // request state and committed by pump() on the owner thread. When null, pump() runs sync IO.
    Task::ITaskSystem* taskSystem = nullptr;
    // Optional non-owning Null upload ledger. When non-null, ReadyCpu assets are tracked and
    // advanced toward ReadyGpu during pump()/load() via AssetGpuUploadCoordinator.
    Render::NullUploadLedger* uploadLedger = nullptr;
    AssetGpuUploadConfig gpuUpload{};
    // When true and uploadLedger is set, newly ReadyCpu handles are auto-tracked for upload.
    bool autoGpuUpload = true;
    // When true, openAndBindCatalog verifies all known typed 2D payloads and dependencies.
    bool requireTyped2dPayloads = false;
};

struct AssetPumpStats final {
    Core::u32 processed = 0;
    Core::u32 becameReady = 0; // ReadyCpu transitions this pump
    Core::u32 becameFailed = 0;
    Core::u32 dispatchedIo = 0;
    Core::u32 mainCompletions = 0; // async read results committed on the owner thread
    Core::u32 remaining = 0;
    Core::u32 inFlight = 0;
    Core::u32 gpuSubmitted = 0;
    Core::u32 becameGpuReady = 0;
    Core::u32 gpuFailed = 0;
};

// Optional owner-thread GPU registry participants for catalog reload. The
// spans and pointed-to registries are borrowed only for one reload call.
struct CatalogReloadBindings final {
    std::span<Sprite2DBindingRegistry*> sprite2D{};
    std::span<Mesh3DBindingRegistry*> mesh3D{};
};

struct CatalogReloadConfig final {
    CatalogPackageOpenConfig package{};
    CatalogChangePlanConfig changePlan{
        .maxChanges = (std::numeric_limits<Core::u32>::max)(),
    };
    Core::u32 maxResidentMigrations = (std::numeric_limits<Core::u32>::max)();
    // Optional owner-thread GPU registries. They must belong to this
    // AssetSystem and are prepared before the Catalog/index swap.
    CatalogReloadBindings bindings{};
};

enum class CatalogResidentMigrationKind : Core::u8 {
    Replaced = 0,
    Removed = 1,
    LoadedDependency = 2,
};

struct CatalogResidentMigration final {
    Core::AssetId assetId{};
    CatalogResidentMigrationKind kind = CatalogResidentMigrationKind::Replaced;
    AssetHandle previous{};
    AssetHandle current{};
};

struct CatalogReloadResult final {
    CatalogChangePlan changes{};
    std::pmr::vector<CatalogResidentMigration> residentMigrations{};
};

// Catalog-bound CPU/GPU-logical asset facade.
// - load/loadOne: synchronous plan→load→publish (+ optional Null GPU upload pump)
// - request/pump: Queued→Loading→ReadyCpu→(UploadQueued→ReadyGpu)
// - optional Task::ITaskSystem for IO; optional NullUploadLedger for GPU-logical upload
class AssetSystem final {
  public:
    AssetSystem() = delete;
    ~AssetSystem() noexcept;

    AssetSystem(const AssetSystem&) = delete;
    AssetSystem& operator=(const AssetSystem&) = delete;
    AssetSystem(AssetSystem&& other) noexcept;
    AssetSystem& operator=(AssetSystem&&) = delete;

    [[nodiscard]] static Core::Result<AssetSystem> Create(AssetSystemConfig config);

    [[nodiscard]] Core::Status bindCatalog(std::string_view catalogRootUtf8, CatalogSnapshot catalog);

    // Opens and fully validates a candidate package, then stages replacement generations for
    // resident Modified/Affected assets and newly required dependencies. Commit swaps the
    // Catalog/root/index only after every fallible step succeeds. Previous leases retain their
    // old payload; residentMigrations maps stale weak handles to their new generations.
    // Queued/in-flight work, tracked uploads, and live retirement records remain a busy boundary.
    [[nodiscard]] Core::Result<CatalogReloadResult>
    reloadCatalog(std::string_view catalogRootUtf8, CatalogReloadConfig config = {});

    // Commits a package snapshot that was already fully validated by a cooker/import worker.
    // The snapshot is consumed only after every fallible owner-thread staging step succeeds, so
    // callers retain it across CatalogReloadBusy and other pre-commit failures.
    [[nodiscard]] Core::Result<CatalogReloadResult>
    reloadPreparedCatalog(std::string_view catalogRootUtf8, CatalogSnapshot&& catalog,
                          CatalogReloadConfig config = {});

    // openCatalogPackage(root, openConfig) then bindCatalog. Uses config.memoryResource for open.
    [[nodiscard]] Core::Status openAndBindCatalog(std::string_view catalogRootUtf8,
                                                  CatalogPackageOpenConfig openConfig = {});

    [[nodiscard]] bool hasCatalog() const noexcept;
    [[nodiscard]] const CatalogSnapshot* catalog() const noexcept;
    [[nodiscard]] std::string_view catalogRoot() const noexcept;
    [[nodiscard]] AssetStore& store() noexcept;
    [[nodiscard]] const AssetStore& store() const noexcept;
    [[nodiscard]] Core::u32 pendingCount() const noexcept;
    [[nodiscard]] Core::u32 inFlightCount() const noexcept;
    [[nodiscard]] bool hasGpuUpload() const noexcept;
    [[nodiscard]] const AssetRetirementLedger& retirement() const noexcept;
    [[nodiscard]] AssetRetirementStats retirementStats() const noexcept;

    [[nodiscard]] std::optional<AssetHandle> find(Core::AssetId assetId) const noexcept;
    // First catalog entry with the given kind (stable index order). Does not load.
    [[nodiscard]] std::optional<Core::AssetId> catalogFirstIdOfKind(AssetFormat::AssetKind kind) const noexcept;
    // First currently published handle with the given kind.
    [[nodiscard]] std::optional<AssetHandle> findFirstLoadedOfKind(AssetFormat::AssetKind kind) const noexcept;

    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    load(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> loadOne(Core::AssetId assetId);

    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    request(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> requestOne(Core::AssetId assetId);

    // Advances IO deferred work and, when configured, Null GPU upload toward ReadyGpu.
    // A non-zero budget bounds the combined owner completion commits and queued requests
    // advanced by this call. Passing 0 uses defaultPumpBudget.
    [[nodiscard]] Core::Result<AssetPumpStats> pump(Core::u32 budget = 0);

    // Explicitly track a ReadyCpu handle for GPU upload (no-op without upload ledger).
    [[nodiscard]] Core::Status trackForGpuUpload(AssetHandle handle);

    // Pump only GPU upload coordinator (no IO). No-op stats if upload not configured.
    [[nodiscard]] Core::Result<AssetGpuUploadStats> pumpGpuUploads();

    [[nodiscard]] const CookedAssetFile* tryGet(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetLogicalState state(AssetHandle handle) const noexcept;
    [[nodiscard]] bool isGpuReady(AssetHandle handle) const noexcept;
    [[nodiscard]] Core::Result<AssetLease> acquire(AssetHandle handle);
    [[nodiscard]] Core::Status unload(AssetHandle handle) noexcept;

    // Acquires an AssetLease, transfers it to the Render retirement pin, then
    // logically unloads the weak handle. The RenderDevice and AssetSystem must
    // both remain alive while the pin is live; backend completion or an explicit
    // drain releases the lease exactly once.
    [[nodiscard]] Core::Status retireTexture2D(Render::IRenderDevice& device, AssetHandle handle,
                                               Render::GpuTextureId texture);
    // Transfers an existing Texture2D lease and GPU owner only after the backend
    // accepts retirement. Every failure before that commit preserves both caller
    // values and leaves the asset and retirement ledger retryable. Owner-thread only.
    [[nodiscard]] Core::Status retireTexture2D(Render::IRenderDevice& device, AssetLease& lease,
                                               Render::GpuTextureId& texture);
    [[nodiscard]] Core::Status retireGpuMesh(Render::IRenderDevice& device, AssetHandle handle,
                                             Render::GpuMeshId mesh);
    // Mesh counterpart to the lease-consuming Texture2D transaction. The
    // transaction accepts both StaticMesh and SkinnedMesh leases because they
    // share the Render GpuMeshId retirement path.
    // Backend rejection and every pre-commit failure preserve both owners.
    [[nodiscard]] Core::Status retireGpuMesh(Render::IRenderDevice& device, AssetLease& lease,
                                             Render::GpuMeshId& mesh);
    [[nodiscard]] Core::Status drainGpuRetirements() noexcept;

  private:
    struct IndexEntry final {
        Core::AssetId assetId{};
        AssetHandle handle{};
    };

    struct WorkItem final {
        AssetHandle handle{};
        Core::AssetId assetId{};
        AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    };

    struct AsyncRequestState;

    AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                Core::usize queueCapacity, Core::u32 defaultPumpBudget, Task::ITaskSystem* taskSystem,
                Render::NullUploadLedger* uploadLedger, AssetGpuUploadConfig gpuUploadConfig, bool autoGpuUpload,
                bool requireTyped2dPayloads);

    void forgetHandle(AssetHandle handle) noexcept;
    [[nodiscard]] bool isCatalogReloadIdle() const noexcept;
    [[nodiscard]] bool isCatalogMigrationQuiescent() const noexcept;
    void prepareCatalogOpenConfig(CatalogPackageOpenConfig& config,
                                  bool requireFullValidation,
                                  std::pmr::memory_resource& transientValidationMemory) const noexcept;
    [[nodiscard]] Core::Status commitCatalogWhenIdle(std::string_view catalogRootUtf8,
                                                      CatalogSnapshot catalog);
    [[nodiscard]] std::optional<Core::u32> findIndex(Core::AssetId assetId) const noexcept;
    [[nodiscard]] Core::Status insertIndex(Core::AssetId assetId, AssetHandle handle);
    void eraseIndexAt(Core::u32 index) noexcept;
    [[nodiscard]] Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
    planForRequest(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> ensureQueued(const CatalogLoadPlanEntry& row);
    [[nodiscard]] Core::Result<std::string> resolveObjectPath(Core::AssetId assetId, AssetFormat::AssetKind kind) const;
    [[nodiscard]] Core::Result<AssetPumpStats> pumpSync(Core::u32 limit);
    [[nodiscard]] Core::Result<AssetPumpStats> pumpAsync(Core::u32 limit);
    [[nodiscard]] Core::u32 commitAsyncCompletions(Core::u32 limit, AssetPumpStats& stats) noexcept;
    void completeOnMain(AssetHandle handle, Core::AssetId assetId, std::pmr::vector<std::byte> bytes,
                        bool ok) noexcept;
    void noteReadyCpu(AssetHandle handle) noexcept;
    [[nodiscard]] Core::Status mergeGpuStats(AssetPumpStats& stats) noexcept;

    AssetStore m_store;
    CookedAssetBatchLoadConfig m_batch{};
    std::pmr::memory_resource* m_memoryResource = nullptr;
    Core::usize m_queueCapacity = 0;
    Core::u32 m_defaultPumpBudget = 0;
    Task::ITaskSystem* m_taskSystem = nullptr;
    Render::NullUploadLedger* m_uploadLedger = nullptr;
    AssetGpuUploadConfig m_gpuUploadConfig{};
    std::unique_ptr<AssetGpuUploadCoordinator> m_gpuUpload;
    AssetRetirementLedger m_retirement{};
    Render::IRenderDevice* m_gpuRetirementDevice = nullptr;
    std::thread::id m_ownerThread{};
    bool m_autoGpuUpload = true;
    bool m_requireTyped2dPayloads = false;
    CatalogSnapshot m_catalog{};
    std::pmr::string m_catalogRoot;
    std::pmr::vector<IndexEntry> m_index;
    std::pmr::vector<WorkItem> m_queue;
    // Dispatch order is commit order. Each state is also owned by its worker callable, so
    // AssetSystem move/destruction cannot invalidate an active blocking read.
    std::pmr::vector<std::shared_ptr<AsyncRequestState>> m_asyncRequests;
    std::atomic<Core::u32> m_inFlight{0};
};

} // namespace Tina::Asset
