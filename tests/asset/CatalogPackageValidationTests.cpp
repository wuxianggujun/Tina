#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageValidation.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/TileMapChunkPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

using Bytes = std::vector<std::byte>;

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_outstandingAllocations;
        return pointer;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --m_outstandingAllocations;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::size_t m_outstandingAllocations = 0;
};

void putU8(Bytes& bytes, Core::usize offset, Core::u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}
void putU16(Bytes& bytes, Core::usize offset, Core::u16 value)
{
    putU8(bytes, offset, static_cast<Core::u8>(value & 0xFFU));
    putU8(bytes, offset + 1U, static_cast<Core::u8>((value >> 8U) & 0xFFU));
}
void putU32(Bytes& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < 4U; ++index)
    {
        putU8(bytes, offset + index, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}
void putU64(Bytes& bytes, Core::usize offset, Core::u64 value)
{
    for (Core::usize index = 0; index < 8U; ++index)
    {
        putU8(bytes, offset + index, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}
template <Core::usize Size> void putFixed(Bytes& bytes, Core::usize offset, const std::array<std::byte, Size>& value)
{
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

Core::u64 alignUp(Core::u64 value, Core::u32 alignment)
{
    return (value + alignment - 1U) & ~(static_cast<Core::u64>(alignment) - 1U);
}

Bytes makeCookedSprite(Core::u8 assetSeed)
{
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    constexpr Core::u32 PayloadAlignment = 16U;
    const auto payloadOffset = alignUp(AssetFormat::Wire::CookedAssetHeaderBytes, PayloadAlignment);
    const auto fileBytes = payloadOffset + Payload.size();
    Bytes bytes(static_cast<Core::usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, AssetFormat::Wire::CookedAssetMagic);
    putU16(bytes, 8U, AssetFormat::Wire::SchemaMajor);
    putU16(bytes, 10U, AssetFormat::Wire::SchemaMinor);
    putU32(bytes, 12U, AssetFormat::Wire::CookedAssetHeaderBytes);
    putU16(bytes, 16U, static_cast<Core::u16>(AssetFormat::AssetKind::Sprite));
    putU16(bytes, 18U, 1U);
    putU16(bytes, 20U, static_cast<Core::u16>(AssetFormat::TargetPlatform::WindowsX64));
    putU8(bytes, 22U, static_cast<Core::u8>(AssetFormat::EndianTag::Little));
    putU8(bytes, 23U, static_cast<Core::u8>(AssetFormat::HashAlgorithm::Xxh3_128V1));
    putFixed(bytes, 32U, idBytes(assetSeed));
    putU64(bytes, 64U, AssetFormat::Wire::CookedAssetHeaderBytes);
    putU32(bytes, 72U, 0U);
    putU32(bytes, 76U, AssetFormat::Wire::DependencyEntryBytes);
    putU64(bytes, 80U, payloadOffset);
    putU64(bytes, 88U, Payload.size());
    putU32(bytes, 96U, PayloadAlignment);
    putU64(bytes, 104U, fileBytes);
    putFixed(bytes, static_cast<Core::usize>(payloadOffset), Payload);

    const auto digest = Core::digestContentHashV1(Payload);
    EXPECT_TRUE(digest.has_value());
    putFixed(bytes, 48U, digest->bytes());
    return bytes;
}

Bytes makeSingleSpriteManifest(Core::u8 assetSeed, Core::u64 cookedFileBytes, Core::ContentHash contentHash)
{
    Bytes bytes(AssetFormat::Wire::CookedManifestHeaderBytes + AssetFormat::Wire::ManifestEntryBytes, std::byte{0});
    putFixed(bytes, 0U, AssetFormat::Wire::CookedManifestMagic);
    putU16(bytes, 8U, AssetFormat::Wire::SchemaMajor);
    putU16(bytes, 10U, AssetFormat::Wire::SchemaMinor);
    putU32(bytes, 12U, AssetFormat::Wire::CookedManifestHeaderBytes);
    putU16(bytes, 16U, static_cast<Core::u16>(AssetFormat::TargetPlatform::WindowsX64));
    putU8(bytes, 18U, static_cast<Core::u8>(AssetFormat::EndianTag::Little));
    putU8(bytes, 19U, static_cast<Core::u8>(AssetFormat::HashAlgorithm::Xxh3_128V1));
    putU32(bytes, 24U, 1U);
    putU32(bytes, 28U, AssetFormat::Wire::ManifestEntryBytes);
    putU32(bytes, 32U, 0U);
    putU32(bytes, 36U, AssetFormat::Wire::DependencyEntryBytes);
    putU64(bytes, 40U, AssetFormat::Wire::CookedManifestHeaderBytes);
    putU64(bytes, 48U, AssetFormat::Wire::CookedManifestHeaderBytes + AssetFormat::Wire::ManifestEntryBytes);
    putU64(bytes, 56U, bytes.size());

    const auto offset = AssetFormat::Wire::CookedManifestHeaderBytes;
    putFixed(bytes, offset, idBytes(assetSeed));
    putFixed(bytes, offset + 16U, contentHash.bytes());
    putU16(bytes, offset + 32U, static_cast<Core::u16>(AssetFormat::AssetKind::Sprite));
    putU16(bytes, offset + 34U, 1U);
    putU32(bytes, offset + 40U, 0U);
    putU32(bytes, offset + 44U, 0U);
    putU64(bytes, offset + 48U, cookedFileBytes);
    return bytes;
}

Core::Result<CatalogSnapshot> makeSingleSpriteCatalog(Core::u8 assetSeed, Core::u64 cookedFileBytes,
                                                      Core::ContentHash contentHash,
                                                      std::pmr::memory_resource& resource)
{
    const auto manifestBytes = makeSingleSpriteManifest(assetSeed, cookedFileBytes, contentHash);
    auto manifest = AssetFormat::parseCookedManifestView(manifestBytes);
    if (!manifest)
    {
        return Core::failure(std::move(manifest.error()));
    }
    return CatalogSnapshot::Create(*manifest, CatalogConfig{.maxEntries = 4,
                                                             .maxDependencies = 4,
                                                             .maxDependenciesPerAsset = 2,
                                                             .memoryResource = &resource});
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char*>(static_cast<const void*>(bytes.data())),
                 static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

void resetDirectory(const std::filesystem::path& path)
{
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
}

TEST(CatalogPackageValidationTests, AcceptsCompletePackage)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 Seed = 0x21U;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto cooked = makeCookedSprite(Seed);
    const auto assetId = *Core::AssetId::fromBytes(idBytes(Seed));
    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_valid";
    resetDirectory(catalogRoot);
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Sprite, assetId)->view()),
               cooked);

    auto catalog = makeSingleSpriteCatalog(Seed, cooked.size(), *digest, resource);
    ASSERT_TRUE(catalog.has_value());

    CatalogPackageValidationConfig config{
        .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
        .verifyContent = true,
    };
    const auto status = validateCatalogPackageOnDisk(toUtf8(catalogRoot), *catalog, config);
    ASSERT_TRUE(status.has_value()) << status.error().message;

    catalog = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackageValidationTests, RejectsMissingObjectFile)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 Seed = 0x22U;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto cooked = makeCookedSprite(Seed);
    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_missing";
    resetDirectory(catalogRoot);
    // Do not write object file.

    auto catalog = makeSingleSpriteCatalog(Seed, cooked.size(), *digest, resource);
    ASSERT_TRUE(catalog.has_value());

    CatalogPackageValidationConfig config{.verifyContent = false};
    const auto status = validateCatalogPackageOnDisk(toUtf8(catalogRoot), *catalog, config);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Core::CoreErrorCode::NotFound);
    const auto expectedAssetId = Core::AssetId::fromBytes(idBytes(Seed))->canonicalText();
    const auto assetContext = std::find_if(status.error().context.begin(), status.error().context.end(),
                                           [](const Core::ErrorContext& context) {
                                               return context.operation == "assetId";
                                           });
    ASSERT_NE(assetContext, status.error().context.end());
    EXPECT_EQ(assetContext->detail, std::string(expectedAssetId.data(), expectedAssetId.size()));

    catalog = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackageValidationTests, RejectsSizeMismatchWithoutContentVerify)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 Seed = 0x23U;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto cooked = makeCookedSprite(Seed);
    const auto assetId = *Core::AssetId::fromBytes(idBytes(Seed));
    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_size";
    resetDirectory(catalogRoot);
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Sprite, assetId)->view()),
               cooked);

    // Lie about size in catalog entry.
    auto catalog = makeSingleSpriteCatalog(Seed, cooked.size() + 8U, *digest, resource);
    ASSERT_TRUE(catalog.has_value());

    CatalogPackageValidationConfig config{.verifyContent = false};
    const auto status = validateCatalogPackageOnDisk(toUtf8(catalogRoot), *catalog, config);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::CatalogEntryMismatch);

    catalog = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackageValidationTests, MetadataOnlyAcceptsButFullValidationRejectsSameSizeCorruption)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 Seed = 0x24U;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    auto cooked = makeCookedSprite(Seed);
    cooked.back() ^= std::byte{0x01};
    const auto assetId = *Core::AssetId::fromBytes(idBytes(Seed));
    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_content";
    resetDirectory(catalogRoot);
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Sprite, assetId)->view()),
               cooked);

    auto catalog = makeSingleSpriteCatalog(Seed, cooked.size(), *digest, resource);
    ASSERT_TRUE(catalog.has_value());

    const auto metadataOnly = validateCatalogPackageOnDisk(
        toUtf8(catalogRoot), *catalog, CatalogPackageValidationConfig{.verifyContent = false});
    ASSERT_TRUE(metadataOnly.has_value()) << metadataOnly.error().message;

    CatalogPackageValidationConfig fullValidation{
        .file = CookedAssetFileLoadConfig{.verifyContentHash = false, .memoryResource = &resource},
        .verifyContent = true,
    };
    const auto full = validateCatalogPackageOnDisk(toUtf8(catalogRoot), *catalog, fullValidation);
    ASSERT_FALSE(full.has_value());
    EXPECT_EQ(full.error().code, AssetFormat::AssetFormatErrorCode::ContentHashMismatch);

    catalog = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackageValidationTests, TypedValidationRejectsSelfConsistentMalformedTileMapChunk)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 ChunkSeed = 0x26U;
    constexpr Core::u8 ParentSeed = 0x27U;
    const auto chunkAssetId = Core::AssetId::fromBytes(idBytes(ChunkSeed));
    const auto parentAssetId = Core::AssetId::fromBytes(idBytes(ParentSeed));
    ASSERT_TRUE(chunkAssetId.has_value());
    ASSERT_TRUE(parentAssetId.has_value());

    constexpr std::array<Core::u16, 1> Cells{1U};
    auto payload = AssetFormat::writeTileMapChunkPayloadBytes(AssetFormat::TileMapChunkPayloadDesc{
        .parentTileMapId = *parentAssetId,
        .layerId = 10U,
        .chunkX = 0U,
        .chunkY = 0U,
        .widthCells = 1U,
        .heightCells = 1U,
        .cells = Cells,
    });
    ASSERT_TRUE(payload.has_value()) << (payload ? "" : payload.error().message);
    ASSERT_EQ(payload->size(), AssetFormat::TileMapChunkWire::HeaderBytes + sizeof(Core::u16));

    // Keep the generic cooked file valid while violating the typed chunk schema.
    // Offset 44 is the reserved u32 in TileMapChunk payload v1.
    putU32(*payload, 44U, 1U);
    const auto digest = Core::digestContentHashV1(*payload);
    ASSERT_TRUE(digest.has_value());

    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::TileMapChunk,
        .assetTypeVersion = AssetFormat::TileMapChunkWire::SchemaVersion,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .assetId = *chunkAssetId,
        .payload = *payload,
    });
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    const std::array<AssetFormat::CookedManifestWriteEntry, 1> entries{{
        AssetFormat::CookedManifestWriteEntry{
            .assetId = *chunkAssetId,
            .contentHash = *digest,
            .assetKind = AssetFormat::AssetKind::TileMapChunk,
            .assetTypeVersion = AssetFormat::TileMapChunkWire::SchemaVersion,
            .cookedFileBytes = cooked->size(),
        },
    }};
    auto manifestBytes = AssetFormat::writeCookedManifestBytes(
        AssetFormat::CookedManifestWriteDesc{.entries = entries});
    ASSERT_TRUE(manifestBytes.has_value()) << (manifestBytes ? "" : manifestBytes.error().message);
    auto manifest = AssetFormat::parseCookedManifestView(*manifestBytes);
    ASSERT_TRUE(manifest.has_value()) << (manifest ? "" : manifest.error().message);
    auto catalog = CatalogSnapshot::Create(*manifest, CatalogConfig{
                                                          .maxEntries = 4,
                                                          .maxDependencies = 4,
                                                          .maxDependenciesPerAsset = 2,
                                                          .memoryResource = &resource,
                                                      });
    ASSERT_TRUE(catalog.has_value()) << (catalog ? "" : catalog.error().message);

    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_typed_tilemap_chunk";
    resetDirectory(catalogRoot);
    writeBytes(catalogRoot / std::filesystem::u8path(DefaultCatalogManifestRelativePath),
               *manifestBytes);
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::TileMapChunk,
                                                       *chunkAssetId)
                       ->view()),
               *cooked);

    CatalogPackageValidationConfig config{
        .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
        .verifyContent = true,
        .verifyTypedPayload = false,
    };
    const auto raw = validateCatalogPackageOnDisk(toUtf8(catalogRoot), *catalog, config);
    ASSERT_TRUE(raw.has_value()) << (raw ? "" : raw.error().message);

    config.verifyTypedPayload = true;
    const auto typed = validateCatalogPackageOnDisk(toUtf8(catalogRoot), *catalog, config);
    ASSERT_FALSE(typed.has_value());
    EXPECT_EQ(typed.error().code, AssetFormat::AssetFormatErrorCode::UnsupportedValue);
    const auto phaseContext = std::find_if(typed.error().context.begin(), typed.error().context.end(),
                                           [](const Core::ErrorContext& context) {
                                               return context.operation == "validateCatalogPackageOnDisk";
                                           });
    ASSERT_NE(phaseContext, typed.error().context.end());
    EXPECT_EQ(phaseContext->detail, "typedTileMapChunk");

    {
        auto system = AssetSystem::Create(AssetSystemConfig{
            .storeCapacity = 4,
            .memoryResource = &resource,
            .requireTyped2dPayloads = true,
        });
        ASSERT_TRUE(system.has_value()) << (system ? "" : system.error().message);
        const auto bound = system->openAndBindCatalog(toUtf8(catalogRoot));
        ASSERT_FALSE(bound.has_value());
        EXPECT_EQ(bound.error().code, AssetFormat::AssetFormatErrorCode::UnsupportedValue);
        const auto requiredTypedContext =
            std::find_if(bound.error().context.begin(), bound.error().context.end(),
                         [](const Core::ErrorContext& context) {
                             return context.operation == "validateCatalogPackageOnDisk";
                         });
        ASSERT_NE(requiredTypedContext, bound.error().context.end());
        EXPECT_EQ(requiredTypedContext->detail, "typedTileMapChunk");
    }

    catalog = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackageValidationTests, RejectsInvalidUtf8CatalogRootBeforeFilesystemAccess)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 Seed = 0x25U;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto cooked = makeCookedSprite(Seed);
    auto catalog = makeSingleSpriteCatalog(Seed, cooked.size(), *digest, resource);
    ASSERT_TRUE(catalog.has_value());

    const std::string invalidUtf8Root{static_cast<char>(0xC3), '('};
    const auto status = validateCatalogPackageOnDisk(
        invalidUtf8Root, *catalog, CatalogPackageValidationConfig{.verifyContent = false});
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetErrorCode::InvalidCatalogConfig);

    catalog = CatalogSnapshot{};
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

} // namespace
} // namespace Tina::Asset
