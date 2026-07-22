#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>
#include <vector>

namespace Tina::Asset::TestSupport {

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

inline void putU8(Bytes& bytes, Core::usize offset, Core::u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}

inline void putU16(Bytes& bytes, Core::usize offset, Core::u16 value)
{
    putU8(bytes, offset, static_cast<Core::u8>(value & 0xFFU));
    putU8(bytes, offset + 1U, static_cast<Core::u8>((value >> 8U) & 0xFFU));
}

inline void putU32(Bytes& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < 4U; ++index)
    {
        putU8(bytes, offset + index, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}

inline void putU64(Bytes& bytes, Core::usize offset, Core::u64 value)
{
    for (Core::usize index = 0; index < 8U; ++index)
    {
        putU8(bytes, offset + index, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}

template <Core::usize Size>
inline void putFixed(Bytes& bytes, Core::usize offset, const std::array<std::byte, Size>& value)
{
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] inline Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] inline Core::AssetId assetId(Core::u8 seed)
{
    return *Core::AssetId::fromBytes(idBytes(seed));
}

[[nodiscard]] inline Core::u64 alignUp(Core::u64 value, Core::u32 alignment)
{
    return (value + alignment - 1U) & ~(static_cast<Core::u64>(alignment) - 1U);
}

[[nodiscard]] inline std::array<std::byte, 4> defaultPayload()
{
    return {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
}

[[nodiscard]] inline Core::ContentHash defaultPayloadHash()
{
    const auto digest = Core::digestContentHashV1(defaultPayload());
    EXPECT_TRUE(digest.has_value());
    return *digest;
}

[[nodiscard]] inline Bytes makeCookedAsset(Core::u8 assetSeed, AssetFormat::AssetKind kind)
{
    constexpr Core::u32 PayloadAlignment = 16U;
    const auto payload = defaultPayload();
    const auto payloadOffset = alignUp(AssetFormat::Wire::CookedAssetHeaderBytes, PayloadAlignment);
    const auto fileBytes = payloadOffset + payload.size();
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
    putU64(bytes, 88U, payload.size());
    putU32(bytes, 96U, PayloadAlignment);
    putU64(bytes, 104U, fileBytes);
    putFixed(bytes, static_cast<Core::usize>(payloadOffset), payload);

    const auto digest = Core::digestContentHashV1(payload);
    EXPECT_TRUE(digest.has_value());
    putFixed(bytes, 48U, digest->bytes());
    return bytes;
}

// Texture seed=1, Material seed=2 depending on texture.
[[nodiscard]] inline Bytes makeTextureMaterialManifest(Core::u64 textureBytes, Core::ContentHash textureHash,
                                                       Core::u64 materialBytes, Core::ContentHash materialHash)
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

inline void writeBytes(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char*>(static_cast<const void*>(bytes.data())),
                 static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] inline std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

struct TextureMaterialPackage final {
    std::filesystem::path root;
    Core::AssetId textureId;
    Core::AssetId materialId;
    Bytes textureBytes;
    Bytes materialBytes;
};

// Writes catalogRoot/manifest.tmnft + deterministic object paths for texture/material chain.
[[nodiscard]] inline TextureMaterialPackage writeTextureMaterialPackage(std::string_view directoryName,
                                                                        bool writeMaterialObject = true)
{
    const auto digest = defaultPayloadHash();
    TextureMaterialPackage package{
        .root = std::filesystem::temp_directory_path() / directoryName,
        .textureId = assetId(1U),
        .materialId = assetId(2U),
        .textureBytes = makeCookedAsset(1U, AssetFormat::AssetKind::Texture2D),
        .materialBytes = makeCookedAsset(2U, AssetFormat::AssetKind::Material),
    };

    writeBytes(package.root / "manifest.tmnft",
               makeTextureMaterialManifest(package.textureBytes.size(), digest, package.materialBytes.size(), digest));
    writeBytes(package.root / std::filesystem::u8path(AssetFormat::makeCookedArtifactPath(
                                                             AssetFormat::AssetKind::Texture2D, package.textureId)
                                                             ->view()),
               package.textureBytes);
    if (writeMaterialObject)
    {
        writeBytes(package.root / std::filesystem::u8path(AssetFormat::makeCookedArtifactPath(
                                                                 AssetFormat::AssetKind::Material, package.materialId)
                                                                 ->view()),
                   package.materialBytes);
    }
    return package;
}

inline void removePackage(const TextureMaterialPackage& package)
{
    std::error_code errorCode;
    std::filesystem::remove_all(package.root, errorCode);
}

} // namespace Tina::Asset::TestSupport
