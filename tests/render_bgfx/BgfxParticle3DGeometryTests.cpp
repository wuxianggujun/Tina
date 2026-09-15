#include "BgfxParticle3DGeometry.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

void countRelease(void* userData) noexcept
{
    ++(*static_cast<u32*>(userData));
}

class FrameResourceScope final {
public:
    FrameResourceScope() { EXPECT_TRUE(packet_.beginFrame(0)); }

    [[nodiscard]] FrameResourceRef texture(u64 deviceBindingKey)
    {
        FramePin pin{FramePinKind::Custom, deviceBindingKey, &releaseCount_, &countRelease};
        auto result = packet_.intern(
            FrameResourceDescriptor{
                .kind = FrameResourceKind::Texture2D,
                .deviceBindingKey = deviceBindingKey,
            },
            std::move(pin));
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        return result ? *result : FrameResourceRef{};
    }

    [[nodiscard]] FrameResourceTableView view() const noexcept { return packet_.resourceTableView(); }

private:
    u32 releaseCount_ = 0;
    RenderFramePacket packet_{};
};

[[nodiscard]] RenderSceneBuilder makeBuilder(u32 particleCapacity = 8)
{
    RenderSceneCapacity capacity{};
    capacity.spriteCapacity = 1;
    capacity.mesh3DItemCapacity = 4;
    capacity.mesh3DBatchCapacity = 4;
    capacity.particle3DItemCapacity = particleCapacity;
    auto result = RenderSceneBuilder::Create(capacity);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return std::move(*result);
}

// Camera at +Z looking down -Z with +Y up. right = forward x up = (-1,0,0)*... is
// computed by the production code; the test asserts the resulting basis rather
// than assuming a sign.
[[nodiscard]] RenderPerspectiveCameraInput camera(float z = 20.0F) noexcept
{
    return RenderPerspectiveCameraInput{
        .stableCameraKey = 3,
        .worldPose = {.positionZ = z},
        .verticalFovDegrees = 90.0F,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 500.0F,
    };
}

[[nodiscard]] RenderParticle3DInput particle(FrameResourceRef texture, u64 key, float x, float y,
                                            float z, float width = 2.0F, float height = 2.0F) noexcept
{
    return RenderParticle3DInput{
        .texture = texture,
        .stableParticleKey = key,
        .worldX = x,
        .worldY = y,
        .worldZ = z,
        .widthMeters = width,
        .heightMeters = height,
    };
}

TEST(BgfxParticle3DGeometryTests, BillboardBasisIsOrthonormalAndFacesTheCamera)
{
    RenderPerspectiveCamera view{};
    view.forwardX = 0.0F;
    view.forwardY = 0.0F;
    view.forwardZ = -1.0F;
    view.upX = 0.0F;
    view.upY = 1.0F;
    view.upZ = 0.0F;

    auto basis = particle3DBillboardBasis(view);
    ASSERT_TRUE(basis.has_value()) << (basis ? "" : basis.error().message);
    const float rightLength = std::sqrt(basis->rightX * basis->rightX + basis->rightY * basis->rightY +
                                        basis->rightZ * basis->rightZ);
    const float upLength =
        std::sqrt(basis->upX * basis->upX + basis->upY * basis->upY + basis->upZ * basis->upZ);
    EXPECT_NEAR(rightLength, 1.0F, 1e-5F);
    EXPECT_NEAR(upLength, 1.0F, 1e-5F);
    // Both axes perpendicular to the view direction is what "faces the camera" means.
    EXPECT_NEAR(basis->rightZ, 0.0F, 1e-5F);
    EXPECT_NEAR(basis->upZ, 0.0F, 1e-5F);
    EXPECT_NEAR(basis->rightX * basis->upX + basis->rightY * basis->upY + basis->rightZ * basis->upZ,
                0.0F, 1e-5F);
}

TEST(BgfxParticle3DGeometryTests, BillboardBasisRejectsParallelForwardAndUp)
{
    RenderPerspectiveCamera view{};
    view.forwardX = 0.0F;
    view.forwardY = 1.0F;
    view.forwardZ = 0.0F;
    view.upX = 0.0F;
    view.upY = 1.0F;
    view.upZ = 0.0F;

    auto basis = particle3DBillboardBasis(view);
    ASSERT_FALSE(basis.has_value());
    EXPECT_EQ(basis.error().code, RenderErrorCode::InvalidRenderSceneInput);
}

