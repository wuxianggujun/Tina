#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset/FontBindingRegistry.hpp>
#include <tina/asset_format/BitmapFontPayload.hpp>

#include "support/BitmapFontTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>

namespace Tina::Asset {
namespace {

using namespace AssetFormat;
using namespace Tests::BitmapFontFixture;

CookedAssetFile loadFile(std::span<const std::byte> bytes, std::pmr::memory_resource& memory)
{
    return makeCookedAssetFileFromBytes(
               std::pmr::vector<std::byte>{bytes.begin(), bytes.end(), &memory},
               {.memoryResource = &memory})
        .value();
}

TEST(FontBindingRegistryTests, InternSharesOneFontObject)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto assets = AssetSystem::Create(AssetSystemConfig{.memoryResource = &memory});
    ASSERT_TRUE(assets);
    const std::array ids{assetId(1), assetId(2)};
    auto handle = assets->publishCooked(loadFile(writeCookedBitmapFontAsset(assetId(3), font(), ids).value(), memory));
    ASSERT_TRUE(handle);

    auto registry = FontBindingRegistry::Create(*assets, {.memoryResource = &memory});
    ASSERT_TRUE(registry);
    auto first = registry->intern(*handle);
    ASSERT_TRUE(first) << first.error().message;
    auto second = registry->intern(*handle);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->get(), second->get());
    EXPECT_EQ(registry->internedCount(), 1U);
    EXPECT_EQ(registry->pages(*handle).size(), 2U);
    EXPECT_TRUE(registry->font(*handle)->contains('A'));
}

TEST(FontBindingRegistryTests, EmptyHandleFailsClosed)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto assets = AssetSystem::Create(AssetSystemConfig{.memoryResource = &memory});
    ASSERT_TRUE(assets);
    auto registry = FontBindingRegistry::Create(*assets);
    ASSERT_TRUE(registry);
    EXPECT_FALSE(registry->intern(AssetHandle{}));
}

} // namespace
} // namespace Tina::Asset
