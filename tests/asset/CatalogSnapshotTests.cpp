#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogFile.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

using Bytes = std::vector<std::byte>;

struct DependencySpec final {
    Core::u8 idSeed = 0;
    AssetFormat::AssetKind expectedKind = AssetFormat::AssetKind::Invalid;
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
    [[nodiscard]] std::size_t outstandingBytes() const noexcept
    {
        return m_outstandingBytes;
    }
    [[nodiscard]] std::size_t allocationCount() const noexcept
    {
        return m_allocationCount;
    }
    [[nodiscard]] std::size_t deallocationCount() const noexcept
    {
        return m_deallocationCount;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_allocationCount;
        ++m_outstandingAllocations;
        m_outstandingBytes += bytes;
        return pointer;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        ++m_deallocationCount;
        --m_outstandingAllocations;
        m_outstandingBytes -= bytes;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::size_t m_allocationCount = 0;
    std::size_t m_deallocationCount = 0;
    std::size_t m_outstandingAllocations = 0;
    std::size_t m_outstandingBytes = 0;
};

class FailAfterSuccessfulAllocationsResource final : public std::pmr::memory_resource {
  public:
    explicit FailAfterSuccessfulAllocationsResource(std::size_t allowedAllocations) noexcept
        : m_allowedAllocations(allowedAllocations)
    {
    }

    [[nodiscard]] std::size_t outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (m_successfulAllocations >= m_allowedAllocations)
        {
            throw std::bad_alloc{};
        }
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_successfulAllocations;
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

    std::size_t m_allowedAllocations = 0;
    std::size_t m_successfulAllocations = 0;
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
                   static_cast<Core::u16>(AssetFormat::DependencyFlags::Required));
        }
        dependencyFirst += static_cast<Core::u32>(entry.dependencies.size());
    }
    return bytes;
}

[[nodiscard]] CatalogConfig defaultConfig(std::pmr::memory_resource& resource, Core::u32 maxEntries = 64,
                                          Core::u32 maxDependencies = 256, Core::u32 maxDependenciesPerAsset = 16)
{
    return CatalogConfig{
        .maxEntries = maxEntries,
        .maxDependencies = maxDependencies,
        .maxDependenciesPerAsset = maxDependenciesPerAsset,
        .memoryResource = &resource,
    };
}

[[nodiscard]] AssetFormat::CookedManifestView parseManifest(const Bytes& bytes)
{
    auto result = AssetFormat::parseCookedManifestView(bytes);
    EXPECT_TRUE(result.has_value()) << result.error().message;
    return std::move(*result);
}

[[nodiscard]] CatalogSnapshot createCatalog(const Bytes& bytes, CatalogConfig config)
{
    const auto view = parseManifest(bytes);
    auto result = CatalogSnapshot::Create(view, config);
    EXPECT_TRUE(result.has_value()) << result.error().message;
    return std::move(*result);
}

TEST(CatalogSnapshotTests, EmptyCatalog)
{
    TrackingMemoryResource resource;
    const auto bytes = makeManifest({});
    auto snapshot = createCatalog(bytes, defaultConfig(resource, 0, 0, 0));
    EXPECT_TRUE(snapshot);
    EXPECT_EQ(snapshot.entryCount(), 0U);
    EXPECT_EQ(snapshot.dependencyCount(), 0U);
    EXPECT_FALSE(snapshot.find(*Core::AssetId::fromBytes(idBytes(1U))));
    EXPECT_FALSE(snapshot.entry(0U));
}

TEST(CatalogSnapshotTests, SingleEntry)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 7U, .kind = AssetFormat::AssetKind::Sprite},
    };
    auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource));
    ASSERT_EQ(snapshot.entryCount(), 1U);
    const auto entry = snapshot.entry(0U);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->assetId, *Core::AssetId::fromBytes(idBytes(7U)));
    EXPECT_EQ(entry->assetKind, AssetFormat::AssetKind::Sprite);
    EXPECT_EQ(entry->dependencyCount, 0U);
    ASSERT_TRUE(snapshot.find(entry->assetId));
    EXPECT_EQ(*snapshot.find(entry->assetId), 0U);
}

