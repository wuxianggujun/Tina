#include <gtest/gtest.h>

#include <tina/render/RenderDevice.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <limits>
#include <span>
#include <vector>

namespace Tina::Tests {
namespace {

void countPinRelease(void* userData) noexcept
{
    ++*static_cast<Core::u32*>(userData);
}

[[nodiscard]] Core::Result<Render::GpuTextureId>
uploadRgba8(Render::IRenderDevice& device, std::span<const std::byte> pixels,
            Core::u16 width = 1, Core::u16 height = 1)
{
    const std::array levels{
        Render::Texture2DUploadLevel{.width = width, .height = height, .bytes = pixels},
    };
    return device.createTexture2D(Render::Texture2DUploadDesc{.levels = levels});
}

} // namespace

TEST(NullRenderDeviceTextureTest, CreateBindDestroyLifecycle)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    auto foreignDevice = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(foreignDevice.has_value());

    std::array<std::byte, 4> pixel{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto texture = uploadRgba8(**device, pixel);
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    auto foreignTexture = uploadRgba8(**foreignDevice, pixel);
    ASSERT_TRUE(foreignTexture.has_value()) << foreignTexture.error().message;
    EXPECT_EQ(texture->index, foreignTexture->index);
    EXPECT_EQ(texture->generation, foreignTexture->generation);
    EXPECT_NE(texture->owner, foreignTexture->owner);
    EXPECT_EQ((*device)->statistics().liveResources, 1U);

    auto foreignValidation = (*device)->validateTexture2D(*foreignTexture);
    ASSERT_FALSE(foreignValidation.has_value());
    EXPECT_EQ(foreignValidation.error().code, Render::RenderErrorCode::TextureNotFound);
    auto foreignBinding = (*device)->setTexture2DBinding(1U, *foreignTexture);
    ASSERT_FALSE(foreignBinding.has_value());
    EXPECT_EQ(foreignBinding.error().code, Render::RenderErrorCode::TextureNotFound);
    auto foreignDestroy = (*device)->destroyTexture2D(*foreignTexture);
    ASSERT_FALSE(foreignDestroy.has_value());
    EXPECT_EQ(foreignDestroy.error().code, Render::RenderErrorCode::TextureNotFound);

    ASSERT_TRUE((*device)->setTexture2DBinding(1U, *texture).has_value());
    ASSERT_TRUE((*device)->setTexture2DBinding(1U, {}).has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*texture).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);

    auto stale = (*device)->destroyTexture2D(*texture);
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().code, Render::RenderErrorCode::TextureNotFound);
    ASSERT_TRUE((*foreignDevice)->destroyTexture2D(*foreignTexture).has_value());
}

TEST(NullRenderDeviceTextureTest, MaterialBaseColorAndMetallicRoughnessBindings)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<std::byte, 4> pixel{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255}};
    auto baseColor = uploadRgba8(**device, pixel);
    ASSERT_TRUE(baseColor.has_value()) << baseColor.error().message;
    auto metallicRoughness = uploadRgba8(**device, pixel);
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
        .cascadedDirectionalShadow = Render::Mesh3DCascadedDirectionalShadow{},
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

    const auto invalidShadowDistance = Render::Mesh3DCascadedDirectionalShadow{
        .maximumDistanceMeters = 0.0F,
    };
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = lights,
        .cascadedDirectionalShadow = invalidShadowDistance,
        .ambientScale = 0.2F,
    }));

    const auto invalidShadowIndex = Render::Mesh3DCascadedDirectionalShadow{
        .directionalLightIndex = static_cast<u32>(lights.size()),
    };
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = lights,
        .cascadedDirectionalShadow = invalidShadowIndex,
        .ambientScale = 0.2F,
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

