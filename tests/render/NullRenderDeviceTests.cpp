#include <gtest/gtest.h>

#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/render/UIDisplayList.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <array>
#include <limits>
#include <span>

namespace Tina::Tests {
namespace {

std::unique_ptr<Render::IRenderDevice>
createDevice(const Render::RenderDeviceCreateParams& params = Render::RenderDeviceCreateParams{})
{
    auto deviceResult = Render::createNullRenderDevice(params);
    EXPECT_TRUE(deviceResult.has_value());
    if (!deviceResult || *deviceResult == nullptr)
    {
        return nullptr;
    }
    return std::move(*deviceResult);
}

[[nodiscard]] constexpr Render::RenderSurfaceState activeSurface() noexcept
{
    return Render::RenderSurfaceState{
        .surface = {.owner = 1, .index = 0, .generation = 1},
        .framebufferExtent = {640, 480},
        .contentScale = {1.0F, 1.0F},
        .sourceMetricsRevision = 1,
        .surfaceRevision = 1,
        .availability = Render::RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] constexpr Render::RenderSurfaceState suspendedSurface() noexcept
{
    auto surface = activeSurface();
    surface.framebufferExtent = {0, 0};
    surface.availability = Render::RenderSurfaceAvailability::Suspended;
    return surface;
}

void releaseTestPin(void* userData) noexcept
{
    ++(*static_cast<u32*>(userData));
}

[[nodiscard]] Render::FrameResourceRef internResource(
    Render::RenderFramePacket& packet,
    Render::FrameResourceKind kind,
    u64 bindingKey,
    u32& releaseCount)
{
    Render::FramePin pin{
        Render::FramePinKind::Custom,
        bindingKey,
        &releaseCount,
        &releaseTestPin,
    };
    auto result = packet.intern(
        Render::FrameResourceDescriptor{
            .kind = kind,
            .deviceBindingKey = bindingKey,
        },
        std::move(pin));
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : Render::FrameResourceRef{};
}

[[nodiscard]] Render::FrameResourceRef internTexture(
    Render::RenderFramePacket& packet, u64 bindingKey, u32& releaseCount)
{
    return internResource(
        packet, Render::FrameResourceKind::Texture2D, bindingKey, releaseCount);
}

[[nodiscard]] Core::Result<Render::RenderSceneView> twoSpriteScene(
    Render::RenderSceneBuilder& builder,
    Render::FrameResourceRef firstTexture,
    Render::FrameResourceRef secondTexture)
{
    if (auto status = builder.beginFrame(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    Render::RenderSceneWriter writer = builder.writer();
    if (auto status = writer.setCamera2D(Render::RenderCamera2DInput{
            .stableCameraKey = 1,
            .worldWidth = 10.0F,
            .worldHeight = 10.0F,
            .actualPixelsPerMeter = 10.0F,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addSprite2D(Render::RenderSprite2DInput{
            .texture = firstTexture,
            .stableEntityKey = 1,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addSprite2D(Render::RenderSprite2DInput{
            .texture = secondTexture,
            .stableEntityKey = 2,
            .centerX = 1.0F,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return builder.commit();
}

[[nodiscard]] Core::Result<Render::RenderSceneView> oneSpriteScene(
    Render::RenderSceneBuilder& builder,
    Render::FrameResourceRef texture,
    Render::FrameResourceRef normalTexture = {})
{
    if (auto status = builder.beginFrame(); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    Render::RenderSceneWriter writer = builder.writer();
    if (auto status = writer.setCamera2D(Render::RenderCamera2DInput{
            .stableCameraKey = 1,
            .worldWidth = 10.0F,
            .worldHeight = 10.0F,
            .actualPixelsPerMeter = 10.0F,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addSprite2D(Render::RenderSprite2DInput{
            .texture = texture,
            .normalTexture = normalTexture,
            .stableEntityKey = 1,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return builder.commit();
}

[[nodiscard]] Core::Result<Render::RenderSceneView> oneMeshScene(
    Render::RenderSceneBuilder& builder,
    Render::FrameResourceRef mesh,
    Render::FrameResourceRef material)
{
    if (auto status = builder.beginFrame({.primarySurfaceAspectRatio = 1.0F}); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    Render::RenderSceneWriter writer = builder.writer();
    if (auto status = writer.setPerspectiveCamera(Render::RenderPerspectiveCameraInput{
            .stableCameraKey = 1,
            .verticalFovDegrees = 60.0F,
            .nearPlaneMeters = 0.1F,
            .farPlaneMeters = 100.0F,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addMesh3D(Render::RenderMesh3DInput{
            .mesh = mesh,
            .material = material,
            .stableEntityKey = 1,
            .localBounds = {.radius = 1.0F},
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return builder.commit();
}

[[nodiscard]] Core::Result<Render::RenderSceneView> oneSkinnedMeshScene(
    Render::RenderSceneBuilder& builder,
    Render::FrameResourceRef mesh,
    Render::FrameResourceRef material,
    std::span<const float> palette)
{
    if (auto status = builder.beginFrame({.primarySurfaceAspectRatio = 1.0F}); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    Render::RenderSceneWriter writer = builder.writer();
    if (auto status = writer.setPerspectiveCamera(Render::RenderPerspectiveCameraInput{
            .stableCameraKey = 1,
            .verticalFovDegrees = 60.0F,
            .nearPlaneMeters = 0.1F,
            .farPlaneMeters = 100.0F,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addSkinnedMesh3D(Render::RenderSkinnedMesh3DInput{
            .mesh = mesh,
            .material = material,
            .stableEntityKey = 1,
            .localBounds = {.radius = 1.0F},
            .paletteColumnMajorJointMatrices = palette,
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return builder.commit();
}

} // namespace

TEST(NullRenderDeviceTest, RejectsInvalidShadowMapExtentConfiguration)
{
    Render::RenderDeviceCreateParams params{};
    params.shadowMapExtents.pointLightFaceExtent = 384;

    auto result = Render::createNullRenderDevice(params);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code,
              Render::RenderErrorCode::InvalidShadowMapExtentConfig);
}

TEST(NullRenderDeviceTest, RejectsStructurallyInvalidInitialWindowSurface)
{
    const auto expectRejected = [](const Render::RenderSurfaceState& surface) {
        auto result =
            Render::createNullRenderDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = surface});
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Render::RenderErrorCode::InvalidSurfaceState);
    };

    auto surface = activeSurface();
    surface.surface = {};
    expectRejected(surface);

    surface = activeSurface();
    surface.surface.index = Render::RenderSurfaceId::InvalidIndex;
    expectRejected(surface);

    surface = activeSurface();
    surface.sourceMetricsRevision = 0;
    expectRejected(surface);

    surface = activeSurface();
    surface.surfaceRevision = 0;
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.x = 0.0F;
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.x = -1.0F;
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.x = (std::numeric_limits<float>::quiet_NaN)();
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.y = (std::numeric_limits<float>::infinity)();
    expectRejected(surface);

    surface = activeSurface();
    surface.framebufferExtent.width = 0;
    expectRejected(surface);

    surface = activeSurface();
    surface.availability = static_cast<Render::RenderSurfaceAvailability>(255);
    expectRejected(surface);
}

TEST(NullRenderDeviceTest, EnforcesSubmitPresentPairsAndContiguousFrameIndices)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);

    auto presentWithoutSubmit = device->present();
    ASSERT_FALSE(presentWithoutSubmit.has_value());
    EXPECT_EQ(presentWithoutSubmit.error().code, Render::RenderErrorCode::NoFrameSubmitted);

    auto wrongFirstFrame = device->submitFrame(Render::RenderFrame{.frameIndex = 1});
    ASSERT_FALSE(wrongFirstFrame.has_value());
    EXPECT_EQ(wrongFirstFrame.error().code, Render::RenderErrorCode::UnexpectedFrameIndex);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    auto duplicateSubmit = device->submitFrame(Render::RenderFrame{.frameIndex = 0});
    ASSERT_FALSE(duplicateSubmit.has_value());
    EXPECT_EQ(duplicateSubmit.error().code, Render::RenderErrorCode::FrameAlreadyOpen);

    ASSERT_TRUE(device->present().has_value());
    auto repeatedFrame = device->submitFrame(Render::RenderFrame{.frameIndex = 0});
    ASSERT_FALSE(repeatedFrame.has_value());
    EXPECT_EQ(repeatedFrame.error().code, Render::RenderErrorCode::UnexpectedFrameIndex);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 1}).has_value());
    ASSERT_TRUE(device->present().has_value());

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, 2U);
    EXPECT_EQ(statistics.presented, 2U);
    EXPECT_EQ(statistics.liveResources, 0U);
}

TEST(NullRenderDeviceTest, RejectsInvalidSpotLightSnapshotBeforeConsumingFrameState)
{
    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    ASSERT_TRUE(builder.beginFrame());

    const std::array spotLights{
        Render::Mesh3DSpotLight{
            .positionX = 1.0F,
            .positionY = 2.0F,
            .positionZ = 3.0F,
            .influenceRadius = 8.0F,
            .directionFromLightX = 0.0F,
            .directionFromLightY = -1.0F,
            .directionFromLightZ = 0.0F,
            .innerConeCosine = 0.9F,
            .outerConeCosine = 0.7F,
            .colorR = 1.0F,
            .colorG = 0.8F,
            .colorB = 0.6F,
        },
    };
    ASSERT_TRUE(builder.writer().setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = spotLights,
        .ambientScale = 0.2F,
    }));
    auto scene = builder.commit();
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_TRUE(scene->mesh3DLighting().has_value());

    auto committedSpotLights = scene->mesh3DLighting()->spotLights();
    ASSERT_EQ(committedSpotLights.size(), 1U);
    auto* corruptedSpotLight = const_cast<Render::Mesh3DSpotLight*>(committedSpotLights.data());
    corruptedSpotLight->outerConeCosine = corruptedSpotLight->innerConeCosine;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_EQ(device->statistics().submitted, 0U);
    EXPECT_EQ(device->statistics().skippedSuspendedSurfaceFrames, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
    EXPECT_EQ(device->statistics().submitted, 1U);
}

TEST(NullRenderDeviceTest, RejectsInvalidCascadedShadowSnapshotBeforeConsumingFrameState)
{
    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    ASSERT_TRUE(builder.beginFrame());

    const std::array directionalLights{Render::Mesh3DDirectionalLight{}};
    ASSERT_TRUE(builder.writer().setMesh3DLighting(Render::Mesh3DLightingDesc{
        .directionalLights = directionalLights,
        .cascadedDirectionalShadow = Render::Mesh3DCascadedDirectionalShadow{},
    }));
    auto scene = builder.commit();
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_TRUE(scene->mesh3DLighting().has_value());
    ASSERT_TRUE(scene->mesh3DLighting()->cascadedDirectionalShadow().has_value());

    auto* corruptedShadow = const_cast<Render::Mesh3DCascadedDirectionalShadow*>(
        &*scene->mesh3DLighting()->cascadedDirectionalShadow());
    corruptedShadow->directionalLightIndex = 1U;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_EQ(device->statistics().submitted, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
    EXPECT_EQ(device->statistics().submitted, 1U);
}

TEST(NullRenderDeviceTest, RejectsInvalidSpotShadowSnapshotBeforeConsumingFrameState)
{
    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    ASSERT_TRUE(builder.beginFrame());

    const std::array spotLights{Render::Mesh3DSpotLight{.influenceRadius = 4.0F}};
    ASSERT_TRUE(builder.writer().setMesh3DLighting(Render::Mesh3DLightingDesc{
        .spotLights = spotLights,
        .spotLightShadow = Render::Mesh3DSpotLightShadow{},
    }));
    auto scene = builder.commit();
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_TRUE(scene->mesh3DLighting().has_value());
    ASSERT_TRUE(scene->mesh3DLighting()->spotLightShadow().has_value());

    auto* corruptedShadow = const_cast<Render::Mesh3DSpotLightShadow*>(
        &*scene->mesh3DLighting()->spotLightShadow());
    corruptedShadow->nearPlaneMeters = spotLights[0].influenceRadius;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidMesh3DLighting);
    EXPECT_EQ(device->statistics().submitted, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
    EXPECT_EQ(device->statistics().submitted, 1U);
}

TEST(NullRenderDeviceTest, RejectsAnyCrossPacketSpriteTextureBeforeConsumingFrameOrStatistics)
{
    u32 firstReleaseCount = 0;
    u32 secondReleaseCount = 0;
    Render::RenderFramePacket firstPacket;
    Render::RenderFramePacket secondPacket;
    ASSERT_TRUE(firstPacket.beginFrame(0));
    ASSERT_TRUE(secondPacket.beginFrame(0));
    const Render::FrameResourceRef firstTexture = internTexture(firstPacket, 11, firstReleaseCount);
    const Render::FrameResourceRef secondTexture = internTexture(secondPacket, 22, secondReleaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 2});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = twoSpriteScene(builder, firstTexture, secondTexture);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = firstPacket.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);
    EXPECT_EQ(device->statistics().skippedSuspendedSurfaceFrames, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
    EXPECT_EQ(device->statistics().submitted, 1U);
}

TEST(NullRenderDeviceTest, RejectsCrossPacketSpriteNormalTextureBeforeConsumingFrameOrStatistics)
{
    u32 firstReleaseCount = 0;
    u32 secondReleaseCount = 0;
    Render::RenderFramePacket firstPacket;
    Render::RenderFramePacket secondPacket;
    ASSERT_TRUE(firstPacket.beginFrame(0));
    ASSERT_TRUE(secondPacket.beginFrame(0));
    const Render::FrameResourceRef texture = internTexture(firstPacket, 11, firstReleaseCount);
    const Render::FrameResourceRef normalTexture = internTexture(secondPacket, 22, secondReleaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 1});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneSpriteScene(builder, texture, normalTexture);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = firstPacket.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);
}

TEST(NullRenderDeviceTest, RejectsStaleSpriteNormalTextureBeforeConsumingFrameOrStatistics)
{
    u32 releaseCount = 0;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    const Render::FrameResourceRef staleNormalTexture = internTexture(packet, 22, releaseCount);
    ASSERT_TRUE(packet.beginFrame(1));
    const Render::FrameResourceRef texture = internTexture(packet, 11, releaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 1});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneSpriteScene(builder, texture, staleNormalTexture);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = packet.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);
}

TEST(NullRenderDeviceTest, RejectsWrongKindSpriteNormalTextureBeforeConsumingFrameOrStatistics)
{
    u32 releaseCount = 0;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    const Render::FrameResourceRef texture = internTexture(packet, 11, releaseCount);
    const Render::FrameResourceRef wrongNormalTexture = internResource(
        packet, Render::FrameResourceKind::Mesh3DMaterial, 22, releaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 1});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneSpriteScene(builder, texture, wrongNormalTexture);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = packet.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);
}

TEST(NullRenderDeviceTest, RejectsOutOfRangeSpriteNormalTextureOnSuspendedFrameBeforeStatistics)
{
    u32 releaseCount = 0;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    const Render::FrameResourceRef texture = internTexture(packet, 11, releaseCount);
    constexpr u64 OversizedBindingKey =
        static_cast<u64>((std::numeric_limits<u32>::max)()) + 1U;
    const Render::FrameResourceRef normalTexture =
        internTexture(packet, OversizedBindingKey, releaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 1});
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneSpriteScene(builder, texture, normalTexture);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    const Render::RenderSurfaceState suspended = suspendedSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{
        .initialPrimaryWindowSurface = suspended,
    });
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .primaryWindowSurface = suspended,
        .resources = packet.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);
    EXPECT_EQ(device->statistics().skippedSuspendedSurfaceFrames, 0U);
}

TEST(NullRenderDeviceTest, RejectsCrossPacketMeshResourcesBeforeConsumingFrameOrStatistics)
{
    u32 firstReleaseCount = 0;
    u32 secondReleaseCount = 0;
    Render::RenderFramePacket firstPacket;
    Render::RenderFramePacket secondPacket;
    ASSERT_TRUE(firstPacket.beginFrame(0));
    ASSERT_TRUE(secondPacket.beginFrame(0));
    const Render::FrameResourceRef mesh = internResource(
        firstPacket, Render::FrameResourceKind::Mesh3DGeometry, 11, firstReleaseCount);
    const Render::FrameResourceRef material = internResource(
        secondPacket, Render::FrameResourceKind::Mesh3DMaterial, 22, secondReleaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{
        .mesh3DItemCapacity = 1,
        .mesh3DBatchCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneMeshScene(builder, mesh, material);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = firstPacket.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsWrongKindMeshResourcesBeforeConsumingFrameOrStatistics)
{
    u32 releaseCount = 0;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    const Render::FrameResourceRef wrongMesh = internResource(
        packet, Render::FrameResourceKind::Mesh3DMaterial, 11, releaseCount);
    const Render::FrameResourceRef wrongMaterial = internResource(
        packet, Render::FrameResourceKind::Mesh3DGeometry, 22, releaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{
        .mesh3DItemCapacity = 1,
        .mesh3DBatchCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneMeshScene(builder, wrongMesh, wrongMaterial);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = packet.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsStaleMeshResourcesBeforeConsumingFrameOrStatistics)
{
    u32 releaseCount = 0;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    const Render::FrameResourceRef mesh = internResource(
        packet, Render::FrameResourceKind::Mesh3DGeometry, 11, releaseCount);
    const Render::FrameResourceRef material = internResource(
        packet, Render::FrameResourceKind::Mesh3DMaterial, 22, releaseCount);
    const Render::FrameResourceTableView staleResources = packet.resourceTableView();

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{
        .mesh3DItemCapacity = 1,
        .mesh3DBatchCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneMeshScene(builder, mesh, material);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_TRUE(packet.abandon().has_value());
    EXPECT_EQ(releaseCount, 2U);

    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .resources = staleResources,
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsOutOfRangeMeshResourcesOnSuspendedFrameBeforeStatistics)
{
    u32 releaseCount = 0;
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(0));
    constexpr u64 OversizedBindingKey =
        static_cast<u64>((std::numeric_limits<u32>::max)()) + 1U;
    const Render::FrameResourceRef mesh = internResource(
        packet, Render::FrameResourceKind::Mesh3DGeometry, OversizedBindingKey, releaseCount);
    const Render::FrameResourceRef material = internResource(
        packet, Render::FrameResourceKind::Mesh3DMaterial, 22, releaseCount);

    auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{
        .mesh3DItemCapacity = 1,
        .mesh3DBatchCapacity = 1,
    });
    ASSERT_TRUE(builderResult.has_value()) << builderResult.error().message;
    Render::RenderSceneBuilder builder = std::move(*builderResult);
    auto scene = oneMeshScene(builder, mesh, material);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto suspended = suspendedSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{
        .initialPrimaryWindowSurface = suspended,
    });
    ASSERT_NE(device, nullptr);
    auto invalid = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .primaryWindowSurface = suspended,
        .resources = packet.resourceTableView(),
        .primaryWorldScene = *scene,
    });
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_EQ(device->statistics().submitted, 0U);
    EXPECT_EQ(device->statistics().skippedSuspendedSurfaceFrames, 0U);

    auto skipped = device->submitFrame(Render::RenderFrame{
        .frameIndex = 0,
        .primaryWindowSurface = suspended,
    });
    ASSERT_TRUE(skipped.has_value());
    EXPECT_EQ(skipped->kind, Render::RenderFrameSubmissionKind::SkippedSuspendedSurface);
}

TEST(NullRenderDeviceTest, SkinnedSubmissionRequiresMatchingBoundSkeletonAndResourceKind)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    std::array<float, 36> vertices{
        0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1,
    };
    std::array<u16, 12> jointIndices{};
    const std::array<u16, 12> jointWeights{
        65535, 0, 0, 0,
        65535, 0, 0, 0,
        65535, 0, 0, 0,
    };
    const std::array<u16, 3> indices{0, 1, 2};
    auto skinnedGpu = device->createSkinnedMesh(Render::SkinnedMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .jointCount = 1,
        .vertices = vertices,
        .jointIndices = jointIndices,
        .jointWeights = jointWeights,
        .indices = indices,
    });
    ASSERT_TRUE(skinnedGpu.has_value()) << (skinnedGpu ? "" : skinnedGpu.error().message);
    auto staticGpu = device->createStaticMesh(Render::StaticMeshUploadDesc{
        .vertexCount = 3,
        .indexCount = 3,
        .vertices = vertices,
        .indices = indices,
    });
    ASSERT_TRUE(staticGpu.has_value()) << (staticGpu ? "" : staticGpu.error().message);
    ASSERT_TRUE(device->setMesh3DBinding(7U, *skinnedGpu));
    ASSERT_TRUE(device->setMesh3DBinding(8U, *staticGpu));

    constexpr std::array<float, Render::SkinnedMesh3DPaletteFloatsPerJoint> IdentityPalette{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    const auto submitWith = [&](Render::FrameResourceKind meshKind,
                                u32 meshKey,
                                std::span<const float> palette,
                                u64 frameIndex)
        -> Core::Result<Render::RenderFrameSubmission> {
        u32 releaseCount = 0;
        Render::RenderFramePacket packet;
        EXPECT_TRUE(packet.beginFrame(frameIndex));
        const Render::FrameResourceRef mesh =
            internResource(packet, meshKind, meshKey, releaseCount);
        const Render::FrameResourceRef material = internResource(
            packet, Render::FrameResourceKind::Mesh3DMaterial, 22, releaseCount);
        auto builderResult = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{
            .mesh3DItemCapacity = 1,
            .mesh3DBatchCapacity = 1,
            .skinnedMesh3DItemCapacity = 1,
        });
        EXPECT_TRUE(builderResult.has_value());
        if (!builderResult)
        {
            return Core::failure(std::move(builderResult.error()));
        }
        Render::RenderSceneBuilder builder = std::move(*builderResult);
        auto scene = oneSkinnedMeshScene(builder, mesh, material, palette);
        EXPECT_TRUE(scene.has_value());
        if (!scene)
        {
            return Core::failure(std::move(scene.error()));
        }
        return device->submitFrame(Render::RenderFrame{
            .frameIndex = frameIndex,
            .resources = packet.resourceTableView(),
            .primaryWorldScene = *scene,
        });
    };

    auto wrongKind = submitWith(
        Render::FrameResourceKind::Mesh3DGeometry, 7U, IdentityPalette, 0U);
    ASSERT_FALSE(wrongKind.has_value());
    EXPECT_EQ(wrongKind.error().code, Render::RenderErrorCode::InvalidFrameResource);

    auto staticBinding = submitWith(
        Render::FrameResourceKind::SkinnedMesh3DGeometry, 8U, IdentityPalette, 0U);
    ASSERT_FALSE(staticBinding.has_value());
    EXPECT_EQ(staticBinding.error().code, Render::RenderErrorCode::InvalidFrameResource);

    std::array<float, 2U * Render::SkinnedMesh3DPaletteFloatsPerJoint> twoJointPalette{};
    auto mismatchedPalette = submitWith(
        Render::FrameResourceKind::SkinnedMesh3DGeometry, 7U, twoJointPalette, 0U);
    ASSERT_FALSE(mismatchedPalette.has_value());
    EXPECT_EQ(mismatchedPalette.error().code, Render::RenderErrorCode::InvalidFrameResource);

    auto accepted = submitWith(
        Render::FrameResourceKind::SkinnedMesh3DGeometry, 7U, IdentityPalette, 0U);
    ASSERT_TRUE(accepted.has_value()) << (accepted ? "" : accepted.error().message);
    ASSERT_TRUE(device->present());
    ASSERT_TRUE(device->destroyStaticMesh(*skinnedGpu));
    ASSERT_TRUE(device->destroyStaticMesh(*staticGpu));
}

TEST(NullRenderDeviceTest, RunsThreeHundredFramesWithoutGpuResources)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);
    auto builderResult = Render::UIDisplayListBuilder::Create({
        .commandCount = 1,
        .clipCount = 0,
        .batchCount = 1,
    });
    ASSERT_TRUE(builderResult.has_value());
    auto builder = std::move(*builderResult);
    const Render::UIDrawCommand* fixedCommandStorage = nullptr;

    constexpr u64 frameCount = 300;
    for (u64 frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        ASSERT_TRUE(builder.beginFrame().has_value());
        ASSERT_TRUE(builder
                        .addSolidQuad({
                            .paintOrdinal = 0,
                            .bounds = {0, 0, 64, 64},
                            .color = {.red = 20, .green = 40, .blue = 80, .alpha = 255},
                        })
                        .has_value());
        auto displayList = builder.commit();
        ASSERT_TRUE(displayList.has_value());
        ASSERT_EQ(displayList->commands().size(), 1U);
        if (fixedCommandStorage == nullptr)
        {
            fixedCommandStorage = displayList->commands().data();
        }
        EXPECT_EQ(displayList->commands().data(), fixedCommandStorage);

        ASSERT_TRUE(device
                        ->submitFrame(Render::RenderFrame{
                            .frameIndex = frameIndex,
                            .interpolation = 0.5,
                            .primaryWindowSurface = std::nullopt,
                            .primaryWindowUIDisplayList = *displayList,
                        })
                        .has_value());
        ASSERT_TRUE(device->present().has_value());
    }

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, frameCount);
    EXPECT_EQ(statistics.presented, frameCount);
    EXPECT_EQ(statistics.liveResources, 0U);
    EXPECT_EQ(builder.statistics().committedBuildCount, frameCount);
}

TEST(NullRenderDeviceTest, SuspendedWindowSurfaceSkipsSubmissionButKeepsEngineFrameSequence)
{
    auto suspended = suspendedSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = suspended});
    ASSERT_NE(device, nullptr);

    constexpr u64 suspendedFrameCount = 300;
    for (u64 frameIndex = 0; frameIndex < suspendedFrameCount; ++frameIndex)
    {
        suspended.sourceMetricsRevision = frameIndex + 1;
        auto skipped = device->submitFrame(Render::RenderFrame{
            .frameIndex = frameIndex,
            .primaryWindowSurface = suspended,
        });
        ASSERT_TRUE(skipped.has_value());
        EXPECT_EQ(skipped->kind, Render::RenderFrameSubmissionKind::SkippedSuspendedSurface);
        EXPECT_FALSE(skipped->requiresPresent());
    }

    auto active = suspended;
    active.framebufferExtent = {640, 480};
    active.sourceMetricsRevision = suspendedFrameCount + 1;
    active.surfaceRevision = 2;
    active.availability = Render::RenderSurfaceAvailability::Active;
    auto submitted = device->submitFrame(Render::RenderFrame{
        .frameIndex = suspendedFrameCount,
        .primaryWindowSurface = active,
    });
    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(submitted->submissionIndex, 0U);
    ASSERT_TRUE(device->present().has_value());

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, 1U);
    EXPECT_EQ(statistics.presented, 1U);
    EXPECT_EQ(statistics.skippedSuspendedSurfaceFrames, suspendedFrameCount);
}

TEST(NullRenderDeviceTest, RejectsWindowSurfaceCompositionPresenceChanges)
{
    const auto initial = activeSurface();
    auto surfaceDevice = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(surfaceDevice, nullptr);

    auto missingSurface = surfaceDevice->submitFrame(Render::RenderFrame{.frameIndex = 0});
    ASSERT_FALSE(missingSurface.has_value());
    EXPECT_EQ(missingSurface.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto recoveredSurface =
        surfaceDevice->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial});
    ASSERT_TRUE(recoveredSurface.has_value());
    ASSERT_TRUE(surfaceDevice->present().has_value());

