#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackageSummary.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/AssetFormat.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory_resource>
#include <string_view>
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
Core::ContentHash::Bytes hashBytes(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[7] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

Bytes makeTwoEntryManifest()
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
    putFixed(bytes, entryTable + 16U, hashBytes(0x21U));
    putU16(bytes, entryTable + 32U, static_cast<Core::u16>(AssetFormat::AssetKind::Texture2D));
    putU16(bytes, entryTable + 34U, 1U);
    putU32(bytes, entryTable + 40U, 0U);
    putU32(bytes, entryTable + 44U, 0U);
    putU64(bytes, entryTable + 48U, 128U);

    const auto materialOffset = entryTable + AssetFormat::Wire::ManifestEntryBytes;
    putFixed(bytes, materialOffset, idBytes(2U));
    putFixed(bytes, materialOffset + 16U, hashBytes(0x22U));
    putU16(bytes, materialOffset + 32U, static_cast<Core::u16>(AssetFormat::AssetKind::Material));
    putU16(bytes, materialOffset + 34U, 1U);
    putU32(bytes, materialOffset + 40U, 0U);
    putU32(bytes, materialOffset + 44U, 1U);
    putU64(bytes, materialOffset + 48U, 256U);

    putFixed(bytes, dependencyOffset, idBytes(1U));
    putU16(bytes, dependencyOffset + 16U, static_cast<Core::u16>(AssetFormat::AssetKind::Texture2D));
    putU16(bytes, dependencyOffset + 18U, static_cast<Core::u16>(AssetFormat::DependencyFlags::Required));
    return bytes;
}

TEST(CatalogPackageSummaryTests, BuildsTotalsAndEntryRows)
{
    TrackingMemoryResource resource;
    const auto bytes = makeTwoEntryManifest();
    auto view = AssetFormat::parseCookedManifestView(bytes);
    ASSERT_TRUE(view.has_value());
    auto catalog = CatalogSnapshot::Create(*view, CatalogConfig{.maxEntries = 8,
                                                                .maxDependencies = 8,
                                                                .maxDependenciesPerAsset = 4,
                                                                .memoryResource = &resource});
    ASSERT_TRUE(catalog.has_value());

    auto summary = buildCatalogPackageSummary(*catalog, CatalogPackageSummaryConfig{.memoryResource = &resource});
    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->entryCount, 2U);
    EXPECT_EQ(summary->dependencyCount, 1U);
    ASSERT_EQ(summary->entries.size(), 2U);
    EXPECT_EQ(summary->entries[0].assetKind, AssetFormat::AssetKind::Texture2D);
    EXPECT_EQ(summary->entries[1].assetKind, AssetFormat::AssetKind::Material);
    EXPECT_EQ(summary->entries[1].dependencyCount, 1U);
    EXPECT_EQ(summary->entries[1].cookedFileBytes, 256U);

    const auto expectedId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto text = expectedId.canonicalText();
    EXPECT_EQ(std::string_view(summary->entries[0].assetIdText.data(), text.size()),
              std::string_view(text.data(), text.size()));
}

TEST(CatalogPackageSummaryTests, CanOmitEntryRows)
{
    TrackingMemoryResource resource;
    const auto bytes = makeTwoEntryManifest();
    auto view = AssetFormat::parseCookedManifestView(bytes);
    ASSERT_TRUE(view.has_value());
    auto catalog = CatalogSnapshot::Create(*view, CatalogConfig{.maxEntries = 8,
                                                                .maxDependencies = 8,
                                                                .maxDependenciesPerAsset = 4,
                                                                .memoryResource = &resource});
    ASSERT_TRUE(catalog.has_value());

    auto summary = buildCatalogPackageSummary(
        *catalog, CatalogPackageSummaryConfig{.memoryResource = &resource, .includeEntries = false});
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->entryCount, 2U);
    EXPECT_TRUE(summary->entries.empty());
}

} // namespace
} // namespace Tina::Asset
