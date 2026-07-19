#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct CookedAssetFileLoadConfig final {
    AssetFormat::CookedAssetLimits assetLimits{};
    // Capped by Core::MaxReadFileBytes (256 MiB). Wire allows up to 1 GiB, but A2c sync read uses Core limit.
    Core::u64 maxFileBytes = Core::MaxReadFileBytes;
    bool verifyContentHash = true;
    std::pmr::memory_resource* memoryResource = nullptr;
};

// Move-only owning cooked asset bytes. Create paths release no partial object on failure.
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
        return !m_bytes.empty();
    }
    [[nodiscard]] const AssetFormat::CookedAssetHeader& header() const noexcept
    {
        return m_header;
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return m_bytes;
    }
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;
    [[nodiscard]] std::optional<AssetFormat::AssetDependency> dependency(Core::u32 index) const noexcept;

  private:
    friend Core::Result<CookedAssetFile> makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>,
                                                                      CookedAssetFileLoadConfig);

    CookedAssetFile(std::pmr::vector<std::byte> bytes, AssetFormat::CookedAssetHeader header) noexcept;

    std::pmr::vector<std::byte> m_bytes{};
    AssetFormat::CookedAssetHeader m_header{};
};

[[nodiscard]] Core::Result<CookedAssetFile> loadCookedAssetFile(std::string_view utf8Path,
                                                                CookedAssetFileLoadConfig config);

// Resolves objects/<kind>/<aa>/<id>.tasset under catalogRoot, loads the file, and requires the
// cooked header to match the Catalog entry identity/kind/typeVersion/contentHash/cookedFileBytes.
[[nodiscard]] Core::Result<CookedAssetFile> loadCookedAssetFromCatalog(std::string_view catalogRootUtf8,
                                                                       const CatalogSnapshot& catalog,
                                                                       Core::AssetId assetId,
                                                                       CookedAssetFileLoadConfig config);

} // namespace Tina::Asset