TEST(CatalogSnapshotTests, MultiEntryQueryAndBinarySearch)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U, .kind = AssetFormat::AssetKind::Material},
        ManifestEntrySpec{.idSeed = 5U, .kind = AssetFormat::AssetKind::Prefab},
        ManifestEntrySpec{.idSeed = 9U, .kind = AssetFormat::AssetKind::AudioClip},
    };
    auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource));
    ASSERT_EQ(snapshot.entryCount(), 4U);
    ASSERT_TRUE(snapshot.find(*Core::AssetId::fromBytes(idBytes(1U))));
    EXPECT_EQ(*snapshot.find(*Core::AssetId::fromBytes(idBytes(1U))), 0U);
    ASSERT_TRUE(snapshot.find(*Core::AssetId::fromBytes(idBytes(5U))));
    EXPECT_EQ(*snapshot.find(*Core::AssetId::fromBytes(idBytes(5U))), 2U);
    ASSERT_TRUE(snapshot.find(*Core::AssetId::fromBytes(idBytes(9U))));
    EXPECT_EQ(*snapshot.find(*Core::AssetId::fromBytes(idBytes(9U))), 3U);
    EXPECT_FALSE(snapshot.find(*Core::AssetId::fromBytes(idBytes(3U))));
    EXPECT_FALSE(snapshot.find(*Core::AssetId::fromBytes(idBytes(255U))));
}

TEST(CatalogSnapshotTests, SurvivesManifestBytesDestruction)
{
    TrackingMemoryResource resource;
    CatalogSnapshot snapshot;
    {
        auto bytes = makeManifest({
            ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
            ManifestEntrySpec{.idSeed = 2U,
                              .kind = AssetFormat::AssetKind::Material,
                              .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
        });
        snapshot = createCatalog(bytes, defaultConfig(resource));
        std::fill(bytes.begin(), bytes.end(), std::byte{0xFF});
        bytes.clear();
        bytes.shrink_to_fit();
    }

    ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot.entryCount(), 2U);
    const auto entry = snapshot.entry(1U);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->assetKind, AssetFormat::AssetKind::Material);
    const auto dependency = snapshot.dependency(1U, 0U);
    ASSERT_TRUE(dependency);
    EXPECT_EQ(dependency->targetEntryIndex, 0U);
    EXPECT_EQ(dependency->assetId, *Core::AssetId::fromBytes(idBytes(1U)));
}

TEST(CatalogSnapshotTests, ResolvesDependencyTargetIndex)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
        ManifestEntrySpec{.idSeed = 3U,
                          .kind = AssetFormat::AssetKind::Prefab,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D},
                                           {.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Material}}},
    };
    auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource));

    ASSERT_EQ(snapshot.dependencyCount(), 3U);
    const auto prefabTexture = snapshot.dependency(2U, 0U);
    const auto prefabMaterial = snapshot.dependency(2U, 1U);
    ASSERT_TRUE(prefabTexture);
    ASSERT_TRUE(prefabMaterial);
    EXPECT_EQ(prefabTexture->targetEntryIndex, 0U);
    EXPECT_EQ(prefabMaterial->targetEntryIndex, 1U);
    EXPECT_FALSE(snapshot.dependency(2U, 2U));
}

TEST(CatalogSnapshotTests, AcceptsLegalChainDag)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
        ManifestEntrySpec{.idSeed = 3U,
                          .kind = AssetFormat::AssetKind::Prefab,
                          .dependencies = {{.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Material}}},
    };
    auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource));
    EXPECT_TRUE(snapshot);
    EXPECT_EQ(snapshot.entryCount(), 3U);
}

TEST(CatalogSnapshotTests, AcceptsLegalDiamondDag)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
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
    };
    auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource));
    EXPECT_TRUE(snapshot);
    EXPECT_EQ(snapshot.dependencyCount(), 4U);
}

