#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/Fx2DPayload.hpp>
#include <tina/asset_format/NavigationGrid2DPayload.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xC3U);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] Core::Result<CookedAssetFile> fileFrom(
    const std::vector<std::byte>& bytes,
    std::pmr::memory_resource& resource)
{
    return makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(bytes.begin(), bytes.end(), &resource),
        CookedAssetFileLoadConfig{.memoryResource = &resource});
}

TEST(NavigationFxTypedViewTests, LoadsNavigationGridDataFromIndependentCookedAsset)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array<Core::u8, 4> flags{0, 1, 0, 0};
    const std::array<Core::u8, 4> costs{1, 1, 4, 2};
    auto cooked = AssetFormat::writeCookedNavigationGrid2DAsset(
        assetId(1U),
        {.widthCells = 2,
         .heightCells = 2,
         .originXMeters = 3.0F,
         .originYMeters = -1.0F,
         .cellSizeMeters = 0.5F,
         .cellFlags = flags,
         .traversalCosts = costs});
    ASSERT_TRUE(cooked) << cooked.error().message;
    auto file = fileFrom(*cooked, memory);
    ASSERT_TRUE(file) << file.error().message;
    auto grid = loadNavigationGrid2DDataFromCooked(*file, memory);
    ASSERT_TRUE(grid) << grid.error().message;
    EXPECT_EQ(grid->widthCells(), 2U);
    EXPECT_EQ(grid->heightCells(), 2U);
    EXPECT_TRUE(grid->blockedAt({1, 0}));
    EXPECT_EQ(grid->traversalCostAt({0, 1}), 4U);
}

TEST(NavigationFxTypedViewTests, FxDependencyMustMatchPayloadSprite)
{
    std::pmr::unsynchronized_pool_resource memory;
    AssetFormat::Fx2DPayloadDesc desc{};
    desc.spriteAssetId = assetId(2U);
    desc.particle.capacity = 4;
    desc.particle.count = 2;
    desc.trail.segmentCapacity = 3;
    auto validCooked = AssetFormat::writeCookedFx2DAsset(
        assetId(3U), desc, AssetFormat::TargetPlatform::WindowsX64);
    ASSERT_TRUE(validCooked) << validCooked.error().message;
    auto validFile = fileFrom(*validCooked, memory);
    ASSERT_TRUE(validFile) << validFile.error().message;
    auto parsed = parseFx2DFromCooked(*validFile);
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_EQ(parsed->spriteAssetId, desc.spriteAssetId);

    auto payload = AssetFormat::writeFx2DPayloadBytes(desc);
    ASSERT_TRUE(payload);
    const std::array dependencies{AssetFormat::CookedAssetWriteDependency{
        .assetId = assetId(4U),
        .expectedKind = AssetFormat::AssetKind::Sprite,
        .flags = AssetFormat::DependencyFlags::Required,
    }};
    auto mismatched = AssetFormat::writeCookedAssetBytes({
        .assetKind = AssetFormat::AssetKind::Fx2D,
        .assetTypeVersion = AssetFormat::Fx2DWire::SchemaVersion,
        .assetId = assetId(3U),
        .dependencies = dependencies,
        .payload = *payload,
    });
    ASSERT_TRUE(mismatched) << mismatched.error().message;
    auto mismatchedFile = fileFrom(*mismatched, memory);
    ASSERT_TRUE(mismatchedFile) << mismatchedFile.error().message;
    auto rejected = parseFx2DFromCooked(*mismatchedFile);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, AssetErrorCode::CatalogEntryMismatch);
}

} // namespace
} // namespace Tina::Asset
