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
};

// Catalog-bound CPU asset facade for the next M10 product slice.
// Owner-thread only. Synchronous load path: plan → load cooked files → publish into AssetStore.
// Keeps a stable AssetId → AssetHandle index for dedupe. No async Task/IO, GPU upload, or LRU.
class AssetSystem final {
  public:
    AssetSystem() = delete;
    ~AssetSystem() noexcept;

    AssetSystem(const AssetSystem&) = delete;
    AssetSystem& operator=(const AssetSystem&) = delete;
    AssetSystem(AssetSystem&&) noexcept;
    AssetSystem& operator=(AssetSystem&&) = delete;

    [[nodiscard]] static Core::Result<AssetSystem> Create(AssetSystemConfig config);

    // Binds an owning CatalogSnapshot and UTF-8 catalog root used for cooked object paths.
    // Replaces any previous catalog binding. Existing published assets are left intact.
    [[nodiscard]] Core::Status bindCatalog(std::string_view catalogRootUtf8, CatalogSnapshot catalog);

    [[nodiscard]] bool hasCatalog() const noexcept;
    [[nodiscard]] const CatalogSnapshot* catalog() const noexcept;
    [[nodiscard]] std::string_view catalogRoot() const noexcept;
    [[nodiscard]] AssetStore& store() noexcept;
    [[nodiscard]] const AssetStore& store() const noexcept;

    // Weak lookup by logical AssetId among currently published Ready/UnloadPending slots.
    [[nodiscard]] std::optional<AssetHandle> find(Core::AssetId assetId) const noexcept;

    // Synchronously loads the dependency-expanded plan for requested ids (empty = all catalog
    // entries), publishes missing Ready payloads, and returns handles for the requested ids only
    // (or all plan rows when requested is empty). Already-published ids are reused.
    // On failure, rolls back only handles published by this call.
    [[nodiscard]] Core::Result<std::pmr::vector<AssetHandle>>
    load(std::span<const Core::AssetId> requestedAssetIds);

    [[nodiscard]] Core::Result<AssetHandle> loadOne(Core::AssetId assetId);

    [[nodiscard]] const CookedAssetFile* tryGet(AssetHandle handle) const noexcept;
    [[nodiscard]] Core::Result<AssetLease> acquire(AssetHandle handle);
    [[nodiscard]] Core::Status unload(AssetHandle handle) noexcept;

  private:
    struct IndexEntry final {
        Core::AssetId assetId{};
        AssetHandle handle{};
    };

    AssetSystem(AssetStore store, CookedAssetBatchLoadConfig batch, std::pmr::memory_resource* memoryResource) noexcept;

    void forgetHandle(AssetHandle handle) noexcept;
    [[nodiscard]] std::optional<Core::u32> findIndex(Core::AssetId assetId) const noexcept;
    [[nodiscard]] Core::Status insertIndex(Core::AssetId assetId, AssetHandle handle);
    void eraseIndexAt(Core::u32 index) noexcept;

    AssetStore m_store;
    CookedAssetBatchLoadConfig m_batch{};
    std::pmr::memory_resource* m_memoryResource = nullptr;
    CatalogSnapshot m_catalog{};
    std::pmr::string m_catalogRoot;
    std::pmr::vector<IndexEntry> m_index;
};

} // namespace Tina::Asset