TEST(CatalogSnapshotTests, RejectsTwoNodeCycle)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Prefab}}},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Prefab,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Material}}},
    };
    const auto bytes = makeManifest(entries);
    const auto view = parseManifest(bytes);
    const auto result = CatalogSnapshot::Create(view, defaultConfig(resource));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::DependencyCycle);
    EXPECT_NE(result.error().message.find("cycle"), std::string::npos);
}

TEST(CatalogSnapshotTests, RejectsMultiNodeCycle)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U,
                          .kind = AssetFormat::AssetKind::Texture2D,
                          .dependencies = {{.idSeed = 3U, .expectedKind = AssetFormat::AssetKind::Prefab}}},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
        ManifestEntrySpec{.idSeed = 3U,
                          .kind = AssetFormat::AssetKind::Prefab,
                          .dependencies = {{.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Material}}},
    };
    const auto bytes = makeManifest(entries);
    const auto view = parseManifest(bytes);
    const auto result = CatalogSnapshot::Create(view, defaultConfig(resource));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::DependencyCycle);
}

TEST(CatalogSnapshotTests, DeepChainDoesNotRecurse)
{
    TrackingMemoryResource resource;
    std::vector<ManifestEntrySpec> entries;
    entries.reserve(128U);
    entries.push_back(ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D});
    for (Core::u8 seed = 2U; seed <= 128U; ++seed)
    {
        entries.push_back(ManifestEntrySpec{
            .idSeed = seed,
            .kind = AssetFormat::AssetKind::Material,
            .dependencies = {{.idSeed = static_cast<Core::u8>(seed - 1U),
                              .expectedKind = seed == 2U ? AssetFormat::AssetKind::Texture2D
                                                         : AssetFormat::AssetKind::Material}},
        });
    }
    auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource, 256, 256, 4));
    EXPECT_EQ(snapshot.entryCount(), 128U);
    EXPECT_EQ(snapshot.dependencyCount(), 127U);
}

TEST(CatalogSnapshotTests, CapacityExceededDoesNotPublish)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U, .kind = AssetFormat::AssetKind::Material},
    };
    const auto bytes = makeManifest(entries);
    const auto view = parseManifest(bytes);
    const auto result = CatalogSnapshot::Create(view, defaultConfig(resource, 1, 8, 4));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::CatalogCapacityExceeded);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogSnapshotTests, InvalidConfigRejected)
{
    TrackingMemoryResource resource;
    const auto bytes = makeManifest({});
    const auto view = parseManifest(bytes);

    auto nullResource = CatalogSnapshot::Create(view, CatalogConfig{.maxEntries = 1, .memoryResource = nullptr});
    ASSERT_FALSE(nullResource.has_value());
    EXPECT_EQ(nullResource.error().code, AssetErrorCode::InvalidCatalogConfig);

    auto overWireLimit = CatalogSnapshot::Create(
        view, CatalogConfig{.maxEntries = AssetFormat::Wire::MaxManifestEntries + 1U,
                            .maxDependencies = 1,
                            .maxDependenciesPerAsset = 1,
                            .memoryResource = &resource});
    ASSERT_FALSE(overWireLimit.has_value());
    EXPECT_EQ(overWireLimit.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(CatalogSnapshotTests, AllocationFailureRollsBack)
{
    FailAfterSuccessfulAllocationsResource resource(1);
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
    };
    const auto bytes = makeManifest(entries);
    const auto view = parseManifest(bytes);
    const auto result = CatalogSnapshot::Create(view, defaultConfig(resource));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetErrorCode::AllocationFailed);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogSnapshotTests, MoveConstructionAndAssignment)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 4U, .kind = AssetFormat::AssetKind::Font},
    };
    auto first = createCatalog(makeManifest(entries), defaultConfig(resource));
    auto second = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_TRUE(second);
    EXPECT_EQ(second.entryCount(), 1U);

    CatalogSnapshot third;
    third = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_TRUE(third);
    ASSERT_TRUE(third.find(*Core::AssetId::fromBytes(idBytes(4U))));
    EXPECT_EQ(*third.find(*Core::AssetId::fromBytes(idBytes(4U))), 0U);
}

