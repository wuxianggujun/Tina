#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/PackageFile.hpp>

#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct CookedAssetFileLoadConfig final {
    AssetFormat::CookedAssetLimits assetLimits{};
    // Package views do not copy payloads or inherit the loose-file 256 MiB read limit.
    Core::u64 maxFileBytes = AssetFormat::Wire::MaxCookedFileBytes;
    bool verifyContentHash = true;
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Move-only cooked bytes owner: either an explicit memory buffer or an immutable package pin.
// The parsed wire view is cached; dependency access never reparses the complete file.
class CookedAssetFile final {
  public:
    CookedAssetFile() noexcept = default;
    ~CookedAssetFile() noexcept;

    CookedAssetFile(const CookedAssetFile&) = delete;
    CookedAssetFile& operator=(const CookedAssetFile&) = delete;
    CookedAssetFile(CookedAssetFile&& other) noexcept;
    CookedAssetFile& operator=(CookedAssetFile&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !bytes().empty();
    }
    [[nodiscard]] const AssetFormat::CookedAssetHeader& header() const noexcept
    {
        return m_view.header();
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return m_packageView ? m_packageView.bytes() : std::span<const std::byte>{m_bytes};
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;
    [[nodiscard]] std::optional<AssetFormat::AssetDependency> dependency(Core::u32 index) const noexcept;

  private:
    friend Core::Result<CookedAssetFile> makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>,
                                                                      CookedAssetFileLoadConfig);
    friend Core::Result<CookedAssetFile> makeCookedAssetFileFromPackageView(Core::PackageFileView,
                                                                         CookedAssetFileLoadConfig);

    CookedAssetFile(std::pmr::vector<std::byte> bytes, AssetFormat::CookedAssetView view) noexcept;
    CookedAssetFile(Core::PackageFileView bytes, AssetFormat::CookedAssetView view) noexcept;

    std::pmr::vector<std::byte> m_bytes{};
    Core::PackageFileView m_packageView;
    AssetFormat::CookedAssetView m_view{};
};

[[nodiscard]] Core::Result<CookedAssetFile> loadCookedAssetFile(std::string_view utf8Path,
                                                                CookedAssetFileLoadConfig config);

// Builds an owning CookedAssetFile from already-read bytes (parse + optional content-hash verify).
[[nodiscard]] Core::Result<CookedAssetFile> makeCookedAssetFileFromBytes(std::pmr::vector<std::byte> bytes,
                                                                         CookedAssetFileLoadConfig config);

[[nodiscard]] Core::Result<CookedAssetFile> makeCookedAssetFileFromPackageView(
    Core::PackageFileView bytes, CookedAssetFileLoadConfig config);

// Resolves the virtual object in the pinned catalog package and requires the
// cooked header to match the Catalog entry identity/kind/typeVersion/contentHash/cookedFileBytes.
[[nodiscard]] Core::Result<CookedAssetFile> loadCookedAssetFromCatalog(const CatalogSnapshot& catalog,
                                                                       Core::AssetId assetId,
                                                                       CookedAssetFileLoadConfig config);

} // namespace Tina::Asset
