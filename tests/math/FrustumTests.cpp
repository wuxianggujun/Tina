#include <gtest/gtest.h>

#include <tina/math/Frustum.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-5F;
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

struct CameraFixture final {
    Math::Vec3 position{0.0F, 0.0F, 0.0F};
    Math::Vec3 forward{0.0F, 0.0F, -1.0F};
    Math::Vec3 up{0.0F, 1.0F, 0.0F};
    float verticalFovDegrees = 60.0F;
    float aspectRatio = 16.0F / 9.0F;
    float nearPlane = 0.5F;
    float farPlane = 100.0F;
};

[[nodiscard]] std::optional<Math::Frustum> buildFrustum(const CameraFixture& camera)
{
    return Math::frustumFromPerspective(
        camera.position,
        camera.forward,
        camera.up,
        Math::radians(camera.verticalFovDegrees),
        camera.aspectRatio,
        camera.nearPlane,
        camera.farPlane);
}

// The exact sphere-versus-frustum test that lived in src/scene/ExtractRenderScene.cpp before
// Tina::Math existed, reproduced here verbatim (double accumulation and comparison
// order included). Tina::Math must agree with it: the 3D product evidence numbers
// were produced by this arithmetic, so a divergence would look like a regression in
// mesh/light submission counts rather than a refactor.
[[nodiscard]] bool legacySphereIntersectsPerspectiveCamera(
    float positionX,
    float positionY,
    float positionZ,
    float influenceRadius,
    const CameraFixture& camera) noexcept
{
    const Math::Vec3 cameraPosition = camera.position;
    const Math::Vec3 forward = camera.forward;
    const Math::Vec3 up = camera.up;
    const Math::Vec3 right{
        forward.y * up.z - forward.z * up.y,
        forward.z * up.x - forward.x * up.z,
        forward.x * up.y - forward.y * up.x,
    };
    const Math::Vec3 relative = Math::Vec3{positionX, positionY, positionZ} - cameraPosition;
    const double x = static_cast<double>(Math::dot(relative, right));
    const double y = static_cast<double>(Math::dot(relative, up));
    const double depth = static_cast<double>(Math::dot(relative, forward));
    const double radius = static_cast<double>(influenceRadius);

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth) ||
        !std::isfinite(radius) || depth + radius < camera.nearPlane ||
        depth - radius > camera.farPlane) {
        return false;
    }

    const double tangentY =
        std::tan(static_cast<double>(camera.verticalFovDegrees) * std::numbers::pi / 360.0);
    const double tangentX = tangentY * static_cast<double>(camera.aspectRatio);
    const double horizontalRadiusScale = std::hypot(tangentX, 1.0);
    const double verticalRadiusScale = std::hypot(tangentY, 1.0);
    return depth * tangentX + x >= -radius * horizontalRadiusScale &&
           depth * tangentX - x >= -radius * horizontalRadiusScale &&
           depth * tangentY + y >= -radius * verticalRadiusScale &&
           depth * tangentY - y >= -radius * verticalRadiusScale;
}

} // namespace

TEST(MathFrustumTest, PlaneNormalsPointInwardSoInsidePointsArePositive)
{
    const CameraFixture camera{};
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    // A point well inside is in front of every plane.
    const Math::Vec3 inside{0.0F, 0.0F, -10.0F};
    for (const Math::Plane& plane : frustum->planes) {
        EXPECT_GT(Math::signedDistance(plane, inside), 0.0F);
        EXPECT_NEAR(Math::length(plane.normal), 1.0F, Tolerance);
    }
    EXPECT_TRUE(Math::contains(*frustum, inside));
}

TEST(MathFrustumTest, ContainsRejectsPointsOutsideEachPlane)
{
    const CameraFixture camera{};
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    // Behind the camera.
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 0.0F, 10.0F}));
    // Nearer than the near plane.
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 0.0F, -0.1F}));
    // Beyond the far plane.
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 0.0F, -200.0F}));
    // Far off to the side.
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{1000.0F, 0.0F, -10.0F}));
    // Far above.
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 1000.0F, -10.0F}));
}