TEST(NullRenderDeviceLightingTest, AcceptsBoundedPointLightsAndRejectsInvalidValues)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    const std::array pointLights{
        Render::Mesh3DPointLight{
            .positionX = 1.0F,
            .positionY = 2.0F,
            .positionZ = 3.0F,
            .influenceRadius = 4.0F,
            .colorR = 0.8F,
            .colorG = 0.6F,
            .colorB = 0.4F,
        },
        Render::Mesh3DPointLight{
            .positionX = -1.0F,
            .positionY = 0.5F,
            .positionZ = 2.0F,
            .influenceRadius = 8.0F,
            .colorR = 0.2F,
            .colorG = 0.3F,
            .colorB = 0.5F,
        },
    };
    ASSERT_TRUE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = pointLights,
        .pointLightShadow = Render::Mesh3DPointLightShadow{
            .pointLightIndex = 1U,
            .nearPlaneMeters = 0.1F,
        },
        .ambientScale = 0.2F,
    }));

    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = pointLights,
        .pointLightShadow = Render::Mesh3DPointLightShadow{
            .pointLightIndex = 2U,
        },
        .ambientScale = 0.2F,
    }));
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = pointLights,
        .pointLightShadow = Render::Mesh3DPointLightShadow{
            .pointLightIndex = 0U,
            .nearPlaneMeters = pointLights[0].influenceRadius,
        },
        .ambientScale = 0.2F,
    }));

    auto invalidPointLights = pointLights;
    invalidPointLights[0].influenceRadius = 0.0F;
    auto pointRadiusFailure = (*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = invalidPointLights,
        .ambientScale = 0.2F,
    });
    ASSERT_FALSE(pointRadiusFailure);
    EXPECT_EQ(pointRadiusFailure.error().code, Render::RenderErrorCode::InvalidMesh3DLighting);

    invalidPointLights = pointLights;
    invalidPointLights[1].colorG = -0.1F;
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = invalidPointLights,
        .ambientScale = 0.2F,
    }));

    invalidPointLights = pointLights;
    invalidPointLights[0].positionZ = std::numeric_limits<float>::infinity();
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = invalidPointLights,
        .ambientScale = 0.2F,
    }));

    std::array<Render::Mesh3DPointLight, Render::Mesh3DLightingDesc::MaximumPointLightCount + 1U>
        tooManyPointLights{};
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .pointLights = tooManyPointLights,
        .ambientScale = 0.2F,
    }));
}

TEST(NullRenderDeviceLightingTest, AcceptsBoundedSpotLightsAndRejectsInvalidValues)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    const std::array spotLights{
        Render::Mesh3DSpotLight{
            .positionX = 1.0F,
            .positionY = 2.0F,
            .positionZ = 3.0F,
            .influenceRadius = 6.0F,
            .directionFromLightX = 0.0F,
            .directionFromLightY = -2.0F,
            .directionFromLightZ = 1.0F,
            .innerConeCosine = 0.9F,
            .outerConeCosine = 0.7F,
            .colorR = 0.8F,
            .colorG = 0.6F,
            .colorB = 0.4F,
        },
        Render::Mesh3DSpotLight{
            .positionX = -1.0F,
            .positionY = 0.5F,
            .positionZ = 2.0F,
            .influenceRadius = 9.0F,
            .directionFromLightX = 1.0F,
            .directionFromLightY = -1.0F,
            .directionFromLightZ = 0.0F,
            .innerConeCosine = 0.8F,
            .outerConeCosine = 0.4F,
            .colorR = 0.2F,
            .colorG = 0.3F,
            .colorB = 0.5F,
        },
    };
    ASSERT_TRUE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = spotLights,
        .ambientScale = 0.2F,
    }));

    auto invalidSpotLights = spotLights;
    invalidSpotLights[0].directionFromLightX = 0.0F;
    invalidSpotLights[0].directionFromLightY = 0.0F;
    invalidSpotLights[0].directionFromLightZ = 0.0F;
    auto directionFailure = (*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = invalidSpotLights,
        .ambientScale = 0.2F,
    });
    ASSERT_FALSE(directionFailure);
    EXPECT_EQ(directionFailure.error().code, Render::RenderErrorCode::InvalidMesh3DLighting);

    invalidSpotLights = spotLights;
    invalidSpotLights[0].innerConeCosine = invalidSpotLights[0].outerConeCosine;
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = invalidSpotLights,
        .ambientScale = 0.2F,
    }));

    invalidSpotLights = spotLights;
    invalidSpotLights[1].outerConeCosine = -1.1F;
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = invalidSpotLights,
        .ambientScale = 0.2F,
    }));

    invalidSpotLights = spotLights;
    invalidSpotLights[1].influenceRadius = std::numeric_limits<float>::infinity();
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = invalidSpotLights,
        .ambientScale = 0.2F,
    }));

    invalidSpotLights = spotLights;
    invalidSpotLights[0].colorB = -0.1F;
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = invalidSpotLights,
        .ambientScale = 0.2F,
    }));

    std::array<Render::Mesh3DSpotLight, Render::Mesh3DLightingDesc::MaximumSpotLightCount + 1U>
        tooManySpotLights{};
    ASSERT_FALSE((*device)->setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = tooManySpotLights,
        .ambientScale = 0.2F,
    }));
}

