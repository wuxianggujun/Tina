#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackageLoad.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

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

Bytes makeCookedAsset(Core::u8 assetSeed, AssetFormat::AssetKind kind)
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
    putU16(bytes, 16U, static_cast<Core::u16>(kind));
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

Bytes makeTwoEntryManifest(Core::u64 textureBytes, Core::ContentHash textureHash, Core::u64 materialBytes,
                           Core::ContentHash materialHash)
{
    const auto entryTable = AssetFormat::Wire::CookedManifestHeaderBytes;
    const auto dependencyOffset = entryTable + 2U * AssetFormat::Wire::ManifestEntryBytes;
    const auto fileBytes = dependencyOffset + AssetFormat::Wire::DependencyEntryBytes;
    Bytes bytes(static_cast<Core::usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, AssetFormat::Wire::CookedManifestMagic);
    putU16(bytes, 8U, AssetFormat::Wire::SchemaMajor);
    putU16(bytes, 10U, AssetFormat::Wire::SchemaMinor);
    putU32(bytes, 12U, AssetFormat::Wire::CookedManifestHeaderBytes);
    putU16(bytes, 16U, static_cast<Core::u16>(AssetFormat::TargetPlatform::WindowsX64));
    putU8(bytes, 18U, static_cast<Core::u8>(AssetFormat::EndianTag::Little));
    putU8(bytes, 19U, static_cast<Core::u8>(AssetFormat::HashAlgorithm::Xxh3_128V1));
    putU32(bytes, 24U, 2U);
    putU32(bytes, 28U, AssetFormat::Wire::ManifestEntryBytes);
    putU32(bytes, 32U, 1U);
    putU32(bytes, 36U, AssetFormat::Wire::DependencyEntryBytes);
    putU64(bytes, 40U, entryTable);
    putU64(bytes, 48U, dependencyOffset);
    putU64(bytes, 56U, fileBytes);

    putFixed(bytes, entryTable, idBytes(1U));
    putFixed(bytes, entryTable + 16U, textureHash.bytes());
    putU16(bytes, entryTable + 32U, static_cast<Core::u16>(AssetFormat::AssetKind::Texture2D));
    putU16(bytes, entryTable + 34U, 1U);
    putU32(bytes, entryTable + 40U, 0U);
    putU32(bytes, entryTable + 44U, 0U);
    putU64(bytes, entryTable + 48U, textureBytes);

    const auto materialOffset = entryTable + AssetFormat::Wire::ManifestEntryBytes;
    putFixed(bytes, materialOffset, idBytes(2U));
    putFixed(bytes, materialOffset + 16U, materialHash.bytes());
    putU16(bytes, materialOffset + 32U, static_cast<Core::u16>(AssetFormat::AssetKind::Material));
    putU16(bytes, materialOffset + 34U, 1U);
    putU32(bytes, materialOffset + 40U, 0U);
    putU32(bytes, materialOffset + 44U, 1U);
    putU64(bytes, materialOffset + 48U, materialBytes);

    putFixed(bytes, dependencyOffset, idBytes(1U));
    putU16(bytes, dependencyOffset + 16U, static_cast<Core::u16>(AssetFormat::AssetKind::Texture2D));
    putU16(bytes, dependencyOffset + 18U, static_cast<Core::u16>(AssetFormat::DependencyFlags::Required));
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

TEST(CatalogPackageLoadTests, OpensPackageAndLoadsRequestedChain)
{
    TrackingMemoryResource resource;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto textureBytes = makeCookedAsset(1U, AssetFormat::AssetKind::Texture2D);
    const auto materialBytes = makeCookedAsset(2U, AssetFormat::AssetKind::Material);
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));

    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_load_ok";
    writeBytes(catalogRoot / "manifest.tmnft",
               makeTwoEntryManifest(textureBytes.size(), *digest, materialBytes.size(), *digest));
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Texture2D, textureId)->view()),
               textureBytes);
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Material, materialId)->view()),
               materialBytes);

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &resource,
                    },
            },
        .validateOnOpen = true,
        .validation =
            CatalogPackageValidationConfig{
                .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
                .verifyContent = true,
            },
    };
    CookedAssetBatchLoadConfig batchConfig{
        .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
        .memoryResource = &resource,
    };

    {
        auto loaded =
            loadCookedAssetsFromPackage(toUtf8(catalogRoot), std::array{materialId}, openConfig, batchConfig);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        EXPECT_TRUE(loaded->catalog);
        EXPECT_EQ(loaded->catalog.entryCount(), 2U);
        ASSERT_EQ(loaded->assets.size(), 2U);
        EXPECT_EQ(loaded->assets[0].header().assetId, textureId);
        EXPECT_EQ(loaded->assets[1].header().assetId, materialId);
    }

    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogPackageLoadTests, FailureDoesNotPublishCatalogOrAssets)
{
    TrackingMemoryResource resource;
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto digest = Core::digestContentHashV1(Payload);
    ASSERT_TRUE(digest.has_value());

    const auto textureBytes = makeCookedAsset(1U, AssetFormat::AssetKind::Texture2D);
    const auto materialBytes = makeCookedAsset(2U, AssetFormat::AssetKind::Material);
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto materialId = *Core::AssetId::fromBytes(idBytes(2U));

    const auto catalogRoot = std::filesystem::temp_directory_path() / "tina_package_load_fail";
    writeBytes(catalogRoot / "manifest.tmnft",
               makeTwoEntryManifest(textureBytes.size(), *digest, materialBytes.size(), *digest));
    // Only texture on disk; material missing.
    writeBytes(catalogRoot / std::filesystem::u8path(
                   AssetFormat::makeCookedArtifactPath(AssetFormat::AssetKind::Texture2D, textureId)->view()),
               textureBytes);
    (void)materialBytes;

    CatalogPackageOpenConfig openConfig{
        .manifest =
            CatalogFileLoadConfig{
                .catalog =
                    CatalogConfig{
                        .maxEntries = 8,
                        .maxDependencies = 8,
                        .maxDependenciesPerAsset = 4,
                        .memoryResource = &resource,
                    },
            },
        .validateOnOpen = false,
    };
    CookedAssetBatchLoadConfig batchConfig{
        .file = CookedAssetFileLoadConfig{.memoryResource = &resource},
        .memoryResource = &resource,
    };

    const auto loaded =
        loadCookedAssetsFromPackage(toUtf8(catalogRoot), std::array{materialId}, openConfig, batchConfig);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, Core::CoreErrorCode::NotFound);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);

    std::error_code errorCode;
    std::filesystem::remove_all(catalogRoot, errorCode);
}

} // namespace
} // namespace Tina::Asset