// The vertical field of view sets the half-height at a given depth, and the aspect
// ratio widens it. Getting the two swapped is the classic frustum bug.
TEST(MathFrustumTest, FieldOfViewAndAspectSizeTheOpening)
{
    CameraFixture camera{};
    camera.verticalFovDegrees = 90.0F;
    camera.aspectRatio = 2.0F;
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    // At depth 10 a 90 degree vertical FOV gives half-height 10 and half-width 20.
    EXPECT_TRUE(Math::contains(*frustum, Math::Vec3{0.0F, 9.5F, -10.0F}));
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 10.5F, -10.0F}));
    EXPECT_TRUE(Math::contains(*frustum, Math::Vec3{19.0F, 0.0F, -10.0F}));
    EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{21.0F, 0.0F, -10.0F}));
}

TEST(MathFrustumTest, SphereCullingAcceptsStraddlingAndRejectsDistantSpheres)
{
    const CameraFixture camera{};
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    // Fully inside.
    EXPECT_TRUE(Math::intersects(*frustum, Math::Sphere{{0.0F, 0.0F, -10.0F}, 1.0F}));
    // Center outside, but the radius reaches in.
    EXPECT_TRUE(Math::intersects(*frustum, Math::Sphere{{0.0F, 0.0F, 0.2F}, 2.0F}));
    // Entirely behind the camera.
    EXPECT_FALSE(Math::intersects(*frustum, Math::Sphere{{0.0F, 0.0F, 50.0F}, 1.0F}));
    // Entirely past the far plane.
    EXPECT_FALSE(Math::intersects(*frustum, Math::Sphere{{0.0F, 0.0F, -500.0F}, 10.0F}));
    // Far off to the side.
    EXPECT_FALSE(Math::intersects(*frustum, Math::Sphere{{1000.0F, 0.0F, -10.0F}, 1.0F}));
}

TEST(MathFrustumTest, BoxCullingUsesTheProjectedExtent)
{
    const CameraFixture camera{};
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    EXPECT_TRUE(Math::intersects(
        *frustum, Math::fromCenterHalfExtents(Math::Vec3{0.0F, 0.0F, -10.0F},
                                              Math::Vec3{1.0F, 1.0F, 1.0F})));
    // A box that straddles the near plane still intersects.
    EXPECT_TRUE(Math::intersects(
        *frustum, Math::fromCenterHalfExtents(Math::Vec3{0.0F, 0.0F, 0.0F},
                                              Math::Vec3{1.0F, 1.0F, 2.0F})));
    // Entirely behind.
    EXPECT_FALSE(Math::intersects(
        *frustum, Math::fromCenterHalfExtents(Math::Vec3{0.0F, 0.0F, 50.0F},
                                              Math::Vec3{1.0F, 1.0F, 1.0F})));
    // Entirely beyond the far plane.
    EXPECT_FALSE(Math::intersects(
        *frustum, Math::fromCenterHalfExtents(Math::Vec3{0.0F, 0.0F, -500.0F},
                                              Math::Vec3{10.0F, 10.0F, 10.0F})));
    // A box enclosing the whole frustum intersects it.
    EXPECT_TRUE(Math::intersects(
        *frustum, Math::fromCenterHalfExtents(Math::Vec3{}, Math::Vec3{1000.0F, 1000.0F, 1000.0F})));
}

TEST(MathFrustumTest, RotatedCameraCullsInItsOwnBasis)
{
    CameraFixture camera{};
    camera.position = Math::Vec3{5.0F, 0.0F, 0.0F};
    camera.forward = Math::Vec3{1.0F, 0.0F, 0.0F};
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    // In front along +X.
    EXPECT_TRUE(Math::intersects(*frustum, Math::Sphere{{15.0F, 0.0F, 0.0F}, 1.0F}));
    // Behind the camera along -X.
    EXPECT_FALSE(Math::intersects(*frustum, Math::Sphere{{-5.0F, 0.0F, 0.0F}, 1.0F}));
}

