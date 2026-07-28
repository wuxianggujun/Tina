#include "BgfxOpaque3DGeometry.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

class TestFrameResources final {
  public:
    TestFrameResources()
    {
        EXPECT_TRUE(packet_.beginFrame(0).has_value());
    }

    [[nodiscard]] FrameResourceRef intern(FrameResourceKind kind, u64 bindingKey)
    {
        FramePin pin{
            FramePinKind::Custom,
            bindingKey,
            &releaseCount_,
            [](void* userData) noexcept {
                ++(*static_cast<u32*>(userData));
            },
        };
        auto result = packet_.intern(
            FrameResourceDescriptor{.kind = kind, .deviceBindingKey = bindingKey},
            std::move(pin));
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? *result : FrameResourceRef{};
    }

    [[nodiscard]] FrameResourceTableView view() const noexcept
    {
        return packet_.resourceTableView();
    }

    void abandon()
    {
        EXPECT_TRUE(packet_.abandon().has_value());
    }

  private:
    u32 releaseCount_ = 0;
    RenderFramePacket packet_{};
};

[[nodiscard]] RenderSceneBuilder makeBuilder()
{
    RenderSceneCapacity capacity{};
    capacity.spriteCapacity = 4;
    capacity.mesh3DItemCapacity = 8;
    capacity.mesh3DBatchCapacity = 8;
    auto result = RenderSceneBuilder::Create(capacity);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return std::move(*result);
}

[[nodiscard]] RenderPerspectiveCameraInput camera() noexcept
{
    return RenderPerspectiveCameraInput{
        .stableCameraKey = 1,
        .worldPose = {.positionZ = 6.0F},
        .verticalFovDegrees = 60.0F,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 100.0F,
    };
}

[[nodiscard]] RenderMesh3DInput mesh(TestFrameResources& resources, u32 meshKey,
                                     u64 stableEntityKey, float x,
                                     RenderLinearColor color = {},
                                     u32 materialKey = Opaque3DmaterialKey,
                                     u32 submeshIndex = Opaque3DFixtureSubmeshIndex) noexcept
{
    return RenderMesh3DInput{
        .mesh = resources.intern(FrameResourceKind::Mesh3DGeometry, meshKey),
        .material = resources.intern(FrameResourceKind::Mesh3DMaterial, materialKey),
        .submeshIndex = submeshIndex,
        .stableEntityKey = stableEntityKey,
        .worldTransform = {
            .pose = {.positionX = x},
        },
        .localBounds = {.radius = 1.0F},
        .baseColorFactor = color,
    };
}