TEST(NullRenderDeviceTextureTest, MaterialBundleUpdatesComposeAndClearIsIdempotent)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());

    std::array<std::byte, 4> pixel{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255}};
    const auto uploadTexture = [&]() {
        return uploadRgba8(**device, pixel);
    };
    auto baseColor = uploadTexture();
    auto metallicRoughness = uploadTexture();
    auto normal = uploadTexture();
    auto replacement = uploadTexture();
    auto stale = uploadTexture();
    ASSERT_TRUE(baseColor.has_value());
    ASSERT_TRUE(metallicRoughness.has_value());
    ASSERT_TRUE(normal.has_value());
    ASSERT_TRUE(replacement.has_value());
    ASSERT_TRUE(stale.has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*stale).has_value());

    auto invalid = (*device)->createMesh3DMaterialBinding(Render::Mesh3DMaterialBindingDesc{
        .baseColorTexture = *baseColor,
        .metallicRoughnessTexture = *stale,
        .normalTexture = *normal,
        .metallicFactor = 0.25F,
        .roughnessFactor = 0.75F,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::TextureNotFound);

    const Render::Mesh3DMaterialBindingDesc desc{
        .baseColorTexture = *baseColor,
        .metallicRoughnessTexture = *metallicRoughness,
        .normalTexture = *normal,
        .metallicFactor = 0.25F,
        .roughnessFactor = 0.75F,
    };
    auto first = (*device)->createMesh3DMaterialBinding(desc);
    auto second = (*device)->createMesh3DMaterialBinding(desc);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(*first, 2U);
    EXPECT_EQ(*second, 3U);

    ASSERT_TRUE((*device)->clearMesh3DMaterialBinding(*first).has_value());
    ASSERT_TRUE((*device)->clearMesh3DMaterialBinding(*first).has_value());
    auto third = (*device)->createMesh3DMaterialBinding(desc);
    ASSERT_TRUE(third.has_value()) << third.error().message;
    EXPECT_EQ(*third, 4U);

    auto invalidReplacement = (*device)->setMesh3DMaterialBinding(
        *second, Render::Mesh3DMaterialBindingDesc{
                     .baseColorTexture = *normal,
                     .metallicRoughnessTexture = *metallicRoughness,
                     .normalTexture = *baseColor,
                     .metallicFactor = 1.25F,
                     .roughnessFactor = 0.5F,
                 });
    ASSERT_FALSE(invalidReplacement.has_value());
    EXPECT_EQ(invalidReplacement.error().code, Render::RenderErrorCode::InvalidTextureUpload);

    ASSERT_TRUE((*device)
                    ->setMesh3DMaterialBinding(
                        *second, Render::Mesh3DMaterialBindingDesc{
                                     .baseColorTexture = *normal,
                                     .metallicRoughnessTexture = *baseColor,
                                     .normalTexture = *metallicRoughness,
                                     .metallicFactor = 0.4F,
                                     .roughnessFactor = 0.6F,
                                 })
                    .has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialTextureBinding(*second, *replacement).has_value());
    ASSERT_TRUE(
        (*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(*second, *normal).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialNormalTextureBinding(*second, *metallicRoughness).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialFactors(*second, 0.2F, 0.8F).has_value());

    ASSERT_TRUE((*device)->destroyTexture2D(*normal).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialFactors(*second, 0.3F, 0.7F).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialTextureBinding(*second, {}).has_value());
    ASSERT_TRUE(
        (*device)->setMesh3DMaterialMetallicRoughnessTextureBinding(*second, *replacement).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialNormalTextureBinding(*second, {}).has_value());
    ASSERT_TRUE((*device)->setMesh3DMaterialTextureBinding(*second, *metallicRoughness).has_value());

    ASSERT_TRUE((*device)->clearMesh3DMaterialBinding(*second).has_value());
    ASSERT_TRUE((*device)->clearMesh3DMaterialBinding(*third).has_value());

    ASSERT_TRUE((*device)->destroyTexture2D(*baseColor).has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*metallicRoughness).has_value());
    ASSERT_TRUE((*device)->destroyTexture2D(*replacement).has_value());
    EXPECT_EQ((*device)->statistics().liveResources, 0U);
}

TEST(NullRenderDeviceTextureTest, RejectsBadUploadSize)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<std::byte, 2> bad{std::byte{1}, std::byte{2}};
    const std::array levels{
        Render::Texture2DUploadLevel{.width = 1, .height = 1, .bytes = bad},
    };
    auto texture = (*device)->createTexture2D(Render::Texture2DUploadDesc{
        .levels = levels,
    });
    ASSERT_FALSE(texture.has_value());
    EXPECT_EQ(texture.error().code, Render::RenderErrorCode::InvalidTextureUpload);
}

