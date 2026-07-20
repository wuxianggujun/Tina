#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <gtest/gtest.h>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

TEST(AssetGpuTextureTests, UploadTypedTextureToNullDevice)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    std::vector<std::byte> pixels(4, std::byte{0xAB});
    auto cooked = AssetFormat::writeCookedTexture2DAsset(
        textureId, AssetFormat::Texture2DPayloadDesc{.width = 1, .height = 1, .pixels = pixels});
    ASSERT_TRUE(cooked.has_value());

    auto file = makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
                                             CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value());

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto gpu = uploadTexture2DFromCooked(**device, *file);
    ASSERT_TRUE(gpu.has_value()) << gpu.error().message;
    ASSERT_TRUE(uploadAndBindTexture2DForSpriteKey(**device, *file, 1U).has_value());
    EXPECT_GE((*device)->statistics().liveResources, 1U);
}

} // namespace
} // namespace Tina::Asset