// Re-orthogonalization removes exactly two things from `up`: its length, and its
// component along forward. It must NOT remove roll — a component perpendicular to
// both axes is genuine orientation, and discarding it would silently ignore a
// camera's tilt.
//
// So the skewed basis here is non-unit and tilted toward forward but carries no
// roll, and must yield the same frustum. forward is -Z, so the z term is the
// along-forward component that gets projected out; there is no x term.
TEST(MathFrustumTest, BasisIsReorthogonalized)
{
    CameraFixture clean{};
    CameraFixture skewed{};
    skewed.up = Math::Vec3{0.0F, 7.0F, 5.0F};

    const std::optional<Math::Frustum> fromClean = buildFrustum(clean);
    const std::optional<Math::Frustum> fromSkewed = buildFrustum(skewed);
    ASSERT_TRUE(fromClean.has_value());
    ASSERT_TRUE(fromSkewed.has_value());

    for (usize index = 0; index < Math::FrustumPlaneCount; ++index) {
        EXPECT_NEAR(fromClean->planes[index].normal.x, fromSkewed->planes[index].normal.x,
                    1.0e-4F);
        EXPECT_NEAR(fromClean->planes[index].normal.y, fromSkewed->planes[index].normal.y,
                    1.0e-4F);
        EXPECT_NEAR(fromClean->planes[index].normal.z, fromSkewed->planes[index].normal.z,
                    1.0e-4F);
        EXPECT_NEAR(fromClean->planes[index].distance, fromSkewed->planes[index].distance,
                    1.0e-4F);
    }
}

// The other half of the contract above: a rolled camera produces a genuinely
// different frustum. Without this, an implementation that discarded roll entirely
// would still pass BasisIsReorthogonalized.
TEST(MathFrustumTest, RollIsPreservedNotOrthogonalizedAway)
{
    // 90 degrees vertical with aspect 2 makes the extents exact at depth 10:
    // half-height 10, half-width 20.
    CameraFixture upright{};
    upright.verticalFovDegrees = 90.0F;
    upright.aspectRatio = 2.0F;
    CameraFixture rolled = upright;
    // A 90 degree roll about the forward axis swaps the wide and tall directions.
    rolled.up = Math::Vec3{1.0F, 0.0F, 0.0F};

    const std::optional<Math::Frustum> fromUpright = buildFrustum(upright);
    const std::optional<Math::Frustum> fromRolled = buildFrustum(rolled);
    ASSERT_TRUE(fromUpright.has_value());
    ASSERT_TRUE(fromRolled.has_value());
    EXPECT_FALSE(*fromUpright == *fromRolled);

    // Inside the wide axis but outside the tall one, so only one camera contains it.
    const Math::Vec3 wide{15.0F, 0.0F, -10.0F};
    const Math::Vec3 tall{0.0F, 15.0F, -10.0F};
    EXPECT_TRUE(Math::contains(*fromUpright, wide));
    EXPECT_FALSE(Math::contains(*fromUpright, tall));
    EXPECT_FALSE(Math::contains(*fromRolled, wide));
    EXPECT_TRUE(Math::contains(*fromRolled, tall));
}

TEST(MathFrustumTest, ConstructionRejectsDegenerateBasisAndParameters)
{
    CameraFixture camera{};
    // Zero forward.
    camera.forward = Math::Vec3{};
    EXPECT_FALSE(buildFrustum(camera).has_value());
    // up parallel to forward leaves no unique right vector.
    camera = CameraFixture{};
    camera.up = Math::Vec3{0.0F, 0.0F, -1.0F};
    EXPECT_FALSE(buildFrustum(camera).has_value());
    // Invalid frustum parameters.
    camera = CameraFixture{};
    camera.verticalFovDegrees = 0.0F;
    EXPECT_FALSE(buildFrustum(camera).has_value());
    camera = CameraFixture{};
    camera.verticalFovDegrees = 180.0F;
    EXPECT_FALSE(buildFrustum(camera).has_value());
    camera = CameraFixture{};
    camera.aspectRatio = 0.0F;
    EXPECT_FALSE(buildFrustum(camera).has_value());
    camera = CameraFixture{};
    camera.nearPlane = 0.0F;
    EXPECT_FALSE(buildFrustum(camera).has_value());
    camera = CameraFixture{};
    camera.farPlane = camera.nearPlane;
    EXPECT_FALSE(buildFrustum(camera).has_value());
    camera = CameraFixture{};
    camera.position = Math::Vec3{QuietNaN, 0.0F, 0.0F};
    EXPECT_FALSE(buildFrustum(camera).has_value());
}