TEST(BgfxParticle3DGeometryTests, BillboardBasisOrthonormalisesANonPerpendicularUp)
{
    RenderPerspectiveCamera view{};
    view.forwardX = 0.0F;
    view.forwardY = 0.0F;
    view.forwardZ = -1.0F;
    // Deliberately tilted into the view direction.
    view.upX = 0.0F;
    view.upY = 1.0F;
    view.upZ = -0.5F;

    auto basis = particle3DBillboardBasis(view);
    ASSERT_TRUE(basis.has_value()) << (basis ? "" : basis.error().message);
    // A sheared basis would leave a non-zero dot product here, which would make the
    // quad a parallelogram instead of a square.
    EXPECT_NEAR(basis->rightX * basis->upX + basis->rightY * basis->upY + basis->rightZ * basis->upZ,
                0.0F, 1e-5F);
    EXPECT_NEAR(basis->upZ, 0.0F, 1e-5F);
}

TEST(BgfxParticle3DGeometryTests, EmptySceneRequiresNothing)
{
    FrameResourceScope resources{};
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    auto requirements = checkedParticle3DFrame(*committed, resources.view());
    ASSERT_TRUE(requirements.has_value()) << (requirements ? "" : requirements.error().message);
    EXPECT_EQ(requirements->particleCount, 0U);
    EXPECT_EQ(requirements->vertexCount, 0U);
    EXPECT_EQ(requirements->indexCount, 0U);
    EXPECT_EQ(requirements->batchCount, 0U);
}

TEST(BgfxParticle3DGeometryTests, RequirementsCountFourVerticesAndSixIndicesPerParticle)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 1, 0.0F, 0.0F, 0.0F)).has_value());
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 2, 1.0F, 0.0F, 0.0F)).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    auto requirements = checkedParticle3DFrame(*committed, resources.view());
    ASSERT_TRUE(requirements.has_value()) << (requirements ? "" : requirements.error().message);
    EXPECT_EQ(requirements->particleCount, 2U);
    EXPECT_EQ(requirements->vertexCount, 8U);
    EXPECT_EQ(requirements->indexCount, 12U);
    // Same texture and blend mode, adjacent in the draw list: one batch.
    EXPECT_EQ(requirements->batchCount, 1U);
}

TEST(BgfxParticle3DGeometryTests, DistinctTexturesAndBlendModesEachBreakTheBatch)
{
    FrameResourceScope resources{};
    const FrameResourceRef first = resources.texture(11);
    const FrameResourceRef second = resources.texture(12);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    // Placed back-to-front so the committed draw order matches the add order.
    auto farParticle = particle(first, 1, 0.0F, 0.0F, -10.0F);
    auto middle = particle(second, 2, 0.0F, 0.0F, -5.0F);
    auto near = particle(second, 3, 0.0F, 0.0F, 0.0F);
    near.blendMode = Core::BlendMode::Additive;
    ASSERT_TRUE(writer.addParticle3D(farParticle).has_value());
    ASSERT_TRUE(writer.addParticle3D(middle).has_value());
    ASSERT_TRUE(writer.addParticle3D(near).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    auto requirements = checkedParticle3DFrame(*committed, resources.view());
    ASSERT_TRUE(requirements.has_value()) << (requirements ? "" : requirements.error().message);
    EXPECT_EQ(requirements->particleCount, 3U);
    EXPECT_EQ(requirements->batchCount, 3U);
}

// The billboard basis is derived from the committed camera, so a camera-less
// particle scene must never reach the backend. RenderScene is what guarantees it:
// commit fails, which is why checkedParticle3DFrame's own camera guard is a
// dereference precondition rather than a reachable error path.
TEST(BgfxParticle3DGeometryTests, CameraLessParticleSceneNeverCommits)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    {
        auto writer = builder.writer();
        ASSERT_TRUE(writer.addParticle3D(particle(texture, 1, 0.0F, 0.0F, 0.0F)).has_value());
    }
    auto committed = builder.commit();
    ASSERT_FALSE(committed.has_value());
    EXPECT_EQ(committed.error().code, RenderErrorCode::RenderSceneMissingCamera);

    // A default-constructed view carries neither camera nor particles, so the
    // backend answers "nothing to draw" instead of touching the empty optional.
    auto requirements = checkedParticle3DFrame(RenderSceneView{}, resources.view());
    ASSERT_TRUE(requirements.has_value()) << (requirements ? "" : requirements.error().message);
    EXPECT_EQ(requirements->particleCount, 0U);
}

TEST(BgfxParticle3DGeometryTests, MissingTextureRefIsRejected)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 1, 0.0F, 0.0F, 0.0F)).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    // A fresh, empty table cannot resolve the ref the scene committed.
    FrameResourceScope other{};
    auto requirements = checkedParticle3DFrame(*committed, other.view());
    ASSERT_FALSE(requirements.has_value());
    EXPECT_EQ(requirements.error().code, RenderErrorCode::InvalidRenderSceneInput);
}

