#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include "support/Utf8Path.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>
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

Bytes makeCookedAssetWithMatchingHash(Core::u8 assetSeed = 0x40U)
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

Bytes makeManifestForSprite(Core::u8 assetSeed, Core::u64 cookedFileBytes, Core::ContentHash contentHash)
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

TEST(CookedAssetFileTests, LoadsMatchingCookedAssetFile)
{
    TrackingMemoryResource resource;
    const auto bytes = makeCookedAssetWithMatchingHash();
    const auto directory = std::filesystem::temp_directory_path() / "tina_cooked_asset_tests";
    const auto path = directory / "asset.tasset";
    writeBytes(path, bytes);

    CookedAssetFileLoadConfig config{.memoryResource = &resource};
    {
        auto asset = loadCookedAssetFile(toUtf8(path), config);
        ASSERT_TRUE(asset.has_value()) << asset.error().message;
        EXPECT_EQ(asset->header().assetKind, AssetFormat::AssetKind::Sprite);
        ASSERT_EQ(asset->payload().size(), 4U);
        EXPECT_EQ(asset->payload()[0], std::byte{0x10});
    }
    std::error_code errorCode;
    std::filesystem::remove(path, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CookedAssetFileTests, RejectsContentHashMismatch)
{
    TrackingMemoryResource resource;
    auto bytes = makeCookedAssetWithMatchingHash();
    // Corrupt payload after hash is fixed.
    bytes.back() = std::byte{0xFF};
    const auto directory = std::filesystem::temp_directory_path() / "tina_cooked_asset_tests";
    const auto path = directory / "bad_hash.tasset";
    writeBytes(path, bytes);

    CookedAssetFileLoadConfig config{.memoryResource = &resource};
    const auto asset = loadCookedAssetFile(toUtf8(path), config);
    ASSERT_FALSE(asset.has_value());
    EXPECT_EQ(asset.error().code, AssetFormat::AssetFormatErrorCode::ContentHashMismatch);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
    std::error_code errorCode;
    std::filesystem::remove(path, errorCode);
}

TEST(CookedAssetFileTests, LoadsFromCatalogRootUsingDeterministicPath)
{
    TrackingMemoryResource resource;
    constexpr Core::u8 Seed = 0xABU;
    const auto cookedBytes = makeCookedAssetWithMatchingHash(Seed);
    const auto assetId = *Core::AssetId::fromBytes(idBytes(Seed));
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_catalog_root_tests";
    std::filesystem::create_directories(catalogRoot);

    const auto artifact = AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Sprite, assetId);
    ASSERT_TRUE(artifact.has_value());
    writeBytes(catalogRoot / Tina::TestSupport::pathFromUtf8Bytes(artifact->view()), cookedBytes);

    const auto manifestBytes = makeManifestForSprite(Seed, cookedBytes.size(), *digest);
    auto manifest = AssetFormat::parseCookedManifestView(manifestBytes);
    ASSERT_TRUE(manifest.has_value());
    auto catalog = CatalogSnapshot::Create(*manifest, CatalogConfig{.maxEntries = 8,
                                                                    .maxDependencies = 8,
                                                                    .maxDependenciesPerAsset = 4,
                                                                    .memoryResource = &resource});
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;

    CookedAssetFileLoadConfig config{.memoryResource = &resource};
    {
        auto asset = loadCookedAssetFromCatalog(toUtf8(catalogRoot), *catalog, assetId, config);
        ASSERT_TRUE(asset.has_value()) << asset.error().message;
        EXPECT_EQ(asset->header().assetId, assetId);
        EXPECT_EQ(asset->header().contentHash, *digest);
    }
    catalog = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CookedAssetFileTests, MissingCatalogAssetIdFailsWithoutPublish)
{
    TrackingMemoryResource resource;
    const auto manifestBytes = makeManifestForSprite(0x10U, 128U, *Core::ContentHash::fromBytes([] {
        Core::ContentHash::Bytes bytes{};
        bytes[0] = std::byte{1};
        return bytes;
    }()));
    auto manifest = AssetFormat::parseCookedManifestView(manifestBytes);
    ASSERT_TRUE(manifest.has_value());
    auto catalog = CatalogSnapshot::Create(*manifest, CatalogConfig{.maxEntries = 8,
                                                                    .maxDependencies = 8,
                                                                    .maxDependenciesPerAsset = 4,
                                                                    .memoryResource = &resource});
    ASSERT_TRUE(catalog.has_value());

    CookedAssetFileLoadConfig config{.memoryResource = &resource};
    const auto missing = *Core::AssetId::fromBytes(idBytes(0x99U));
    const auto asset = loadCookedAssetFromCatalog("C:/tina_catalog_root", *catalog, missing, config);
    ASSERT_FALSE(asset.has_value());
    EXPECT_EQ(asset.error().code, Core::CoreErrorCode::NotFound);
}

} // namespace
} // namespace Tina::Asset
