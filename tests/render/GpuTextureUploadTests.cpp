#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <vector>

namespace Tina::Tests {
namespace {

void countPinRelease(void* userData) noexcept
{
    ++*static_cast<Core::u32*>(userData);
}

} // namespace

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
    ASSERT_TRUE((*device)->setMesh3DMaterialFactors(7U, 0.25F, 0.75F).has_value());
    ASSERT_FALSE((*device)->setMesh3DMaterialFactors(0U, 0.0F, 1.0F).has_value());
    ASSERT_FALSE((*device)->setMesh3DMaterialFactors(7U, 1.5F, 0.5F).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialNormalTextureBinding(7U, *baseColor).has_value());
    ASSERT_FALSE((*device)->setMesh3DMaterialNormalTextureBinding(0U, *baseColor).has_value());
    ASSERT_FALSE((*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(0U, *metallicRoughness).has_value());
    std::array<Render::Mesh3DDirectionalLight, 3> lights{
        Render::Mesh3DDirectionalLight{
            .directionTowardLightX = 0.2F,
            .directionTowardLightY = 0.8F,
            .directionTowardLightZ = 0.3F,
        },
        Render::Mesh3DDirectionalLight{
            .directionTowardLightX = -0.4F,
            .directionTowardLightY = 0.2F,
            .directionTowardLightZ = -0.3F,
            .colorR = 0.3F,
            .colorG = 0.3F,
            .colorB = 0.4F,
        },
        Render::Mesh3DDirectionalLight{
            .directionTowardLightX = 0.0F,
            .directionTowardLightY = 1.0F,
            .directionTowardLightZ = -1.0F,
            .colorR = 0.1F,
            .colorG = 0.15F,
            .colorB = 0.2F,
        },
    };
    ASSERT_TRUE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = lights,
        .ambientScale = 0.2F,
    }));
    ASSERT_TRUE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = {},
        .ambientScale = 0.1F,
    }));

    auto invalidDirection = lights;
    invalidDirection[1].directionTowardLightX = 0.0F;
    invalidDirection[1].directionTowardLightY = 0.0F;
    invalidDirection[1].directionTowardLightZ = 0.0F;
    auto directionFailure = (*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = invalidDirection,
        .ambientScale = 0.2F,
    });
    ASSERT_FALSE(directionFailure);
    EXPECT_EQ(directionFailure.error().code, Render::RenderErrorCode::InvalidMesh3DLighting);

    auto negativeColor = lights;
    negativeColor[0].colorR = -0.1F;
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = negativeColor,
        .ambientScale = 0.2F,
    }));
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = lights,
        .ambientScale = -0.1F,
    }));

    std::array<Render::Mesh3DDirectionalLight, Render::Mesh3DLightingDesc::MaximumDirectionalLightCount + 1U>
        tooManyLights{};
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = tooManyLights,
        .ambientScale = 0.2F,
    }));
    // Destroy while still bound: stale normal/MR/baseColor bindings must be scrubbed.
    ASSERT_TRUE((*device)->destroyTexture2D(*baseColor).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialNormalTextureBinding(7U, {}).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(7U, {}).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialTextureBinding(7U, {}).has_value());
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

TEST(NullRenderDeviceTextureTest, RetirementPinCompletesImmediatelyAndIsNotConsumedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<std::byte, 4> pixel{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto texture = (*device)->createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = 1,
        .height = 1,
        .rgba8Pixels = pixel,
    });
    ASSERT_TRUE(texture.has_value());

    Core::u32 releases = 0;
    Render::FramePin completionPin{Render::FramePinKind::AssetLease, 7, &releases, &countPinRelease};
    ASSERT_TRUE((*device)->retireTexture2D(*texture, completionPin).has_value());
    EXPECT_FALSE(completionPin.hasValue());
    EXPECT_EQ(releases, 1U);
    EXPECT_EQ((*device)->statistics().pendingGpuRetirements, 0U);
    EXPECT_EQ((*device)->statistics().completedGpuRetirements, 1U);

    Render::FramePin failurePin{Render::FramePinKind::AssetLease, 8, &releases, &countPinRelease};
    auto stale = (*device)->retireTexture2D(*texture, failurePin);
    ASSERT_FALSE(stale.has_value());
    EXPECT_TRUE(failurePin.hasValue());
    EXPECT_EQ(releases, 1U);
    failurePin.release();
    EXPECT_EQ(releases, 2U);
}

} // namespace Tina::Tests