[[nodiscard]] Core::Result<RenderSceneView>
committedScene(RenderSceneBuilder& builder, TestFrameResources& resources,
               u32 meshKey = Opaque3DmeshKey,
               u32 materialKey = Opaque3DmaterialKey,
               u32 submeshIndex = Opaque3DFixtureSubmeshIndex)
{
    if (auto status = builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto writer = builder.writer();
    if (auto status = writer.setPerspectiveCamera(camera()); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addMesh3D(mesh(resources, meshKey, 10, -1.0F,
                                            {.red = 0.25F, .green = 0.5F, .blue = 0.75F},
                                            materialKey, submeshIndex));
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addMesh3D(mesh(resources, meshKey, 20, 1.0F,
                                            {.red = 0.8F, .green = 0.2F, .blue = 0.1F},
                                            materialKey, submeshIndex));
        !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return builder.commit();
}

[[nodiscard]] Core::Result<RenderSceneView>
committedSingleMeshScene(RenderSceneBuilder& builder,
                         FrameResourceRef meshResource,
                         FrameResourceRef materialResource)
{
    if (auto status = builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto writer = builder.writer();
    if (auto status = writer.setPerspectiveCamera(camera()); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (auto status = writer.addMesh3D(RenderMesh3DInput{
            .mesh = meshResource,
            .material = materialResource,
            .stableEntityKey = 1,
            .localBounds = {.radius = 1.0F},
        }); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    return builder.commit();
}

TEST(BgfxOpaque3DGeometryTest, CanonicalCubeUsesP3N3UV2AndOutwardWinding)
{
    EXPECT_TRUE(std::is_standard_layout_v<BgfxOpaque3DVertex>);
    EXPECT_EQ(sizeof(BgfxOpaque3DVertex), 32U);
    EXPECT_EQ(offsetof(BgfxOpaque3DVertex, positionX), 0U);
    EXPECT_EQ(offsetof(BgfxOpaque3DVertex, normalX), 12U);
    EXPECT_EQ(offsetof(BgfxOpaque3DVertex, textureU), 24U);

    const auto vertices = canonicalCubeVertices();
    const auto indices = canonicalCubeIndices();
    ASSERT_EQ(vertices.size(), 24U);
    ASSERT_EQ(indices.size(), 36U);

    for (usize triangle = 0; triangle < indices.size(); triangle += 3U)
    {
        ASSERT_LT(indices[triangle], vertices.size());
        ASSERT_LT(indices[triangle + 1U], vertices.size());
        ASSERT_LT(indices[triangle + 2U], vertices.size());
        const BgfxOpaque3DVertex& a = vertices[indices[triangle]];
        const BgfxOpaque3DVertex& b = vertices[indices[triangle + 1U]];
        const BgfxOpaque3DVertex& c = vertices[indices[triangle + 2U]];
        const float abX = b.positionX - a.positionX;
        const float abY = b.positionY - a.positionY;
        const float abZ = b.positionZ - a.positionZ;
        const float acX = c.positionX - a.positionX;
        const float acY = c.positionY - a.positionY;
        const float acZ = c.positionZ - a.positionZ;
        const float crossX = abY * acZ - abZ * acY;
        const float crossY = abZ * acX - abX * acZ;
        const float crossZ = abX * acY - abY * acX;
        EXPECT_GT(crossX * a.normalX + crossY * a.normalY + crossZ * a.normalZ, 0.0F);
        EXPECT_NEAR(a.normalX * a.normalX + a.normalY * a.normalY + a.normalZ * a.normalZ,
                    1.0F, 1.0e-6F);
    }
}

TEST(BgfxOpaque3DGeometryTest, InstanceDataLayoutMatchesFiveShaderVec4Attributes)
{
    EXPECT_TRUE(std::is_standard_layout_v<BgfxOpaque3DInstanceData>);
    EXPECT_EQ(sizeof(BgfxOpaque3DInstanceData), 80U);
    EXPECT_EQ(offsetof(BgfxOpaque3DInstanceData, columnMajorWorldTransform), 0U);
    EXPECT_EQ(offsetof(BgfxOpaque3DInstanceData, baseColorFactor), 64U);
}

TEST(BgfxOpaque3DGeometryTest, EmptySceneNeedsNoInstancesOrBatches)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame());
    auto scene = builder.commit();
    ASSERT_TRUE(scene.has_value());

    auto requirements = checkedOpaque3DFrame(*scene, resources.view());
    ASSERT_TRUE(requirements.has_value());
    EXPECT_EQ(requirements->instanceCount, 0U);
    EXPECT_EQ(requirements->batchCount, 0U);
}

TEST(BgfxOpaque3DGeometryTest, ValidFixtureWritesWorldTransformsAndColors)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto sceneResult = committedScene(builder, resources);
    ASSERT_TRUE(sceneResult.has_value());
    const RenderSceneView scene = *sceneResult;
    auto requirements = checkedOpaque3DFrame(scene, resources.view());
    ASSERT_TRUE(requirements.has_value());
    EXPECT_EQ(requirements->instanceCount, 2U);
    EXPECT_EQ(requirements->batchCount, 1U);

    std::array<BgfxOpaque3DInstanceData, 2> instances{};
    auto written = writeOpaque3DInstanceData(scene, resources.view(), instances);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->instanceCount, 2U);
    EXPECT_EQ(written->batchCount, 1U);
    EXPECT_EQ(instances[0].columnMajorWorldTransform,
              scene.meshes3D()[0].columnMajorWorldTransform);
    EXPECT_EQ(instances[1].columnMajorWorldTransform,
              scene.meshes3D()[1].columnMajorWorldTransform);
    EXPECT_EQ(instances[0].baseColorFactor,
              (std::array<float, 4>{0.25F, 0.5F, 0.75F, 1.0F}));
    EXPECT_EQ(instances[1].baseColorFactor,
              (std::array<float, 4>{0.8F, 0.2F, 0.1F, 1.0F}));
}

TEST(BgfxOpaque3DGeometryTest, RejectsCrossPacketMeshResources)
{
    TestFrameResources firstResources;
    TestFrameResources secondResources;
    RenderSceneBuilder builder = makeBuilder();
    auto scene = committedSingleMeshScene(
        builder,
        firstResources.intern(FrameResourceKind::Mesh3DGeometry, 11),
        secondResources.intern(FrameResourceKind::Mesh3DMaterial, 22));
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto requirements = checkedOpaque3DFrame(*scene, firstResources.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidFrameResource);
}

TEST(BgfxOpaque3DGeometryTest, RejectsWrongKindMeshResources)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto scene = committedSingleMeshScene(
        builder,
        resources.intern(FrameResourceKind::Mesh3DMaterial, 11),
        resources.intern(FrameResourceKind::Mesh3DGeometry, 22));
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    auto requirements = checkedOpaque3DFrame(*scene, resources.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidFrameResource);
}