TEST(CatalogSnapshotTests, DestructorReleasesAllPmrResources)
{
    TrackingMemoryResource resource;
    {
        const std::vector<ManifestEntrySpec> entries{
            ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
            ManifestEntrySpec{.idSeed = 2U,
                              .kind = AssetFormat::AssetKind::Material,
                              .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
        };
        auto snapshot = createCatalog(makeManifest(entries), defaultConfig(resource));
        EXPECT_GT(resource.outstandingAllocations(), 0U);
        EXPECT_GT(resource.outstandingBytes(), 0U);
    }
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
    EXPECT_EQ(resource.outstandingBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

TEST(CatalogSnapshotTests, RepeatedCreateDestroyHasNoMemoryDrift)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
        ManifestEntrySpec{.idSeed = 3U,
                          .kind = AssetFormat::AssetKind::Prefab,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D},
                                           {.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Material}}},
    };
    const auto bytes = makeManifest(entries);

    for (int iteration = 0; iteration < 300; ++iteration)
    {
        auto snapshot = createCatalog(bytes, defaultConfig(resource));
        ASSERT_TRUE(snapshot);
        ASSERT_EQ(snapshot.entryCount(), 3U);
    }

    EXPECT_EQ(resource.outstandingAllocations(), 0U);
    EXPECT_EQ(resource.outstandingBytes(), 0U);
    EXPECT_EQ(resource.allocationCount(), resource.deallocationCount());
}

TEST(CatalogSnapshotTests, FailedCreateLeavesNoOutstandingAllocations)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 2U, .expectedKind = AssetFormat::AssetKind::Prefab}}},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Prefab,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Material}}},
    };
    const auto bytes = makeManifest(entries);
    const auto view = parseManifest(bytes);
    const auto result = CatalogSnapshot::Create(view, defaultConfig(resource));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
    EXPECT_EQ(resource.outstandingBytes(), 0U);
}

TEST(CatalogFileLoadTests, LoadsManifestFileIntoOwningSnapshot)
{
    TrackingMemoryResource resource;
    const std::vector<ManifestEntrySpec> entries{
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetFormat::AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetFormat::AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetFormat::AssetKind::Texture2D}}},
    };
    const auto bytes = makeManifest(entries);

    const auto directory = std::filesystem::temp_directory_path() / "tina_catalog_file_tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / "manifest.tmnft";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(static_cast<const char*>(static_cast<const void*>(bytes.data())),
                     static_cast<std::streamsize>(bytes.size()));
    }
    const auto u8Path = path.u8string();
    const std::string utf8Path(u8Path.begin(), u8Path.end());

    CatalogFileLoadConfig config{
        .catalog = defaultConfig(resource),
        .maxFileBytes = AssetFormat::Wire::MaxManifestFileBytes,
    };
    auto snapshot = loadCatalogSnapshotFromManifestFile(utf8Path, config);
    ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
    EXPECT_EQ(snapshot->entryCount(), 2U);
    EXPECT_EQ(snapshot->dependencyCount(), 1U);
    ASSERT_TRUE(snapshot->find(*Core::AssetId::fromBytes(idBytes(2U))));

    snapshot = CatalogSnapshot{};
    std::error_code errorCode;
    std::filesystem::remove(path, errorCode);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(CatalogFileLoadTests, MissingManifestFileDoesNotPublishSnapshot)
{
    TrackingMemoryResource resource;
    CatalogFileLoadConfig config{.catalog = defaultConfig(resource)};
    const auto result =
        loadCatalogSnapshotFromManifestFile("C:/tina_missing_manifest_file_does_not_exist.tmnft", config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Core::CoreErrorCode::NotFound);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

} // namespace
} // namespace Tina::Asset
