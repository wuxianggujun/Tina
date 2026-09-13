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

class AssetSystem;
class Sprite2DBindingRegistry;
class Mesh3DBindingRegistry;
class ShaderBindingRegistry;

// Move-only stable-address pin for Tina owners that retain an AssetSystem pointer
// across calls. While a pin exists AssetSystem::canMove() is false. The token and
// AssetSystem must be released/destroyed on their shared owner thread.
class AssetSystemBorrow final {
  public:
    AssetSystemBorrow() noexcept = default;
    ~AssetSystemBorrow() noexcept;

    AssetSystemBorrow(const AssetSystemBorrow&) = delete;
    AssetSystemBorrow& operator=(const AssetSystemBorrow&) = delete;
    AssetSystemBorrow(AssetSystemBorrow&& other) noexcept;
    AssetSystemBorrow& operator=(AssetSystemBorrow&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept { return m_owner != nullptr; }

  private:
    friend class AssetSystem;
    explicit AssetSystemBorrow(AssetSystem& owner) noexcept : m_owner(&owner) {}
    void release() noexcept;

    AssetSystem* m_owner = nullptr;
};

struct AssetAsyncBudget final {
    // Logical package bytes of dispatched requests, including completed results
    // awaiting owner publication. Not heap allocation or OS working-set bytes.
    Core::u64 inFlightBytes = 64ULL * 1024ULL * 1024ULL;
    Core::u64 completionBytesPerPump = 16ULL * 1024ULL * 1024ULL;
    // Both are soft: zero disables the budget. One oversized request may run
    // alone / publish first so valid large assets can never starve permanently.
};

struct AssetSystemConfig final {
    Core::usize storeCapacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
    CookedAssetBatchLoadConfig batch{};
    // Logical metadata budget for queued + in-flight requests (not payload/page residency
    // or allocator overhead). 0 disables it. Storage grows on demand and is reused.
    Core::usize queueBudgetBytes = 64 * 1024 * 1024; // 64 MiB default
    // Hard limit on pending request count, preventing unbounded growth. 0 means unlimited.
    Core::usize maxPendingRequests = 0;
    AssetAsyncBudget asyncBudget{};
    // Default max work items advanced per pump() call. An async completion commit and a
    // queued request advance each consume one item from the same budget. 0 means process
    // all pending work.
    Core::u32 defaultPumpBudget = 8;
    // Optional non-owning task system for IO dispatch, parse and integrity verification.
    // Completed cooked owners are published by pump(), not re-hashed on the owner thread.
    // When null, pump() runs synchronous IO (asyncBudget does not apply).
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
    Core::u64 inFlightBytes = 0;
    Core::u64 dispatchedBytes = 0;
    Core::u64 completedBytes = 0;
    Core::u32 ioBackpressure = 0;
    Core::u32 oversizedIoRequests = 0;
    Core::u32 gpuSubmitted = 0;
    Core::u32 becameGpuReady = 0;
    Core::u32 gpuFailed = 0;
    Core::u32 gpuBackpressure = 0;
    Core::u32 gpuRetries = 0;
};

// Optional owner-thread GPU registry participants for catalog reload. The
// spans and pointed-to registries are borrowed only for one reload call.
struct CatalogReloadBindings final {
    std::span<Sprite2DBindingRegistry*> sprite2D{};
    std::span<Mesh3DBindingRegistry*> mesh3D{};
    std::span<ShaderBindingRegistry*> shader{};
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
    AssetSystem(AssetSystem&& other);
    AssetSystem& operator=(AssetSystem&&) = delete;

    // Move is owner-thread-only and rejects live GPU retirement callbacks or
    // tracked upload work before transferring any member. Borrowers of this
    // facade (binding registries/streams) must not span the move.
    [[nodiscard]] bool canMove() const noexcept;
    [[nodiscard]] Core::Result<AssetSystemBorrow> acquireStableBorrow() noexcept;

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
    [[nodiscard]] const AssetStore& store() const noexcept;
    // Controlled owner-thread publish path for already validated cooked data.
    // This is the only public mutation entry for injected/test/editor payloads;
    // catalog-backed game code should use load()/request().
    [[nodiscard]] Core::Result<AssetHandle> publishCooked(CookedAssetFile asset);
    // Explicit owner-thread queue injection for integration code that has a
    // catalog plan entry but needs to stage it before an IO pump.
    [[nodiscard]] Core::Result<AssetHandle> beginQueuedForOwner(
        Core::AssetId assetId, AssetFormat::AssetKind assetKind);
    // Owner-only logical GPU phase hooks for deterministic integration code.
    // Normal runtime code should use the configured upload coordinator instead.
    [[nodiscard]] Core::Status beginGpuUploadForOwner(AssetHandle handle) noexcept;
    [[nodiscard]] Core::Status completeGpuUploadForOwner(AssetHandle handle) noexcept;
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

