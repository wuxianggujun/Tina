#include <tina/asset/AssetGpuEnvironmentMap.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/EnvironmentMapPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <gtest/gtest.h>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId environmentMapId() noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = std::byte{0x13};
    bytes[15] = std::byte{0xA7};
    return *Core::AssetId::fromBytes(bytes);
}

TEST(AssetGpuEnvironmentMapTests, ParsesAndUploadsCookedAggregateToNullDevice)
{
    std::vector<std::byte> diffuse(48U, std::byte{0x11});
    std::vector<std::byte> specular(240U, std::byte{0x22});
    std::vector<std::byte> brdf(8U, std::byte{0x33});
    auto cooked = AssetFormat::writeCookedEnvironmentMapAsset(
        environmentMapId(),
        AssetFormat::EnvironmentMapPayloadDesc{
            .diffuseFaceSize = 1,
            .specularFaceSize = 2,
            .specularMipCount = 2,
            .brdfWidth = 2,
            .brdfHeight = 1,
            .diffusePixels = diffuse,
            .specularPixels = specular,
            .brdfPixels = brdf,
        });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    auto view = parseEnvironmentMapFromCooked(*file);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->diffuseFaceSize, 1U);
    EXPECT_EQ(view->specularFaceSize, 2U);
    EXPECT_EQ(view->specularMipCount, 2U);
    EXPECT_EQ(view->diffusePixels.size(), diffuse.size());
    EXPECT_EQ(view->specularPixels.size(), specular.size());
    EXPECT_EQ(view->brdfPixels.size(), brdf.size());

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto environment = uploadEnvironmentMapFromCooked(**device, *file);
    ASSERT_TRUE(environment.has_value()) << environment.error().message;
    EXPECT_TRUE((*device)->validateEnvironmentMap(*environment).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 1U);
    EXPECT_TRUE((*device)->destroyEnvironmentMap(*environment).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
}

} // namespace
} // namespace Tina::Asset