    auto surfaceFreeDevice = createDevice();
    ASSERT_NE(surfaceFreeDevice, nullptr);
    auto unexpectedSurface =
        surfaceFreeDevice->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial});
    ASSERT_FALSE(unexpectedSurface.has_value());
    EXPECT_EQ(unexpectedSurface.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    ASSERT_TRUE(surfaceFreeDevice->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(surfaceFreeDevice->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsWindowSurfaceIdentityChangesWithoutConsumingTheFrame)
{
    const auto initial = activeSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(device, nullptr);

    auto changedIdentity = initial;
    changedIdentity.surface.generation = 2;
    auto invalid = device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedIdentity});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsWindowSurfaceRevisionRollbackWithoutMutatingCommittedState)
{
    auto initial = activeSurface();
    initial.sourceMetricsRevision = 4;
    initial.surfaceRevision = 4;
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(device, nullptr);

    auto sourceRevisionRollback = initial;
    sourceRevisionRollback.sourceMetricsRevision = 3;
    auto invalidSourceRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = sourceRevisionRollback});
    ASSERT_FALSE(invalidSourceRevision.has_value());
    EXPECT_EQ(invalidSourceRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto surfaceRevisionRollback = initial;
    surfaceRevisionRollback.surfaceRevision = 3;
    auto invalidSurfaceRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = surfaceRevisionRollback});
    ASSERT_FALSE(invalidSurfaceRevision.has_value());
    EXPECT_EQ(invalidSurfaceRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RequiresSurfaceRevisionToMatchCommittedFactChanges)
{
    const auto initial = activeSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(device, nullptr);

    auto changedFactsWithoutRevision = initial;
    changedFactsWithoutRevision.framebufferExtent = {800, 600};
    changedFactsWithoutRevision.sourceMetricsRevision = 2;
    auto missingRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFactsWithoutRevision});
    ASSERT_FALSE(missingRevision.has_value());
    EXPECT_EQ(missingRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto changedFactsWithoutNewMetrics = changedFactsWithoutRevision;
    changedFactsWithoutNewMetrics.sourceMetricsRevision = initial.sourceMetricsRevision;
    changedFactsWithoutNewMetrics.surfaceRevision = 2;
    auto staleMetrics = device->submitFrame(
        Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFactsWithoutNewMetrics});
    ASSERT_FALSE(staleMetrics.has_value());
    EXPECT_EQ(staleMetrics.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto changedFactsWithSkippedRevision = changedFactsWithoutRevision;
    changedFactsWithSkippedRevision.surfaceRevision = 3;
    auto skippedRevision = device->submitFrame(
        Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFactsWithSkippedRevision});
    ASSERT_FALSE(skippedRevision.has_value());
    EXPECT_EQ(skippedRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto changedFacts = changedFactsWithoutRevision;
    changedFacts.surfaceRevision = 2;
    ASSERT_TRUE(
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFacts}).has_value());
    ASSERT_TRUE(device->present().has_value());

    auto changedRevisionWithoutFacts = changedFacts;
    changedRevisionWithoutFacts.sourceMetricsRevision = 3;
    changedRevisionWithoutFacts.surfaceRevision = 3;
    auto spuriousRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 1, .primaryWindowSurface = changedRevisionWithoutFacts});
    ASSERT_FALSE(spuriousRevision.has_value());
    EXPECT_EQ(spuriousRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    changedFacts.sourceMetricsRevision = 3;
    ASSERT_TRUE(
        device->submitFrame(Render::RenderFrame{.frameIndex = 1, .primaryWindowSurface = changedFacts}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsWorkAfterIdempotentShutdown)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);

    device->shutdown();
    device->shutdown();

    auto submitResult = device->submitFrame(Render::RenderFrame{});
    ASSERT_FALSE(submitResult.has_value());
    EXPECT_EQ(submitResult.error().code, Render::RenderErrorCode::DeviceStopped);

    auto presentResult = device->present();
    ASSERT_FALSE(presentResult.has_value());
    EXPECT_EQ(presentResult.error().code, Render::RenderErrorCode::DeviceStopped);

    EXPECT_EQ(device->statistics().liveResources, 0U);
}

} // namespace Tina::Tests