TEST(BgfxParticle3DGeometryTests, WriteProducesACameraFacingQuadWithTwoTriangles)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 1, 0.0F, 0.0F, 0.0F, 2.0F, 4.0F)).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    std::array<BgfxParticle3DVertex, 4> vertices{};
    std::array<u32, 6> indices{};
    auto written = writeParticle3DGeometry(*committed, resources.view(), vertices, indices);
    ASSERT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    EXPECT_EQ(written->particleCount, 1U);

    // The quad must be flat in the view plane: with forward -Z every corner keeps z = 0.
    for (const BgfxParticle3DVertex& vertex : vertices)
    {
        EXPECT_NEAR(vertex.positionZ, 0.0F, 1e-5F);
    }
    // 2m wide, 4m tall, centred at the origin.
    EXPECT_NEAR(std::abs(vertices[0].positionX), 1.0F, 1e-5F);
    EXPECT_NEAR(std::abs(vertices[0].positionY), 2.0F, 1e-5F);
    // Two triangles sharing the 0-2 diagonal.
    EXPECT_EQ(indices[0], 0U);
    EXPECT_EQ(indices[1], 1U);
    EXPECT_EQ(indices[2], 2U);
    EXPECT_EQ(indices[3], 0U);
    EXPECT_EQ(indices[4], 2U);
    EXPECT_EQ(indices[5], 3U);
}

TEST(BgfxParticle3DGeometryTests, RotationSpinsTheQuadInsideTheViewPlane)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    auto spun = particle(texture, 1, 0.0F, 0.0F, 0.0F, 2.0F, 2.0F);
    spun.rotationRadians = static_cast<float>(std::numbers::pi) * 0.5F;
    ASSERT_TRUE(writer.addParticle3D(spun).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    std::array<BgfxParticle3DVertex, 4> vertices{};
    std::array<u32, 6> indices{};
    ASSERT_TRUE(writeParticle3DGeometry(*committed, resources.view(), vertices, indices).has_value());

    // A quarter turn must stay in the view plane, not tip out of it.
    for (const BgfxParticle3DVertex& vertex : vertices)
    {
        EXPECT_NEAR(vertex.positionZ, 0.0F, 1e-5F);
        EXPECT_NEAR(std::sqrt(vertex.positionX * vertex.positionX + vertex.positionY * vertex.positionY),
                    std::sqrt(2.0F), 1e-4F);
    }
}

TEST(BgfxParticle3DGeometryTests, WriteEmitsGeometryInDrawOrderNotArrayOrder)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    // Added near-to-far; the transparent domain must draw them far-to-near, so the
    // written vertex blocks have to come out in the opposite order to the adds.
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 1, 1.0F, 0.0F, 0.0F)).has_value());
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 2, 2.0F, 0.0F, -30.0F)).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);
    ASSERT_EQ(committed->particles3D().size(), 2U);

    std::array<BgfxParticle3DVertex, 8> vertices{};
    std::array<u32, 12> indices{};
    ASSERT_TRUE(writeParticle3DGeometry(*committed, resources.view(), vertices, indices).has_value());

    // Slot 0 belongs to whichever particle the draw list puts first.
    const auto& draws = committed->transparent3DDraws();
    ASSERT_EQ(draws.size(), 2U);
    const RenderParticle3DItem& firstDrawn = committed->particles3D()[draws[0].itemIndex];
    const float slotZeroCenterX =
        (vertices[0].positionX + vertices[1].positionX + vertices[2].positionX + vertices[3].positionX) *
        0.25F;
    EXPECT_NEAR(slotZeroCenterX, firstDrawn.worldX, 1e-4F);
    // Indices are absolute, so the second block must reference vertices 4..7.
    EXPECT_EQ(indices[6], 4U);
}

TEST(BgfxParticle3DGeometryTests, WriteRejectsUndersizedOutputBuffers)
{
    FrameResourceScope resources{};
    const FrameResourceRef texture = resources.texture(11);
    auto builder = makeBuilder();
    ASSERT_TRUE(builder.beginFrame({.primarySurfaceAspectRatio = 16.0F / 9.0F}).has_value());
    auto writer = builder.writer();
    ASSERT_TRUE(writer.setPerspectiveCamera(camera()).has_value());
    ASSERT_TRUE(writer.addParticle3D(particle(texture, 1, 0.0F, 0.0F, 0.0F)).has_value());
    auto committed = builder.commit();
    ASSERT_TRUE(committed.has_value()) << (committed ? "" : committed.error().message);

    std::array<BgfxParticle3DVertex, 3> vertices{};
    std::array<u32, 6> indices{};
    auto written = writeParticle3DGeometry(*committed, resources.view(), vertices, indices);
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, Core::CoreErrorCode::CapacityExceeded);
}

} // namespace
} // namespace Tina::Render::Bgfx
