#pragma once

#include <tina/core/base/Types.hpp>

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/id/AssetId.hpp>

#include "support/Utf8Path.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Asset::TestSupport {

using Bytes = std::vector<std::byte>;

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] Tina::Core::usize outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

    [[nodiscard]] Tina::Core::usize allocationCalls() const noexcept
    {
        return m_allocationCalls;
    }

  private:
    void* do_allocate(Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCalls;
        ++m_outstandingAllocations;
        return pointer;
    }

    void do_deallocate(void* pointer, Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --m_outstandingAllocations;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    Tina::Core::usize m_outstandingAllocations = 0;
    Tina::Core::usize m_allocationCalls = 0;
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
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<Tina::Core::isize>(offset));
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
    // Taken from the wire constant, not a literal: the loader cross-checks this against
    // the cooked header, so a hand-written version silently fails every load after a bump.
    putU16(bytes, entryTable + 34U, AssetFormat::Texture2DWire::SchemaVersion);
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

// Explicit package publication: never mirror loose fixture files into a mounted catalog.
inline Core::Status writePackage(const std::filesystem::path& root, std::span<const std::byte> manifest,
                                 std::span<const CatalogPackageObjectBlob> objects = {},
                                 std::string_view relativePath = DefaultCatalogPackageRelativePath)
{
    return publishCatalogPackage(toUtf8(root), relativePath, manifest, objects);
}

// Atomically publish a new package generation, preserving the old reader's pinned bytes.
inline Core::Status replacePackageEntry(const std::filesystem::path& root, std::string_view path,
                                        std::optional<std::span<const std::byte>> replacement)
{
    const auto packagePath = toUtf8(root / Tina::TestSupport::pathFromUtf8Bytes(DefaultCatalogPackageRelativePath));
    auto reader = Core::PackageReader::Open(packagePath);
    if (!reader) return Core::failure(std::move(reader.error()));
    std::vector<Core::PackageWriteEntry> entries;
    entries.reserve(reader->fileCount() + 1);
    for (Core::usize i = 0; i < reader->fileCount(); ++i)
    {
        const auto info = reader->entry(i);
        if (info->path == path) continue;
        auto view = reader->viewFile(info->path);
        if (!view) return Core::failure(std::move(view.error()));
        entries.push_back({info->path, view->bytes()});
    }
    if (replacement) entries.push_back({path, *replacement});
    return Core::writePackageFile(packagePath, entries);
}

struct CookedPackageAsset final {
    Core::AssetId assetId{};
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Bytes cookedBytes{};
    std::vector<AssetFormat::CookedAssetWriteDependency> dependencies{};
};

struct CookedPackage final {
    std::filesystem::path root{};
    std::vector<CookedPackageAsset> assets{};
};

[[nodiscard]] inline CookedPackage
writeCookedPackage(std::filesystem::path directoryName,
                   std::vector<CookedPackageAsset> assets)
{
    CookedPackage package{
        .root = std::filesystem::temp_directory_path() / std::move(directoryName),
        .assets = std::move(assets),
    };
    std::error_code cleanupError;
    std::filesystem::remove_all(package.root, cleanupError);
    std::sort(package.assets.begin(), package.assets.end(),
              [](const CookedPackageAsset& left, const CookedPackageAsset& right) {
                  return left.assetId < right.assetId;
              });

    std::vector<AssetFormat::CookedManifestWriteEntry> entries;
    entries.reserve(package.assets.size());
    std::vector<CatalogPackageObjectBlob> objects;
    objects.reserve(package.assets.size());
    for (const CookedPackageAsset& asset : package.assets)
    {
        auto view = AssetFormat::parseCookedAssetView(asset.cookedBytes);
        EXPECT_TRUE(view.has_value()) << (view ? "" : view.error().message);
        if (!view)
        {
            return package;
        }
        EXPECT_EQ(view->header().assetId, asset.assetId);
        EXPECT_EQ(view->header().assetKind, asset.assetKind);
        entries.push_back(AssetFormat::CookedManifestWriteEntry{
            .assetId = asset.assetId,
            .contentHash = view->header().contentHash,
            .assetKind = asset.assetKind,
            .assetTypeVersion = view->header().assetTypeVersion,
            .cookedFileBytes = asset.cookedBytes.size(),
            .dependencies = asset.dependencies,
        });

        objects.push_back({asset.assetKind, asset.assetId, asset.cookedBytes});
    }

    auto manifest = AssetFormat::writeCookedManifestBytes(
        AssetFormat::CookedManifestWriteDesc{.entries = entries});
    EXPECT_TRUE(manifest.has_value()) << (manifest ? "" : manifest.error().message);
    if (manifest)
    {
        const auto status = writePackage(package.root, *manifest, objects);
        EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
    }
    return package;
}

inline void removePackage(const CookedPackage& package)
{
    std::error_code errorCode;
    std::filesystem::remove_all(package.root, errorCode);
}

struct TextureMaterialPackage final {
    std::filesystem::path root;
    Core::AssetId textureId;
    Core::AssetId materialId;
    Bytes textureBytes;
    Bytes materialBytes;
};

// Writes one catalog.pck with a manifest and virtual texture/material objects.
[[nodiscard]] inline TextureMaterialPackage writeTextureMaterialPackage(std::filesystem::path directoryName,
                                                                        bool writeMaterialObject = true)
{
    const auto pixels = defaultPayload();
    auto texturePayload = AssetFormat::writeTexture2DPayloadBytesRgba8(1, 1, pixels);
    EXPECT_TRUE(texturePayload.has_value()) << (texturePayload ? "" : texturePayload.error().message);
    auto textureCooked = AssetFormat::writeCookedTexture2DAssetRgba8(assetId(1U), 1, 1, pixels);
    EXPECT_TRUE(textureCooked.has_value()) << (textureCooked ? "" : textureCooked.error().message);
    Core::ContentHash textureHash = defaultPayloadHash();
    if (texturePayload)
    {
        auto textureDigest = Core::digestContentHashV1(*texturePayload);
        EXPECT_TRUE(textureDigest.has_value()) << (textureDigest ? "" : textureDigest.error().message);
        if (textureDigest)
        {
            textureHash = *textureDigest;
        }
    }
    const auto materialDigest = defaultPayloadHash();
    TextureMaterialPackage package{
        .root = std::filesystem::temp_directory_path() / std::move(directoryName),
        .textureId = assetId(1U),
        .materialId = assetId(2U),
        .textureBytes = textureCooked ? std::move(*textureCooked) : Bytes{},
        .materialBytes = makeCookedAsset(2U, AssetFormat::AssetKind::Material),
    };

    std::vector<CatalogPackageObjectBlob> objects{
        {AssetFormat::AssetKind::Texture2D, package.textureId, package.textureBytes}};
    if (writeMaterialObject)
    {
        objects.push_back({AssetFormat::AssetKind::Material, package.materialId, package.materialBytes});
    }
    const auto status = writePackage(package.root,
        makeTextureMaterialManifest(package.textureBytes.size(), textureHash,
                                     package.materialBytes.size(), materialDigest), objects);
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
    return package;
}

inline void removePackage(const TextureMaterialPackage& package)
{
    std::error_code errorCode;
    std::filesystem::remove_all(package.root, errorCode);
}

} // namespace Tina::Asset::TestSupport