TEST(NullRenderDeviceTextureTest, AcceptsCompleteMipChainAndCountsEveryUploadedLevel)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<std::byte, 64> level0{};
    std::array<std::byte, 16> level1{};
    std::array<std::byte, 4> level2{};
    const std::array levels{
        Render::Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
        Render::Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level1},
        Render::Texture2DUploadLevel{.width = 1, .height = 1, .bytes = level2},
    };
    auto texture = (*device)->createTexture2D(Render::Texture2DUploadDesc{
        .colorSpace = Render::GpuTextureColorSpace::Linear,
        .sampler =
            {
                .wrapU = Render::GpuTextureWrapMode::Clamp,
                .wrapV = Render::GpuTextureWrapMode::Mirror,
                .minFilter = Render::GpuTextureFilterMode::Anisotropic,
                .magFilter = Render::GpuTextureFilterMode::Anisotropic,
                .mipFilter = Render::GpuTextureMipFilterMode::Linear,
            },
        .levels = levels,
    });
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ((*device)->statistics().uploadedTextureLevels, 3U);
    ASSERT_TRUE((*device)->destroyTexture2D(*texture).has_value());
}

TEST(NullRenderDeviceTextureTest, AcceptsOddCompressedMipChainWithWholeTailBlocks)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<std::byte, 32> level0{};
    std::array<std::byte, 8> level1{};
    std::array<std::byte, 8> level2{};
    const std::array levels{
        Render::Texture2DUploadLevel{.width = 7, .height = 5, .bytes = level0},
        Render::Texture2DUploadLevel{.width = 3, .height = 2, .bytes = level1},
        Render::Texture2DUploadLevel{.width = 1, .height = 1, .bytes = level2},
    };

    auto texture = (*device)->createTexture2D(Render::Texture2DUploadDesc{
        .format = Render::GpuTextureFormat::Bc1Rgba,
        .sampler = {.mipFilter = Render::GpuTextureMipFilterMode::Point},
        .levels = levels,
    });
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ((*device)->statistics().uploadedTextureLevels, 3U);
    ASSERT_TRUE((*device)->destroyTexture2D(*texture).has_value());
}

TEST(NullRenderDeviceTextureTest, RejectsMipFilterAndAnisotropicSamplerMismatch)
{
    std::array<std::byte, 64> level0{};
    std::array<std::byte, 16> level1{};
    std::array<std::byte, 4> level2{};
    const std::array singleLevel{
        Render::Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
    };
    auto singleWithMipFilter = Render::validateTexture2DUploadDesc(Render::Texture2DUploadDesc{
        .sampler = {.mipFilter = Render::GpuTextureMipFilterMode::Linear},
        .levels = singleLevel,
    });
    ASSERT_FALSE(singleWithMipFilter.has_value());
    EXPECT_EQ(singleWithMipFilter.error().code, Render::RenderErrorCode::InvalidTextureUpload);

    const std::array completeLevels{
        Render::Texture2DUploadLevel{.width = 4, .height = 4, .bytes = level0},
        Render::Texture2DUploadLevel{.width = 2, .height = 2, .bytes = level1},
        Render::Texture2DUploadLevel{.width = 1, .height = 1, .bytes = level2},
    };
    auto chainWithoutMipFilter = Render::validateTexture2DUploadDesc(Render::Texture2DUploadDesc{
        .sampler = {.mipFilter = Render::GpuTextureMipFilterMode::None},
        .levels = completeLevels,
    });
    ASSERT_FALSE(chainWithoutMipFilter.has_value());
    EXPECT_EQ(chainWithoutMipFilter.error().code, Render::RenderErrorCode::InvalidTextureUpload);

    auto asymmetricAnisotropic = Render::validateTexture2DUploadDesc(Render::Texture2DUploadDesc{
        .sampler =
            {
                .minFilter = Render::GpuTextureFilterMode::Anisotropic,
                .magFilter = Render::GpuTextureFilterMode::Linear,
                .mipFilter = Render::GpuTextureMipFilterMode::None,
            },
        .levels = singleLevel,
    });
    ASSERT_FALSE(asymmetricAnisotropic.has_value());
    EXPECT_EQ(asymmetricAnisotropic.error().code, Render::RenderErrorCode::InvalidTextureUpload);
}

TEST(NullRenderDeviceTextureTest, RetirementPinCompletesImmediatelyAndIsNotConsumedOnFailure)
{
    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    std::array<std::byte, 4> pixel{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto texture = uploadRgba8(**device, pixel);
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
