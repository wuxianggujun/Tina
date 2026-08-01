#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogLoadOrder.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

using Bytes = std::vector<std::byte>;

struct DependencySpec final {
    Core::u8 idSeed = 0;
    AssetFormat::AssetKind expectedKind = AssetFormat::AssetKind::Invalid;
    AssetFormat::DependencyFlags flags = AssetFormat::DependencyFlags::Required;
};

struct ManifestEntrySpec final {
    Core::u8 idSeed = 0;
    AssetFormat::AssetKind kind = AssetFormat::AssetKind::Invalid;
    std::vector<DependencySpec> dependencies{};
};

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
Core::ContentHash::Bytes hashBytes(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[7] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

Bytes makeManifest(const std::vector<ManifestEntrySpec>& entries)
{
    Core::u32 dependencyCount = 0;
    for (const auto& entry : entries)
    {
        dependencyCount += static_cast<Core::u32>(entry.dependencies.size());
    }
    const auto dependencyOffset =
        AssetFormat::Wire::CookedManifestHeaderBytes + entries.size() * AssetFormat::Wire::ManifestEntryBytes;
    const auto fileBytes =
        dependencyOffset + static_cast<Core::u64>(dependencyCount) * AssetFormat::Wire::DependencyEntryBytes;
    Bytes bytes(static_cast<Core::usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, AssetFormat::Wire::CookedManifestMagic);
    putU16(bytes, 8U, AssetFormat::Wire::SchemaMajor);
    putU16(bytes, 10U, AssetFormat::Wire::SchemaMinor);
    putU32(bytes, 12U, AssetFormat::Wire::CookedManifestHeaderBytes);
    putU16(bytes, 16U, static_cast<Core::u16>(AssetFormat::TargetPlatform::WindowsX64));
    putU8(bytes, 18U, static_cast<Core::u8>(AssetFormat::EndianTag::Little));
    putU8(bytes, 19U, static_cast<Core::u8>(AssetFormat::HashAlgorithm::Xxh3_128V1));
    putU32(bytes, 24U, static_cast<Core::u32>(entries.size()));
    putU32(bytes, 28U, AssetFormat::Wire::ManifestEntryBytes);
    putU32(bytes, 32U, dependencyCount);
    putU32(bytes, 36U, AssetFormat::Wire::DependencyEntryBytes);
    putU64(bytes, 40U, AssetFormat::Wire::CookedManifestHeaderBytes);
    putU64(bytes, 48U, dependencyOffset);
    putU64(bytes, 56U, fileBytes);

    Core::u32 dependencyFirst = 0;
    for (Core::usize entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
    {
        const auto& entry = entries[entryIndex];
        const auto offset =
            AssetFormat::Wire::CookedManifestHeaderBytes + entryIndex * AssetFormat::Wire::ManifestEntryBytes;
        putFixed(bytes, offset, idBytes(entry.idSeed));
        putFixed(bytes, offset + 16U, hashBytes(static_cast<Core::u8>(entry.idSeed + 0x20U)));
        putU16(bytes, offset + 32U, static_cast<Core::u16>(entry.kind));
        putU16(bytes, offset + 34U, 1U);
        putU32(bytes, offset + 40U, dependencyFirst);
        putU32(bytes, offset + 44U, static_cast<Core::u32>(entry.dependencies.size()));
        putU64(bytes, offset + 48U, 256U + entry.idSeed);
        for (Core::usize localIndex = 0; localIndex < entry.dependencies.size(); ++localIndex)
        {
            const auto dependencyIndex = dependencyFirst + static_cast<Core::u32>(localIndex);
            const auto dependencyEntryOffset =
                dependencyOffset + dependencyIndex * AssetFormat::Wire::DependencyEntryBytes;
            putFixed(bytes, dependencyEntryOffset, idBytes(entry.dependencies[localIndex].idSeed));
            putU16(bytes, dependencyEntryOffset + 16U,
                   static_cast<Core::u16>(entry.dependencies[localIndex].expectedKind));
            putU16(bytes, dependencyEntryOffset + 18U,
                   static_cast<Core::u16>(entry.dependencies[localIndex].flags));
        }
        dependencyFirst += static_cast<Core::u32>(entry.dependencies.size());
    }
    return bytes;
}

[[nodiscard]] CatalogSnapshot makeCatalog(std::pmr::memory_resource& resource,
                                          const std::vector<ManifestEntrySpec>& entries)
{
    const auto bytes = makeManifest(entries);
    auto view = AssetFormat::parseCookedManifestView(bytes);
    EXPECT_TRUE(view.has_value());
    auto snapshot = CatalogSnapshot::Create(*view, CatalogConfig{.maxEntries = 64,
                                                                 .maxDependencies = 256,
                                                                 .maxDependenciesPerAsset = 16,
                                                                 .memoryResource = &resource});
    EXPECT_TRUE(snapshot.has_value()) << snapshot.error().message;
    return std::move(*snapshot);
}

TEST(CatalogLoadOrderTests, OrdersDependenciesBeforeDependents)
{
    TrackingMemoryResource resource;
    // Texture(1) <- Material(2) <- Prefab(3)
    auto catalog = makeCatalog(resource, {
                                             ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
                                             ManifestEntrySpec{.idSeed = 2U,
                                                               .kind = AssetFormat::AssetKind::Material,
                                                               .dependencies = {{.idSeed = 1U,
                                                                                 .expectedKind =
                                                                                     AssetFormat::AssetKind::Texture2D}}},
                                             ManifestEntrySpec{.idSeed = 3U,
                                                               .kind = AssetFormat::AssetKind::Prefab,
                                                               .dependencies = {{.idSeed = 2U,
                                                                                 .expectedKind =
                                                                                     AssetFormat::AssetKind::Material}}},
                                         });

    const auto prefabId = *Core::AssetId::fromBytes(idBytes(3U));
    const std::array requested{prefabId};
    auto order = computeCatalogLoadOrder(catalog, requested, CatalogLoadOrderConfig{.memoryResource = &resource});
    ASSERT_TRUE(order.has_value()) << order.error().message;
    ASSERT_EQ(order->size(), 3U);
    // entry indices: 0=texture, 1=material, 2=prefab
    EXPECT_EQ((*order)[0], 0U);
    EXPECT_EQ((*order)[1], 1U);
    EXPECT_EQ((*order)[2], 2U);
}

TEST(CatalogLoadOrderTests, DiamondSharesCommonDependencyOnce)
{
    TrackingMemoryResource resource;
    // Texture(1) shared by Shader(2) and Material(3); Prefab(4) depends on both.
    auto catalog = makeCatalog(
        resource, {
                      ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
                      ManifestEntrySpec{.idSeed = 2U,
                                        .kind = AssetFormat::AssetKind::Shader,
                                        .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
                      ManifestEntrySpec{.idSeed = 3U,
                                        .kind = AssetFormat::AssetKind::Material,
                                        .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
                      ManifestEntrySpec{.idSeed = 4U,
                                        .kind = AssetFormat::AssetKind::Prefab,
                                        .dependencies = {{.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Shader},
                                                         {.idSeed = 3U, .expectedKind = AssetFormat::AssetKind::Material}}},
                  });

    const auto prefabId = *Core::AssetId::fromBytes(idBytes(4U));
    auto order =
        computeCatalogLoadOrder(catalog, std::array{prefabId}, CatalogLoadOrderConfig{.memoryResource = &resource});
    ASSERT_TRUE(order.has_value()) << order.error().message;
    ASSERT_EQ(order->size(), 4U);
    // Texture once, then shader/material (order among peers follows edge visit), then prefab.
    EXPECT_EQ((*order)[0], 0U);
    EXPECT_EQ((*order)[3], 3U);
    // Indices 1 and 2 appear exactly once between texture and prefab.
    std::vector<Core::u32> middle{(*order)[1], (*order)[2]};
    std::sort(middle.begin(), middle.end());
    EXPECT_EQ(middle[0], 1U);
    EXPECT_EQ(middle[1], 2U);
}

TEST(CatalogLoadOrderTests, DeferredDependencyLoadsOnlyWhenExplicitlyRequested)
{
    TrackingMemoryResource resource;
    // TileMap(3) references Tileset(2) lazily; Tileset still requires Texture(1).
    auto catalog = makeCatalog(
        resource,
        {
            ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
            ManifestEntrySpec{.idSeed = 2U,
                              .kind = AssetFormat::AssetKind::Tileset,
                              .dependencies = {{.idSeed = 1U,
                                                .expectedKind = AssetFormat::AssetKind::Texture2D}}},
            ManifestEntrySpec{
                .idSeed = 3U,
                .kind = AssetFormat::AssetKind::TileMap,
                .dependencies = {{.idSeed = 2U,
                                  .expectedKind = AssetFormat::AssetKind::Tileset,
                                  .flags = AssetFormat::DependencyFlags::Required |
                                           AssetFormat::DependencyFlags::Deferred}},
            },
        });

    const auto tileMapId = *Core::AssetId::fromBytes(idBytes(3U));
    auto rootOrder = computeCatalogLoadOrder(
        catalog, std::array{tileMapId}, CatalogLoadOrderConfig{.memoryResource = &resource});
    ASSERT_TRUE(rootOrder.has_value()) << rootOrder.error().message;
    ASSERT_EQ(rootOrder->size(), 1U);
    EXPECT_EQ((*rootOrder)[0], 2U);

    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(2U));
    auto deferredOrder = computeCatalogLoadOrder(
        catalog, std::array{tilesetId}, CatalogLoadOrderConfig{.memoryResource = &resource});
    ASSERT_TRUE(deferredOrder.has_value()) << deferredOrder.error().message;
    ASSERT_EQ(deferredOrder->size(), 2U);
    EXPECT_EQ((*deferredOrder)[0], 0U);
    EXPECT_EQ((*deferredOrder)[1], 1U);
}

TEST(CatalogLoadOrderTests, RejectsMissingRequestedAsset)
{
    TrackingMemoryResource resource;
    auto catalog = makeCatalog(resource, {ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D}});
    const auto missing = *Core::AssetId::fromBytes(idBytes(9U));
    const auto order =
        computeCatalogLoadOrder(catalog, std::array{missing}, CatalogLoadOrderConfig{.memoryResource = &resource});
    ASSERT_FALSE(order.has_value());
    EXPECT_EQ(order.error().code, Core::CoreErrorCode::NotFound);
}

TEST(CatalogLoadOrderTests, EmptyRequestYieldsEmptyOrder)
{
    TrackingMemoryResource resource;
    auto catalog = makeCatalog(resource, {ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D}});
    auto order = computeCatalogLoadOrder(catalog, {}, CatalogLoadOrderConfig{.memoryResource = &resource});
    ASSERT_TRUE(order.has_value());
    EXPECT_TRUE(order->empty());
}

} // namespace
} // namespace Tina::Asset
