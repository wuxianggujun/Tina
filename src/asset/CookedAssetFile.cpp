#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset/AssetErrors.hpp>
#include <tina/core/trace/Trace.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace Tina::Asset {
namespace {

Core::Result<AssetFormat::CookedAssetView> parseFile(std::span<const std::byte> bytes,
                                                    CookedAssetFileLoadConfig config)
{
    TINA_TRACE_ZONE("Asset.ParseAndVerifyCooked");
    if (config.maxFileBytes == 0 || config.maxFileBytes > AssetFormat::Wire::MaxCookedFileBytes)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid cooked asset byte budget");
    if (bytes.size() > config.maxFileBytes)
        return Core::failure(Core::CoreErrorCode::CapacityExceeded, "cooked asset exceeds configured byte budget");
    auto view = AssetFormat::parseCookedAssetView(bytes, config.assetLimits);
    if (!view) return Core::failure(std::move(view.error()).withContext("CookedAssetFile", "parse"));
    if (config.verifyContentHash)
    {
        auto status = AssetFormat::verifyCookedAssetContentHash(*view);
        if (!status) return Core::failure(std::move(status.error()).withContext("CookedAssetFile", "verify"));
    }
    return *view;
}

Core::Status alignWithCatalogEntry(const CookedAssetFile& asset, const CatalogEntry& entry)
{
    const auto& header = asset.header();
    if (header.assetId != entry.assetId || header.assetKind != entry.assetKind ||
        header.assetTypeVersion != entry.assetTypeVersion || header.contentHash != entry.contentHash ||
        header.fileBytes != entry.cookedFileBytes)
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset does not match catalog identity/kind/version/hash/size");
    return Core::success();
}

} // namespace

CookedAssetFile::CookedAssetFile(std::pmr::vector<std::byte> bytes, AssetFormat::CookedAssetView view) noexcept
    : m_bytes(std::move(bytes)), m_view(view) {}

CookedAssetFile::CookedAssetFile(Core::PackageFileView bytes, AssetFormat::CookedAssetView view) noexcept
    : m_packageView(std::move(bytes)), m_view(view) {}

CookedAssetFile::~CookedAssetFile() noexcept = default;

CookedAssetFile::CookedAssetFile(CookedAssetFile&& other) noexcept
    : m_bytes(std::move(other.m_bytes)), m_packageView(std::move(other.m_packageView)),
      m_view(std::exchange(other.m_view, {})) {}

CookedAssetFile& CookedAssetFile::operator=(CookedAssetFile&& other) noexcept
{
    if (this != &other)
    {
        // PMR move assignment may allocate when resources differ. Reconstruct instead so the
        // owner and allocator transfer together, preserving noexcept and the cached view.
        std::destroy_at(this);
        std::construct_at(this, std::move(other));
    }
    return *this;
}

Core::Result<CookedAssetFile> makeCookedAssetFileFromBytes(std::pmr::vector<std::byte> bytes,
                                                          CookedAssetFileLoadConfig config)
{
    auto view = parseFile(bytes, config);
    if (!view) return Core::failure(std::move(view.error()));
    return CookedAssetFile(std::move(bytes), *view);
}

Core::Result<CookedAssetFile> makeCookedAssetFileFromPackageView(Core::PackageFileView bytes,
                                                                CookedAssetFileLoadConfig config)
{
    auto view = parseFile(bytes.bytes(), config);
    if (!view) return Core::failure(std::move(view.error()));
    return CookedAssetFile(std::move(bytes), *view);
}

std::span<const std::byte> CookedAssetFile::payload() const noexcept
{
    return *this ? m_view.payload() : std::span<const std::byte>{};
}

std::optional<AssetFormat::AssetDependency> CookedAssetFile::dependency(Core::u32 index) const noexcept
{
    return *this ? m_view.dependency(index) : std::nullopt;
}

Core::Result<CookedAssetFile> loadCookedAssetFile(std::string_view utf8Path, CookedAssetFileLoadConfig config)
{
    if (config.memoryResource == nullptr || config.maxFileBytes == 0 ||
        config.maxFileBytes > AssetFormat::Wire::MaxCookedFileBytes)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid standalone cooked asset load config");
    // Standalone file IO is a tooling API. Runtime catalog loads below only use package views.
    auto bytes = Core::readFile(utf8Path, Core::ReadFileConfig{
        .maxBytes = (std::min)(config.maxFileBytes, Core::MaxReadFileBytes), .memoryResource = config.memoryResource});
    if (!bytes) return Core::failure(std::move(bytes.error()).withContext("loadCookedAssetFile", "readFile"));
    return makeCookedAssetFileFromBytes(std::move(*bytes), config);
}

Core::Result<CookedAssetFile> loadCookedAssetFromCatalog(const CatalogSnapshot& catalog, Core::AssetId assetId,
                                                        CookedAssetFileLoadConfig config)
{
    if (!catalog || !assetId)
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot and asset id are required");
    const auto index = catalog.find(assetId);
    if (!index) return Core::failure(Core::CoreErrorCode::NotFound, "asset id is not present in catalog");
    if (!catalog.packageReader())
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "manifest-only catalog has no mounted package");
    const auto entry = catalog.entry(*index);
    if (!entry) return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry missing after find");
    auto artifact = AssetFormat::makeCookedArtifactPath(entry->assetKind, entry->assetId);
    if (!artifact) return Core::failure(std::move(artifact.error()));
    auto bytes = catalog.packageReader().viewFile(artifact->view(), config.maxFileBytes);
    if (!bytes) return Core::failure(std::move(bytes.error()).withContext("loadCookedAssetFromCatalog", artifact->view()));
    auto asset = makeCookedAssetFileFromPackageView(std::move(*bytes), config);
    if (!asset) return Core::failure(std::move(asset.error()));
    if (auto status = alignWithCatalogEntry(*asset, *entry); !status) return Core::failure(std::move(status.error()));
    return std::move(*asset);
}

} // namespace Tina::Asset
