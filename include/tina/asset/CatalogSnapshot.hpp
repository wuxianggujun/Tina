#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/id/AssetId.hpp>

#include <memory_resource>
#include <optional>

namespace Tina::Asset {

struct CatalogConfig final {
    Core::u32 maxEntries = 0;
    Core::u32 maxDependencies = 0;
    Core::u32 maxDependenciesPerAsset = 0;
    std::pmr::memory_resource* memoryResource = nullptr;
};

struct CatalogEntry final {
    Core::AssetId assetId;
    Core::ContentHash contentHash;
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::u16 assetTypeVersion = 0;
    Core::u32 dependencyCount = 0;
    Core::u64 cookedFileBytes = 0;
};

struct CatalogDependency final {
    Core::AssetId assetId;
    Core::u32 targetEntryIndex = 0;
    AssetFormat::AssetKind expectedKind = AssetFormat::AssetKind::Invalid;
    AssetFormat::DependencyFlags flags = AssetFormat::DependencyFlags::None;
};

// Move-only immutable owning catalog. Create copies only entry/dependency values from a
// validated CookedManifestView; the original Manifest byte buffer may be destroyed afterward.
class CatalogSnapshot final {
  public:
    CatalogSnapshot() noexcept = default;
    ~CatalogSnapshot() noexcept;

    CatalogSnapshot(const CatalogSnapshot&) = delete;
    CatalogSnapshot& operator=(const CatalogSnapshot&) = delete;
    CatalogSnapshot(CatalogSnapshot&& other) noexcept;
    CatalogSnapshot& operator=(CatalogSnapshot&& other) noexcept;

    [[nodiscard]] static Core::Result<CatalogSnapshot> Create(const AssetFormat::CookedManifestView& manifest,
                                                              CatalogConfig config);

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_resource != nullptr;
    }
    [[nodiscard]] Core::u32 entryCount() const noexcept
    {
        return m_entryCount;
    }
    [[nodiscard]] Core::u32 dependencyCount() const noexcept
    {
        return m_dependencyCount;
    }

    [[nodiscard]] std::optional<Core::u32> find(Core::AssetId assetId) const noexcept;
    [[nodiscard]] std::optional<CatalogEntry> entry(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<CatalogDependency> dependency(Core::u32 entryIndex,
                                                              Core::u32 dependencyIndex) const noexcept;

  private:
    struct StoredEntry final {
        Core::AssetId assetId;
        Core::ContentHash contentHash;
        AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
        Core::u16 assetTypeVersion = 0;
        Core::u32 dependencyFirst = 0;
        Core::u32 dependencyCount = 0;
        Core::u64 cookedFileBytes = 0;
    };

    struct StoredDependency final {
        Core::AssetId assetId;
        Core::u32 targetEntryIndex = 0;
        AssetFormat::AssetKind expectedKind = AssetFormat::AssetKind::Invalid;
        AssetFormat::DependencyFlags flags = AssetFormat::DependencyFlags::None;
    };

    CatalogSnapshot(std::pmr::memory_resource* resource, StoredEntry* entries, Core::u32 entryCount,
                    StoredDependency* dependencies, Core::u32 dependencyCount) noexcept;

    void reset() noexcept;

    std::pmr::memory_resource* m_resource = nullptr;
    StoredEntry* m_entries = nullptr;
    StoredDependency* m_dependencies = nullptr;
    Core::u32 m_entryCount = 0;
    Core::u32 m_dependencyCount = 0;
};

} // namespace Tina::Asset
