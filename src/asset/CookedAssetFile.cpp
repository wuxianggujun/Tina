#include <tina/asset/CookedAssetFile.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool containsEmbeddedNul(std::string_view text) noexcept
{
    return text.find('\0') != std::string_view::npos;
}

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

[[nodiscard]] Core::Status alignWithCatalogEntry(const CookedAssetFile& asset, const CatalogEntry& entry)
{
    if (asset.header().assetId != entry.assetId)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset id does not match catalog entry");
    }
    if (asset.header().assetKind != entry.assetKind)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset kind does not match catalog entry");
    }
    if (asset.header().assetTypeVersion != entry.assetTypeVersion)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset type version does not match catalog entry");
    }
    if (asset.header().contentHash != entry.contentHash)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset content hash does not match catalog entry");
    }
    if (asset.header().fileBytes != entry.cookedFileBytes)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked asset file bytes does not match catalog entry");
    }
    return Core::success();
}

} // namespace

CookedAssetFile::CookedAssetFile(std::pmr::vector<std::byte> bytes, AssetFormat::CookedAssetHeader header) noexcept
    : m_bytes(std::move(bytes)), m_header(header)
{
}

CookedAssetFile::~CookedAssetFile() noexcept
{
    m_bytes.clear();
    m_bytes.shrink_to_fit();
    m_header = {};
}

CookedAssetFile::CookedAssetFile(CookedAssetFile&& other) noexcept
    : m_bytes(std::move(other.m_bytes)), m_header(other.m_header)
{
    other.m_header = {};
}

CookedAssetFile& CookedAssetFile::operator=(CookedAssetFile&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_bytes = std::move(other.m_bytes);
    m_header = other.m_header;
    other.m_header = {};
    // Ensure this object does not retain a foreign empty allocator container with unpaid capacity.
    other.m_bytes = std::pmr::vector<std::byte>{};
    return *this;
}

Core::Result<CookedAssetFile> makeCookedAssetFileFromBytes(std::pmr::vector<std::byte> bytes,
                                                           CookedAssetFileLoadConfig config)
{
    auto view = AssetFormat::parseCookedAssetView(bytes, config.assetLimits);
    if (!view)
    {
        return Core::failure(std::move(view.error()).withContext("loadCookedAssetFile", "parseCookedAssetView"));
    }

    if (config.verifyContentHash)
    {
        auto status = AssetFormat::verifyCookedAssetContentHash(*view);
        if (!status)
        {
            return Core::failure(
                std::move(status.error()).withContext("loadCookedAssetFile", "verifyCookedAssetContentHash"));
        }
    }

    return CookedAssetFile(std::move(bytes), view->header());
}

std::span<const std::byte> CookedAssetFile::payload() const noexcept
{
    if (m_bytes.empty() || m_header.payloadBytes == 0)
    {
        return {};
    }
    if (m_header.payloadOffset > m_bytes.size() ||
        m_header.payloadBytes > m_bytes.size() - static_cast<std::size_t>(m_header.payloadOffset))
    {
        return {};
    }
    return std::span<const std::byte>(m_bytes.data() + static_cast<std::size_t>(m_header.payloadOffset),
                                      static_cast<std::size_t>(m_header.payloadBytes));
}

std::optional<AssetFormat::AssetDependency> CookedAssetFile::dependency(Core::u32 index) const noexcept
{
    if (m_bytes.empty() || index >= m_header.dependencyCount)
    {
        return std::nullopt;
    }
    const auto view = AssetFormat::parseCookedAssetView(m_bytes);
    if (!view)
    {
        return std::nullopt;
    }
    return view->dependency(index);
}

Core::Result<CookedAssetFile> loadCookedAssetFile(std::string_view utf8Path, CookedAssetFileLoadConfig config)
{
    if (config.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset load requires memory resource");
    }
    if (config.maxFileBytes == 0 || config.maxFileBytes > Core::MaxReadFileBytes ||
        config.maxFileBytes > AssetFormat::Wire::MaxCookedFileBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "invalid cooked asset maxFileBytes");
    }

    Core::ReadFileConfig readConfig{
        .maxBytes = config.maxFileBytes,
        .memoryResource = config.memoryResource,
    };
    auto fileBytes = Core::readFile(utf8Path, readConfig);
    if (!fileBytes)
    {
        return Core::failure(std::move(fileBytes.error()).withContext("loadCookedAssetFile", "readFile"));
    }
    return makeCookedAssetFileFromBytes(std::move(*fileBytes), config);
}

Core::Result<CookedAssetFile> loadCookedAssetFromCatalog(std::string_view catalogRootUtf8,
                                                         const CatalogSnapshot& catalog, Core::AssetId assetId,
                                                         CookedAssetFileLoadConfig config)
{
    if (!catalog)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog snapshot is empty");
    }
    if (!assetId)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "asset id is required");
    }
    if (catalogRootUtf8.empty() || containsEmbeddedNul(catalogRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog root path is invalid");
    }

    const auto entryIndex = catalog.find(assetId);
    if (!entryIndex)
    {
        return Core::failure(Core::CoreErrorCode::NotFound, "asset id is not present in catalog");
    }
    const auto entry = catalog.entry(*entryIndex);
    if (!entry)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "catalog entry is missing after find");
    }

    auto artifactPath = AssetFormat::makeCookedArtifactPath(entry->assetKind, entry->assetId);
    if (!artifactPath)
    {
        return Core::failure(
            std::move(artifactPath.error()).withContext("loadCookedAssetFromCatalog", "makeCookedArtifactPath"));
    }

    const auto root = Detail::pathFromUtf8Bytes(catalogRootUtf8);
    const auto relative = Detail::pathFromUtf8Bytes(artifactPath->view());
    if (relative.is_absolute() || hasPathEscapeComponent(relative))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "artifact relative path is not safe");
    }

    const auto fullPath = root / relative;
    const auto generic = fullPath.generic_u8string();
    const std::string utf8Path(generic.begin(), generic.end());

    auto asset = loadCookedAssetFile(utf8Path, config);
    if (!asset)
    {
        return Core::failure(
            std::move(asset.error()).withContext("loadCookedAssetFromCatalog", "loadCookedAssetFile"));
    }
    if (const auto status = alignWithCatalogEntry(*asset, *entry); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return std::move(*asset);
}

} // namespace Tina::Asset