    // Explicitly retries a non-transient GPU upload failure while retaining the
    // CPU cooked payload. Owner-thread only; uploads without a configured
    // coordinator are rejected instead of silently changing state.
    [[nodiscard]] Core::Status retryGpuUpload(AssetHandle handle);

    // GPU upload is driven through AssetGpuUploadCoordinator directly; the former
    // trackForGpuUpload()/pumpGpuUploads() forwarders here had no callers.

    [[nodiscard]] const CookedAssetFile* tryGet(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetLogicalState state(AssetHandle handle) const noexcept;
    [[nodiscard]] bool isGpuReady(AssetHandle handle) const noexcept;
    [[nodiscard]] Core::Result<AssetLease> acquire(AssetHandle handle);
    // Explicit logical invalidation for every consumer of this handle. Existing
    // leases retain CPU bytes until released; GPU instances require independent
    // retirement. Retiring one GPU instance never calls this operation implicitly.
    [[nodiscard]] Core::Status unload(AssetHandle handle) noexcept;

    // Acquires an AssetLease and transfers it to the Render retirement pin for
    // this GPU instance only. Other instances, the logical Asset and independent
    // upload staging stay valid. Call unload() explicitly when ending CPU residency.
    // Device and AssetSystem must outlive the pin; backend completion or an
    // explicit drain releases that lease exactly once (not necessarily the CPU payload).
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
    [[nodiscard]] Core::Status retireGpuShader(Render::IRenderDevice& device, AssetHandle handle,
                                                Render::GpuShaderId shader);
    [[nodiscard]] Core::Status retireGpuShader(Render::IRenderDevice& device, AssetLease& lease,
                                                Render::GpuShaderId& shader);
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
    [[nodiscard]] static AssetStore&& checkedStoreForMove(AssetSystem& source) noexcept;
    template <typename GpuId>
    [[nodiscard]] Core::Status retireGpuResource(
        Render::IRenderDevice& device, AssetLease& lease, GpuId& resource,
        AssetRetirementRecord retirement,
        Core::Status (Render::IRenderDevice::*retire)(GpuId, Render::FramePin&) noexcept);

    AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                Core::usize queueBudgetBytes, Core::usize maxPendingRequests, AssetAsyncBudget asyncBudget,
                Core::u32 defaultPumpBudget,
                Task::ITaskSystem* taskSystem, Render::NullUploadLedger* uploadLedger,
                AssetGpuUploadConfig gpuUploadConfig, bool autoGpuUpload, bool requireTyped2dPayloads);

    void forgetHandle(AssetHandle handle) noexcept;
    [[nodiscard]] bool isCatalogReloadIdle() const noexcept;
    [[nodiscard]] bool isCatalogMigrationQuiescent() const noexcept;
    [[nodiscard]] Core::usize queuedCount() const noexcept { return m_queue.size() - m_queueHead; }
    [[nodiscard]] bool queueEmpty() const noexcept { return queuedCount() == 0; }
    void popQueueFront() noexcept;
    void compactQueue() noexcept;
    [[nodiscard]] bool canEnqueueRequest() const noexcept;
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
    [[nodiscard]] Core::Result<AssetPumpStats> pumpSync(Core::u32 limit);
    [[nodiscard]] Core::Result<AssetPumpStats> pumpAsync(Core::u32 limit);
    [[nodiscard]] Core::Result<Core::u32> commitAsyncCompletions(Core::u32 limit,
                                                                  AssetPumpStats& stats);
    [[nodiscard]] Core::Status completeOnMain(AssetHandle handle, Core::AssetId assetId,
                                              CookedAssetFile cooked,
                                              std::optional<Core::Error> failure);
    [[nodiscard]] Core::Status noteReadyCpu(AssetHandle handle);
    [[nodiscard]] Core::Status mergeGpuStats(AssetPumpStats& stats) noexcept;
    [[nodiscard]] Core::Status requireOwnerThread() const noexcept;
    [[nodiscard]] AssetStore& mutableStoreForOwner() noexcept;

    friend class Sprite2DBindingRegistry;
    friend class Mesh3DBindingRegistry;
    friend class ShaderBindingRegistry;
    friend class AssetSystemBorrow;

    AssetStore m_store;
    CookedAssetBatchLoadConfig m_batch{};
    std::pmr::memory_resource* m_memoryResource = nullptr;
    Core::usize m_queueBudgetBytes = 0;
    Core::usize m_maxPendingRequests = 0;
    AssetAsyncBudget m_asyncBudget{};
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
    Core::usize m_queueHead = 0;
    // Dispatch order is commit order. Each state is also owned by its worker callable, so
    // AssetSystem move/destruction cannot invalidate an active blocking read.
    std::pmr::vector<std::shared_ptr<AsyncRequestState>> m_asyncRequests;
    std::atomic<Core::u32> m_inFlight{0};
    Core::u64 m_inFlightBytes = 0; // owner-thread bookkeeping, never written by workers
    Core::u32 m_stableBorrowCount = 0;
};

} // namespace Tina::Asset
