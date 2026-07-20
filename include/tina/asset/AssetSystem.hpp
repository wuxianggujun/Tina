#pragma once

#include <tina/asset/AssetStore.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetBatch.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/task/TaskSystem.hpp>

#include <atomic>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct AssetSystemConfig final {
    Core::usize storeCapacity = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
    CookedAssetBatchLoadConfig batch{};
    // Bounded pending-request queue capacity. 0 defaults to storeCapacity.
    Core::usize queueCapacity = 0;
    // Default max work items advanced per pump() call. 0 means process all pending.
    Core::u32 defaultPumpBudget = 8;
    // Optional non-owning task system for IO+Main completion. When null, pump() runs sync IO.
    // When non-null, must outlive AssetSystem until drain/shutdown.
    Task::ITaskSystem* taskSystem = nullptr;
};

struct AssetPumpStats final {
    Core::u32 processed = 0;
    Core::u32 becameReady = 0;
    Core::u32 becameFailed = 0;
    Core::u32 dispatchedIo = 0;
    Core::u32 mainCompletions = 0;
    Core::u32 remaining = 0;
    Core::u32 inFlight = 0;
};

// Catalog-bound CPU asset facade.
// - load/loadOne: synchronous plan→load→publish
// - request/pump: Queued→Loading→Ready/Failed with bounded queue
// - optional Task::ITaskSystem: IO workers read files; pump drains Main completions
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

    [[nodiscard]] bool hasCatalog() const noexcept;
    [[nodiscard]] const CatalogSnapshot* catalog() const noexcept;
    [[nodiscard]] std::string_view catalogRoot() const noexcept;
    [[nodiscard]] AssetStore& store() noexcept;
    [[nodiscard]] const AssetStore& store() const noexcept;
    [[nodiscard]] Core::u32 pendingCount() const noexcept;
    [[nodiscard]] Core::u32 inFlightCount() const noexcept;

    [[nodiscard]] std::optional<AssetHandle> find(Core::AssetId assetId) const noexcept;

    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    load(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> loadOne(Core::AssetId assetId);

    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    request(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> requestOne(Core::AssetId assetId);

    // Advances deferred work. With taskSystem: dispatches IO and drains Main completions.
    // Without taskSystem: performs synchronous read on the caller thread (legacy A5 path).
    [[nodiscard]] Core::Result<AssetPumpStats> pump(Core::u32 budget = 0);

    [[nodiscard]] const CookedAssetFile* tryGet(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetLogicalState state(AssetHandle handle) const noexcept;
    [[nodiscard]] Core::Result<AssetLease> acquire(AssetHandle handle);
    [[nodiscard]] Core::Status unload(AssetHandle handle) noexcept;

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

    AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                Core::usize queueCapacity, Core::u32 defaultPumpBudget, Task::ITaskSystem* taskSystem) noexcept;

    void forgetHandle(AssetHandle handle) noexcept;
    [[nodiscard]] std::optional<Core::u32> findIndex(Core::AssetId assetId) const noexcept;
    [[nodiscard]] Core::Status insertIndex(Core::AssetId assetId, AssetHandle handle);
    void eraseIndexAt(Core::u32 index) noexcept;
    [[nodiscard]] Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
    planForRequest(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> ensureQueued(const CatalogLoadPlanEntry& row);
    [[nodiscard]] Core::Result<std::string> resolveObjectPath(Core::AssetId assetId, AssetFormat::AssetKind kind) const;
    [[nodiscard]] Core::Result<AssetPumpStats> pumpSync(Core::u32 limit);
    [[nodiscard]] Core::Result<AssetPumpStats> pumpAsync(Core::u32 limit);
    void completeOnMain(AssetHandle handle, Core::AssetId assetId, std::pmr::vector<std::byte> bytes, bool ok,
                        AssetPumpStats* stats) noexcept;

    AssetStore m_store;
    CookedAssetBatchLoadConfig m_batch{};
    std::pmr::memory_resource* m_memoryResource = nullptr;
    Core::usize m_queueCapacity = 0;
    Core::u32 m_defaultPumpBudget = 0;
    Task::ITaskSystem* m_taskSystem = nullptr;
    CatalogSnapshot m_catalog{};
    std::pmr::string m_catalogRoot;
    std::pmr::vector<IndexEntry> m_index;
    std::pmr::vector<WorkItem> m_queue;
    std::atomic<Core::u32> m_inFlight{0};
};

} // namespace Tina::Asset
