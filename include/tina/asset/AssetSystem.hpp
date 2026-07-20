#pragma once

#include <tina/asset/AssetStore.hpp>
#include <tina/asset/CatalogLoadPlan.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetBatch.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

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
    // Bounded completion queue capacity for request()/pump(). 0 defaults to storeCapacity.
    Core::usize queueCapacity = 0;
    // Default max completions processed per pump() call. 0 means process the entire queue.
    Core::u32 defaultPumpBudget = 8;
};

struct AssetPumpStats final {
    Core::u32 processed = 0;
    Core::u32 becameReady = 0;
    Core::u32 becameFailed = 0;
    Core::u32 remaining = 0;
};

// Catalog-bound CPU asset facade.
// - load/loadOne: synchronous plan→load→publish (existing product path)
// - request/pump: owner-thread deferred path (Queued→Loading→Ready/Failed) with bounded queue
// No Task worker threads, GPU upload, or LRU in this slice.
class AssetSystem final {
  public:
    AssetSystem() = delete;
    ~AssetSystem() noexcept;

    AssetSystem(const AssetSystem&) = delete;
    AssetSystem& operator=(const AssetSystem&) = delete;
    AssetSystem(AssetSystem&&) noexcept;
    AssetSystem& operator=(AssetSystem&&) = delete;

    [[nodiscard]] static Core::Result<AssetSystem> Create(AssetSystemConfig config);

    [[nodiscard]] Core::Status bindCatalog(std::string_view catalogRootUtf8, CatalogSnapshot catalog);

    [[nodiscard]] bool hasCatalog() const noexcept;
    [[nodiscard]] const CatalogSnapshot* catalog() const noexcept;
    [[nodiscard]] std::string_view catalogRoot() const noexcept;
    [[nodiscard]] AssetStore& store() noexcept;
    [[nodiscard]] const AssetStore& store() const noexcept;
    [[nodiscard]] Core::u32 pendingCount() const noexcept;

    [[nodiscard]] std::optional<AssetHandle> find(Core::AssetId assetId) const noexcept;

    // Synchronous path (unchanged semantics).
    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    load(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> loadOne(Core::AssetId assetId);

    // Deferred path: enqueue dependency-expanded missing assets as Queued handles.
    // Already Ready assets are reused and not re-queued. Empty request enqueues all catalog entries.
    // Returns handles for requested ids only (or all plan rows when empty).
    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    request(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> requestOne(Core::AssetId assetId);

    // Process up to budget queued work items on the owner thread (sync IO for this slice).
    // budget==0 uses defaultPumpBudget; if that is also 0, processes the entire queue.
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
    };

    AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource,
                Core::usize queueCapacity, Core::u32 defaultPumpBudget) noexcept;

    void forgetHandle(AssetHandle handle) noexcept;
    [[nodiscard]] std::optional<Core::u32> findIndex(Core::AssetId assetId) const noexcept;
    [[nodiscard]] Core::Status insertIndex(Core::AssetId assetId, AssetHandle handle);
    void eraseIndexAt(Core::u32 index) noexcept;
    [[nodiscard]] Core::Result<std::pmr::vector<CatalogLoadPlanEntry>>
    planForRequest(std::span<const Core::AssetId> requestedAssetIds);
    [[nodiscard]] Core::Result<AssetHandle> ensureQueued(const CatalogLoadPlanEntry& row);

    AssetStore m_store;
    CookedAssetBatchLoadConfig m_batch{};
    std::pmr::memory_resource* m_memoryResource = nullptr;
    Core::usize m_queueCapacity = 0;
    Core::u32 m_defaultPumpBudget = 0;
    CatalogSnapshot m_catalog{};
    std::pmr::string m_catalogRoot;
    std::pmr::vector<IndexEntry> m_index;
    std::pmr::vector<WorkItem> m_queue;
};

} // namespace Tina::Asset