TEST(BgfxOpaque3DGeometryTest, RejectsStaleMeshResources)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto scene = committedSingleMeshScene(
        builder,
        resources.intern(FrameResourceKind::Mesh3DGeometry, 11),
        resources.intern(FrameResourceKind::Mesh3DMaterial, 22));
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    const FrameResourceTableView staleResources = resources.view();
    resources.abandon();

    auto requirements = checkedOpaque3DFrame(*scene, staleResources);
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidFrameResource);
}

TEST(BgfxOpaque3DGeometryTest, RejectsBindingKeyOutsideMesh3DDeviceRange)
{
    constexpr u64 OversizedBindingKey =
        static_cast<u64>((std::numeric_limits<u32>::max)()) + 1U;

    for (const bool oversizedGeometry : {false, true})
    {
        TestFrameResources resources;
        RenderSceneBuilder builder = makeBuilder();
        auto scene = committedSingleMeshScene(
            builder,
            resources.intern(FrameResourceKind::Mesh3DGeometry,
                             oversizedGeometry ? OversizedBindingKey : 11U),
            resources.intern(FrameResourceKind::Mesh3DMaterial,
                             oversizedGeometry ? 22U : OversizedBindingKey));
        ASSERT_TRUE(scene.has_value()) << scene.error().message;

        auto status = validateOpaque3DFrameResources(*scene, resources.view());
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, RenderErrorCode::InvalidFrameResource);
    }
}

// Product path: a non-fixture mesh binding is valid at geometry validation;
// submit resolves the packet-local ref and binds the uploaded GPU mesh.
TEST(BgfxOpaque3DGeometryTest, AcceptsNonFixtureMeshBindingAtGeometryStage)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto sceneResult = committedScene(builder, resources, Opaque3DmeshKey + 1U);
    ASSERT_TRUE(sceneResult.has_value());
    const RenderSceneView scene = *sceneResult;
    auto requirements = checkedOpaque3DFrame(scene, resources.view());
    ASSERT_TRUE(requirements.has_value()) << (requirements ? "" : requirements.error().message);
    EXPECT_EQ(requirements->instanceCount, 2U);
    EXPECT_EQ(requirements->batchCount, 1U);
}

// M11-E5: a non-fixture material binding is valid; textures bind through its
// resolved device binding key at submit.
TEST(BgfxOpaque3DGeometryTest, AcceptsNonFixtureMaterialBindingAtGeometryStage)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto sceneResult = committedScene(builder, resources, Opaque3DmeshKey,
                                      Opaque3DmaterialKey + 1U);
    ASSERT_TRUE(sceneResult.has_value());

    auto requirements = checkedOpaque3DFrame(*sceneResult, resources.view());
    ASSERT_TRUE(requirements.has_value()) << (requirements ? "" : requirements.error().message);
    EXPECT_EQ(requirements->instanceCount, 2U);
}

TEST(BgfxOpaque3DGeometryTest, RejectsUnsupportedSubmeshIndexExplicitly)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto sceneResult = committedScene(builder, resources, Opaque3DmeshKey,
                                      Opaque3DmaterialKey,
                                      Opaque3DFixtureSubmeshIndex + 1U);
    ASSERT_TRUE(sceneResult.has_value());

    auto requirements = checkedOpaque3DFrame(*sceneResult, resources.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, Core::CoreErrorCode::Unsupported);
}

TEST(BgfxOpaque3DGeometryTest, InsufficientOutputDoesNotPublishPartialInstanceData)
{
    TestFrameResources resources;
    RenderSceneBuilder builder = makeBuilder();
    auto sceneResult = committedScene(builder, resources);
    ASSERT_TRUE(sceneResult.has_value());
    const RenderSceneView scene = *sceneResult;
    std::array<BgfxOpaque3DInstanceData, 1> instances{};
    instances[0].baseColorFactor = {9.0F, 8.0F, 7.0F, 6.0F};
    const BgfxOpaque3DInstanceData before = instances[0];

    auto result = writeOpaque3DInstanceData(scene, resources.view(), instances);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Core::CoreErrorCode::CapacityExceeded);
    EXPECT_EQ(instances[0].columnMajorWorldTransform, before.columnMajorWorldTransform);
    EXPECT_EQ(instances[0].baseColorFactor, before.baseColorFactor);
}

} // namespace
} // namespace Tina::Render::Bgfx
