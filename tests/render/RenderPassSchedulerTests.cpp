#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderPassScheduler.hpp>

#include <gtest/gtest.h>

namespace Tina::Render {
namespace {

[[nodiscard]] FrameResourceRef internTestResource(RenderFramePacket& packet, FrameResourceKind kind,
                                                  u64 bindingKey)
{
    auto result = packet.intern(
        FrameResourceDescriptor{.kind = kind, .deviceBindingKey = bindingKey},
        FramePin{FramePinKind::Custom, bindingKey, nullptr, nullptr});
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : FrameResourceRef{};
}

[[nodiscard]] RenderSurfaceState activeSurface() noexcept
{
    return RenderSurfaceState{
        .surface = {.owner = 1U, .index = 0U, .generation = 1U},
        .framebufferExtent = {640U, 480U},
        .contentScale = {1.0F, 1.0F},
        .sourceMetricsRevision = 1U,
        .surfaceRevision = 1U,
        .availability = RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] RenderFrame frameWithSurface() noexcept
{
    RenderFrame frame{};
    frame.primaryWindowSurface = activeSurface();
    return frame;
}

TEST(RenderPassSchedulerTest, ClearOnlyOwnsBothAttachmentsWhenThereIsNoContent)
{
    const auto schedule = buildRenderPassSchedule(frameWithSurface());
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    ASSERT_EQ(schedule->passes().size(), 1U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPassKind::Clear);
    EXPECT_TRUE(schedule->passes()[0].clearColor);
    EXPECT_TRUE(schedule->passes()[0].clearDepth);
}

TEST(RenderPassSchedulerTest, ContentOrderIsShadowThenOpaqueThenSpriteThenUi)
{
    auto frame = frameWithSurface();
    RenderFramePacket resources;
    ASSERT_TRUE(resources.beginFrame(0));
    auto sceneBuilderResult = RenderSceneBuilder::Create(RenderSceneCapacity{
        .spriteCapacity = 1, .mesh3DItemCapacity = 1, .mesh3DBatchCapacity = 1});
    ASSERT_TRUE(sceneBuilderResult.has_value()) << sceneBuilderResult.error().message;
    RenderSceneBuilder sceneBuilder = std::move(*sceneBuilderResult);
    ASSERT_TRUE(sceneBuilder.beginFrame(RenderSceneFrameParameters{.primarySurfaceAspectRatio = 16.0F / 9.0F}));
    ASSERT_TRUE(sceneBuilder.writer().setCamera2D(RenderCamera2DInput{
        .stableCameraKey = 1, .worldWidth = 10.0F, .worldHeight = 10.0F}));
    ASSERT_TRUE(sceneBuilder.writer().setPerspectiveCamera(RenderPerspectiveCameraInput{
        .stableCameraKey = 2, .nearPlaneMeters = 0.1F, .farPlaneMeters = 100.0F}));
    ASSERT_TRUE(sceneBuilder.writer().addSprite2D(RenderSprite2DInput{
        .texture = internTestResource(resources, FrameResourceKind::Texture2D, 1),
        .stableEntityKey = 1,
        .widthMeters = 1.0F,
        .heightMeters = 1.0F,
    }));
    ASSERT_TRUE(sceneBuilder.writer().addMesh3D(RenderMesh3DInput{
        .mesh = internTestResource(resources, FrameResourceKind::Mesh3DGeometry, 2),
        .material = internTestResource(resources, FrameResourceKind::Mesh3DMaterial, 3),
        .stableEntityKey = 2,
        .worldTransform = RenderTransform3DInput{.pose = RenderPose3DInput{.positionZ = -2.0F}},
        .localBounds = RenderBoundingSphereInput{.radius = 0.5F},
    }));
    const std::array directionalLights{
        Mesh3DDirectionalLight{},
    };
    const Mesh3DCascadedDirectionalShadow cascadedShadow{};
    ASSERT_TRUE(sceneBuilder.writer().setMesh3DLighting(Mesh3DLightingDesc{
        .directionalLights = directionalLights,
        .cascadedDirectionalShadow = cascadedShadow,
    }));
    auto scene = sceneBuilder.commit();
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    frame.primaryWorldScene = *scene;

    auto displayListBuilderResult = UIDisplayListBuilder::Create(
        UIDisplayListCapacity{.commandCount = 1, .clipCount = 0, .batchCount = 1});
    ASSERT_TRUE(displayListBuilderResult.has_value()) << displayListBuilderResult.error().message;
    UIDisplayListBuilder displayListBuilder = std::move(*displayListBuilderResult);
    ASSERT_TRUE(displayListBuilder.beginFrame());
    ASSERT_TRUE(displayListBuilder.addSolidQuad(UISolidQuadInput{
        .paintOrdinal = 1,
        .bounds = UIPixelRect{.width = 10, .height = 10},
        .color = UIPremultipliedRgba8{.red = 255, .green = 255, .blue = 255, .alpha = 255},
    }));
    auto displayList = displayListBuilder.commit();
    ASSERT_TRUE(displayList.has_value()) << displayList.error().message;
    frame.primaryWindowUIDisplayList = *displayList;

    const auto schedule = buildRenderPassSchedule(frame);
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    ASSERT_EQ(schedule->passes().size(), 7U);
    for (u32 cascadeIndex = 0; cascadeIndex < Mesh3DCascadedDirectionalShadow::CascadeCount;
         ++cascadeIndex)
    {
        const RenderPassPlan& pass = schedule->passes()[cascadeIndex];
        EXPECT_EQ(pass.kind, RenderPassKind::CascadedDirectionalShadowDepth);
        EXPECT_EQ(pass.resource, RenderPassResource::DirectionalShadowAtlas);
        EXPECT_EQ(pass.cascadeIndex, cascadeIndex);
        EXPECT_FALSE(pass.clearColor);
        EXPECT_TRUE(pass.clearDepth);
    }
    EXPECT_EQ(schedule->passes()[4].kind, RenderPassKind::Opaque3D);
    EXPECT_EQ(schedule->passes()[4].resource, RenderPassResource::PrimarySurface);
    EXPECT_EQ(schedule->passes()[5].kind, RenderPassKind::Sprite2D);
    EXPECT_EQ(schedule->passes()[6].kind, RenderPassKind::UI);
    EXPECT_TRUE(schedule->passes()[4].clearColor);
    EXPECT_TRUE(schedule->passes()[4].clearDepth);
    EXPECT_FALSE(schedule->passes()[5].clearColor);
    EXPECT_FALSE(schedule->passes()[5].clearDepth);
    EXPECT_FALSE(schedule->passes()[6].clearColor);
    EXPECT_FALSE(schedule->passes()[6].clearDepth);
}

TEST(RenderPassSchedulerTest, PartialFirstContentViewportGetsFullSurfaceClearPass)
{
    auto frame = frameWithSurface();
    RenderFramePacket resources;
    ASSERT_TRUE(resources.beginFrame(0));
    auto sceneBuilderResult = RenderSceneBuilder::Create(RenderSceneCapacity{
        .spriteCapacity = 1, .mesh3DItemCapacity = 1, .mesh3DBatchCapacity = 1});
    ASSERT_TRUE(sceneBuilderResult.has_value()) << sceneBuilderResult.error().message;
    RenderSceneBuilder sceneBuilder = std::move(*sceneBuilderResult);
    ASSERT_TRUE(sceneBuilder.beginFrame());
    ASSERT_TRUE(sceneBuilder.writer().setCamera2D(RenderCamera2DInput{
        .stableCameraKey = 1,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .normalizedViewport = RenderNormalizedViewport{.x = 0.25F, .width = 0.5F},
    }));
    ASSERT_TRUE(sceneBuilder.writer().addSprite2D(RenderSprite2DInput{
        .texture = internTestResource(resources, FrameResourceKind::Texture2D, 1),
        .stableEntityKey = 1,
        .widthMeters = 1.0F,
        .heightMeters = 1.0F,
    }));
    auto scene = sceneBuilder.commit();
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    frame.primaryWorldScene = *scene;

    const auto schedule = buildRenderPassSchedule(frame);
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    ASSERT_EQ(schedule->passes().size(), 2U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPassKind::Clear);
    EXPECT_TRUE(schedule->passes()[0].clearColor);
    EXPECT_TRUE(schedule->passes()[0].clearDepth);
    EXPECT_EQ(schedule->passes()[1].kind, RenderPassKind::Sprite2D);
    EXPECT_FALSE(schedule->passes()[1].clearColor);
    EXPECT_FALSE(schedule->passes()[1].clearDepth);
}

TEST(RenderPassSchedulerTest, PartialOpaqueViewportClearsPrimarySurfaceBeforeShadowPass)
{
    auto frame = frameWithSurface();
    RenderFramePacket resources;
    ASSERT_TRUE(resources.beginFrame(0));
    auto sceneBuilderResult = RenderSceneBuilder::Create(RenderSceneCapacity{
        .mesh3DItemCapacity = 1, .mesh3DBatchCapacity = 1});
    ASSERT_TRUE(sceneBuilderResult.has_value()) << sceneBuilderResult.error().message;
    RenderSceneBuilder sceneBuilder = std::move(*sceneBuilderResult);
    ASSERT_TRUE(sceneBuilder.beginFrame(RenderSceneFrameParameters{.primarySurfaceAspectRatio = 4.0F / 3.0F}));
    ASSERT_TRUE(sceneBuilder.writer().setPerspectiveCamera(RenderPerspectiveCameraInput{
        .stableCameraKey = 1,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 100.0F,
        .normalizedViewport = RenderNormalizedViewport{.x = 0.25F, .width = 0.5F},
    }));
    ASSERT_TRUE(sceneBuilder.writer().addMesh3D(RenderMesh3DInput{
        .mesh = internTestResource(resources, FrameResourceKind::Mesh3DGeometry, 1),
        .material = internTestResource(resources, FrameResourceKind::Mesh3DMaterial, 2),
        .stableEntityKey = 1,
        .worldTransform = RenderTransform3DInput{.pose = RenderPose3DInput{.positionZ = -2.0F}},
        .localBounds = RenderBoundingSphereInput{.radius = 0.5F},
    }));
    const std::array directionalLights{
        Mesh3DDirectionalLight{},
    };
    const Mesh3DCascadedDirectionalShadow cascadedShadow{};
    ASSERT_TRUE(sceneBuilder.writer().setMesh3DLighting(Mesh3DLightingDesc{
        .directionalLights = directionalLights,
        .cascadedDirectionalShadow = cascadedShadow,
    }));
    auto scene = sceneBuilder.commit();
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    frame.primaryWorldScene = *scene;

    const auto schedule = buildRenderPassSchedule(frame);
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    ASSERT_EQ(schedule->passes().size(), 6U);
    EXPECT_EQ(schedule->passes()[0].kind, RenderPassKind::Clear);
    EXPECT_EQ(schedule->passes()[0].resource, RenderPassResource::PrimarySurface);
    EXPECT_TRUE(schedule->passes()[0].clearColor);
    EXPECT_TRUE(schedule->passes()[0].clearDepth);
    for (u32 cascadeIndex = 0; cascadeIndex < Mesh3DCascadedDirectionalShadow::CascadeCount;
         ++cascadeIndex)
    {
        const RenderPassPlan& pass = schedule->passes()[1U + cascadeIndex];
        EXPECT_EQ(pass.kind, RenderPassKind::CascadedDirectionalShadowDepth);
        EXPECT_EQ(pass.resource, RenderPassResource::DirectionalShadowAtlas);
        EXPECT_EQ(pass.cascadeIndex, cascadeIndex);
        EXPECT_FALSE(pass.clearColor);
        EXPECT_TRUE(pass.clearDepth);
    }
    EXPECT_EQ(schedule->passes()[5].kind, RenderPassKind::Opaque3D);
    EXPECT_EQ(schedule->passes()[5].resource, RenderPassResource::PrimarySurface);
    EXPECT_FALSE(schedule->passes()[5].clearColor);
    EXPECT_FALSE(schedule->passes()[5].clearDepth);
}

TEST(RenderPassSchedulerTest, SuspendedSurfaceSkipsAllPasses)
{
    auto frame = frameWithSurface();
    frame.primaryWindowSurface->availability = RenderSurfaceAvailability::Suspended;
    const auto schedule = buildRenderPassSchedule(frame);
    ASSERT_TRUE(schedule.has_value()) << schedule.error().message;
    EXPECT_TRUE(schedule->empty());
}

TEST(RenderPassSchedulerTest, MissingSurfaceFailsClosed)
{
    const auto schedule = buildRenderPassSchedule(RenderFrame{});
    ASSERT_FALSE(schedule.has_value());
    EXPECT_EQ(schedule.error().code, RenderErrorCode::InvalidSurfaceState);
}

} // namespace
} // namespace Tina::Render
