#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <vector>

namespace Tina::Tests {

TEST(NullRenderDeviceTextureTest, CreateBindDestroyLifecycle)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<std::byte, 4> pixel{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto texture = (*device)->createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = 1,
        .height = 1,
        .rgba8Pixels = pixel,
    });
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    ASSERT_TRUE((*device)->setSprite2DTextureBinding(1U, *texture).has_value());
    ASSERT_TRUE((*device)->setSprite2DTextureBinding(1U, {}).has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*texture).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);

    auto stale = (*device)->destroyTexture2D(*texture);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::TextureNotFound);
}

TEST(NullRenderDeviceTextureTest, MaterialBaseColorAndMetallicRoughnessBindings)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<std::byte, 4> pixel{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255}};
    auto baseColor = (*device)->createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = 1,
        .height = 1,
        .rgba8Pixels = pixel,
    });
    ASSERT_TRUE(baseColor.has_value()) << baseColor.error().message;
    auto metallicRoughness = (*device)->createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = 1,
        .height = 1,
        .rgba8Pixels = pixel,
    });
    ASSERT_TRUE(metallicRoughness.has_value()) << metallicRoughness.error().message;

    ASSERT_TRUE((*device)->setMesh3DMaterialTextureBinding(7U, *baseColor).has_value());
    ASSERT_TRUE(
        (*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(7U, *metallicRoughness).has_value());
    ASSERT_FALSE((*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(0U, *metallicRoughness).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(7U, {}).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialTextureBinding(7U, {}).has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*baseColor).has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*metallicRoughness).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
}

TEST(NullRenderDeviceTextureTest, RejectsBadUploadSize)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<std::byte, 2> bad{std::byte{1}, std::byte{2}};
    auto texture = (*device)->createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = 1,
        .height = 1,
        .rgba8Pixels = bad,
    });
    ASSERT_FALSE(texture.has_value());
    EXPECT_EQ(texture.error().code, Render::RenderErrorCode::InvalidTextureUpload);
}

} // namespace Tina::Tests