// Extracting the planes from a view-projection matrix must agree with building them
// from the camera basis. This is the cross-check between frustumFromPerspective,
// perspectiveRightHanded and lookAtRightHanded: an error in any one shows up here.
TEST(MathFrustumTest, ViewProjectionExtractionAgreesWithBasisConstruction)
{
    const CameraFixture camera{
        .position = Math::Vec3{3.0F, 4.0F, 5.0F},
        .forward = Math::Vec3{0.0F, 0.0F, -1.0F},
        .up = Math::Vec3{0.0F, 1.0F, 0.0F},
        .verticalFovDegrees = 55.0F,
        .aspectRatio = 4.0F / 3.0F,
        .nearPlane = 0.25F,
        .farPlane = 250.0F,
    };
    const std::optional<Math::Frustum> fromBasis = buildFrustum(camera);
    ASSERT_TRUE(fromBasis.has_value());

    const std::optional<Math::Mat4> view = Math::lookAtRightHanded(
        camera.position, camera.position + camera.forward, camera.up);
    const std::optional<Math::Mat4> projection = Math::perspectiveRightHanded(
        Math::radians(camera.verticalFovDegrees),
        camera.aspectRatio,
        camera.nearPlane,
        camera.farPlane,
        Math::ClipDepthRange::ZeroToOne);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(projection.has_value());

    const std::optional<Math::Frustum> fromMatrix = Math::frustumFromViewProjection(
        *projection * *view, Math::ClipDepthRange::ZeroToOne);
    ASSERT_TRUE(fromMatrix.has_value());

    for (usize index = 0; index < Math::FrustumPlaneCount; ++index) {
        EXPECT_NEAR(fromBasis->planes[index].normal.x, fromMatrix->planes[index].normal.x,
                    1.0e-4F)
            << "plane " << index;
        EXPECT_NEAR(fromBasis->planes[index].normal.y, fromMatrix->planes[index].normal.y,
                    1.0e-4F)
            << "plane " << index;
        EXPECT_NEAR(fromBasis->planes[index].normal.z, fromMatrix->planes[index].normal.z,
                    1.0e-4F)
            << "plane " << index;
        EXPECT_NEAR(fromBasis->planes[index].distance, fromMatrix->planes[index].distance,
                    1.0e-3F)
            << "plane " << index;
    }
}

TEST(MathFrustumTest, ViewProjectionExtractionHonoursClipDepthRange)
{
    const std::optional<Math::Mat4> view = Math::lookAtRightHanded(
        Math::Vec3{0.0F, 0.0F, 10.0F}, Math::Vec3{}, Math::Vec3{0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(view.has_value());

    for (const Math::ClipDepthRange range :
         {Math::ClipDepthRange::ZeroToOne, Math::ClipDepthRange::NegativeOneToOne}) {
        const std::optional<Math::Mat4> projection = Math::perspectiveRightHanded(
            Math::radians(60.0F), 1.0F, 1.0F, 50.0F, range);
        ASSERT_TRUE(projection.has_value());
        const std::optional<Math::Frustum> frustum =
            Math::frustumFromViewProjection(*projection * *view, range);
        ASSERT_TRUE(frustum.has_value());

        // Near plane sits 1 unit in front of the eye at z == 9; far plane at z == -40.
        EXPECT_TRUE(Math::contains(*frustum, Math::Vec3{0.0F, 0.0F, 5.0F}));
        EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 0.0F, 9.5F}));
        EXPECT_FALSE(Math::contains(*frustum, Math::Vec3{0.0F, 0.0F, -41.0F}));
    }
}

TEST(MathFrustumTest, ViewProjectionExtractionRejectsDegenerateMatrices)
{
    Math::Mat4 allZero{};
    allZero.columns.fill(0.0F);
    EXPECT_FALSE(
        Math::frustumFromViewProjection(allZero, Math::ClipDepthRange::ZeroToOne).has_value());

    Math::Mat4 nonFinite = Math::identityMat4();
    nonFinite.columns[5] = QuietNaN;
    EXPECT_FALSE(
        Math::frustumFromViewProjection(nonFinite, Math::ClipDepthRange::ZeroToOne).has_value());
}

// D7 regression gate: the standalone overload must reproduce the deleted
// ExtractRenderScene helper bit for bit across a spread of positions and radii.
// The 3D product evidence counters depend on this exact classification.
TEST(MathFrustumTest, StandaloneSphereTestMatchesTheReplacedExtractHelper)
{
    const CameraFixture camera{
        .position = Math::Vec3{1.0F, 2.0F, 3.0F},
        .forward = Math::Vec3{0.0F, 0.0F, -1.0F},
        .up = Math::Vec3{0.0F, 1.0F, 0.0F},
        .verticalFovDegrees = 55.0F,
        .aspectRatio = 16.0F / 9.0F,
        .nearPlane = 0.1F,
        .farPlane = 120.0F,
    };

    usize insideCount = 0;
    usize outsideCount = 0;
    for (int x = -60; x <= 60; x += 7) {
        for (int y = -60; y <= 60; y += 11) {
            for (int z = -160; z <= 40; z += 13) {
                for (const float radius : {0.0F, 0.25F, 4.0F, 40.0F}) {
                    const auto positionX = static_cast<float>(x);
                    const auto positionY = static_cast<float>(y);
                    const auto positionZ = static_cast<float>(z);

                    const bool expected = legacySphereIntersectsPerspectiveCamera(
                        positionX, positionY, positionZ, radius, camera);
                    const bool actual = Math::sphereIntersectsPerspectiveFrustum(
                        Math::Vec3{positionX, positionY, positionZ},
                        radius,
                        camera.position,
                        camera.forward,
                        camera.up,
                        camera.verticalFovDegrees,
                        camera.aspectRatio,
                        camera.nearPlane,
                        camera.farPlane);
                    ASSERT_EQ(expected, actual)
                        << "diverged at (" << positionX << ", " << positionY << ", " << positionZ
                        << ") radius " << radius;
                    if (expected) {
                        ++insideCount;
                    } else {
                        ++outsideCount;
                    }
                }
            }
        }
    }
    // Both outcomes must be well represented, otherwise the loop proves nothing.
    EXPECT_GT(insideCount, 100U);
    EXPECT_GT(outsideCount, 100U);
}

// The standalone overload and the Frustum planes are the same algebra, so they must
// classify the same spheres. Only the accumulation precision differs, which is why
// borderline cases are excluded here rather than asserted bit-exact.
TEST(MathFrustumTest, StandaloneSphereTestAgreesWithFrustumPlanes)
{
    const CameraFixture camera{};
    const std::optional<Math::Frustum> frustum = buildFrustum(camera);
    ASSERT_TRUE(frustum.has_value());

    for (int x = -40; x <= 40; x += 9) {
        for (int y = -40; y <= 40; y += 9) {
            for (int z = -120; z <= 20; z += 11) {
                const Math::Sphere sphere{
                    Math::Vec3{static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(z)},
                    3.0F};
                const bool viaPlanes = Math::intersects(*frustum, sphere);
                const bool viaBasis = Math::sphereIntersectsPerspectiveFrustum(
                    sphere.center,
                    sphere.radius,
                    camera.position,
                    camera.forward,
                    camera.up,
                    camera.verticalFovDegrees,
                    camera.aspectRatio,
                    camera.nearPlane,
                    camera.farPlane);
                EXPECT_EQ(viaPlanes, viaBasis)
                    << "diverged at (" << sphere.center.x << ", " << sphere.center.y << ", "
                    << sphere.center.z << ")";
            }
        }
    }
}

} // namespace Tina::Tests
