#include <gtest/gtest.h>

#include <tina/math/Geometry3D.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-5F;
constexpr float Infinity = std::numeric_limits<float>::infinity();
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

void expectVectorNear(Math::Vec3 actual, Math::Vec3 expected, float tolerance = Tolerance)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

} // namespace

TEST(MathGeometry3DTest, AabbAccessorsAndConstruction)
{
    const Math::Aabb3 box{{-1.0F, -2.0F, -3.0F}, {1.0F, 2.0F, 3.0F}};
    EXPECT_EQ(Math::center(box), (Math::Vec3{}));
    EXPECT_EQ(Math::extents(box), (Math::Vec3{2.0F, 4.0F, 6.0F}));
    EXPECT_EQ(Math::halfExtents(box), (Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(Math::fromCenterHalfExtents(Math::Vec3{}, Math::Vec3{1.0F, 2.0F, 3.0F}), box);
    EXPECT_EQ(Math::fromPoints(Math::Vec3{1.0F, 2.0F, 3.0F}, Math::Vec3{-1.0F, -2.0F, -3.0F}),
              box);
}

// emptyAabb3 is the identity for merge/expand. A default-constructed box would
// wrongly include the origin, silently inflating every accumulated bound.
TEST(MathGeometry3DTest, EmptyBoxIsTheIdentityForAccumulation)
{
    const Math::Aabb3 empty = Math::emptyAabb3();
    EXPECT_FALSE(Math::isValid(empty)) << "the identity is intentionally inverted";

    const Math::Aabb3 single = Math::expand(empty, Math::Vec3{5.0F, 6.0F, 7.0F});
    EXPECT_EQ(single, (Math::Aabb3{{5.0F, 6.0F, 7.0F}, {5.0F, 6.0F, 7.0F}}));
    EXPECT_TRUE(Math::isValid(single));
    // Accumulating from a default box would have dragged in the origin.
    EXPECT_FALSE(Math::contains(single, Math::Vec3{}));

    const Math::Aabb3 both = Math::expand(single, Math::Vec3{-1.0F, 0.0F, 9.0F});
    EXPECT_EQ(both, (Math::Aabb3{{-1.0F, 0.0F, 7.0F}, {5.0F, 6.0F, 9.0F}}));
}

TEST(MathGeometry3DTest, AabbContainmentAndIntersection)
{
    const Math::Aabb3 unit{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    EXPECT_TRUE(Math::contains(unit, Math::Vec3{1.0F, 1.0F, 1.0F}));
    EXPECT_FALSE(Math::contains(unit, Math::Vec3{1.0F, 1.0F, 1.001F}));
    EXPECT_TRUE(Math::contains(unit, Math::Aabb3{{0.2F, 0.2F, 0.2F}, {0.8F, 0.8F, 0.8F}}));
    EXPECT_FALSE(Math::contains(unit, Math::Aabb3{{0.2F, 0.2F, 0.2F}, {0.8F, 0.8F, 1.5F}}));
    // Touching faces count as intersecting.
    EXPECT_TRUE(Math::intersects(unit, Math::Aabb3{{1.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 1.0F}}));
    EXPECT_FALSE(Math::intersects(unit, Math::Aabb3{{1.01F, 0.0F, 0.0F}, {2.0F, 1.0F, 1.0F}}));
}

TEST(MathGeometry3DTest, AabbMergeExpandAndClamp)
{
    const Math::Aabb3 left{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    EXPECT_EQ(Math::merge(left, Math::Aabb3{{-5.0F, 2.0F, 0.0F}, {-4.0F, 3.0F, 0.5F}}),
              (Math::Aabb3{{-5.0F, 0.0F, 0.0F}, {1.0F, 3.0F, 1.0F}}));
    EXPECT_EQ(Math::expand(left, 1.0F), (Math::Aabb3{{-1.0F, -1.0F, -1.0F}, {2.0F, 2.0F, 2.0F}}));
    EXPECT_EQ(Math::clamped(left, Math::Vec3{-2.0F, 0.5F, 9.0F}), (Math::Vec3{0.0F, 0.5F, 1.0F}));
}

TEST(MathGeometry3DTest, CornersEnumerateEveryVertexOnce)
{
    const Math::Aabb3 box{{0.0F, 0.0F, 0.0F}, {1.0F, 2.0F, 3.0F}};
    const std::array<Math::Vec3, 8> vertices = Math::corners(box);

    for (const Math::Vec3& vertex : vertices) {
        EXPECT_TRUE(Math::contains(box, vertex));
    }
    // All eight are distinct, and re-bounding them reproduces the box.
    Math::Aabb3 rebuilt = Math::emptyAabb3();
    for (const Math::Vec3& vertex : vertices) {
        rebuilt = Math::expand(rebuilt, vertex);
    }
    EXPECT_EQ(rebuilt, box);
    for (usize outer = 0; outer < vertices.size(); ++outer) {
        for (usize inner = outer + 1U; inner < vertices.size(); ++inner) {
            EXPECT_FALSE(vertices[outer] == vertices[inner]) << outer << " vs " << inner;
        }
    }
}

// Transforming all eight corners stays correct under rotation, where the
// |M| * halfExtents shortcut would be wrong.
TEST(MathGeometry3DTest, TransformedBoxCoversTheRotatedBox)
{
    const Math::Aabb3 unit =
        Math::fromCenterHalfExtents(Math::Vec3{}, Math::Vec3{1.0F, 1.0F, 1.0F});
    const Math::Mat4 rotation = Math::fromQuaternion(
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::radians(45.0F)));

    const Math::Aabb3 bounds = Math::transformed(unit, rotation);
    ASSERT_TRUE(Math::isValid(bounds));
    // The XY footprint grows to sqrt(2); Z is untouched by a Z rotation.
    EXPECT_NEAR(Math::halfExtents(bounds).x, std::sqrt(2.0F), 1.0e-4F);
    EXPECT_NEAR(Math::halfExtents(bounds).z, 1.0F, Tolerance);
    for (const Math::Vec3& corner : Math::corners(unit)) {
        EXPECT_TRUE(Math::contains(Math::expand(bounds, Tolerance),
                                   Math::transformPoint(rotation, corner)));
    }
}

TEST(MathGeometry3DTest, TransformedBoxAppliesTranslation)
{
    const Math::Aabb3 unit{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    const Math::Aabb3 moved =
        Math::transformed(unit, Math::translationMat4(Math::Vec3{10.0F, 0.0F, -5.0F}));
    EXPECT_EQ(moved, (Math::Aabb3{{10.0F, 0.0F, -5.0F}, {11.0F, 1.0F, -4.0F}}));
}

TEST(MathGeometry3DTest, SphereContainmentAndIntersection)
{
    const Math::Sphere sphere{{0.0F, 0.0F, 0.0F}, 2.0F};
    EXPECT_TRUE(Math::contains(sphere, Math::Vec3{2.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(Math::contains(sphere, Math::Vec3{2.01F, 0.0F, 0.0F}));
    // Touching spheres intersect.
    EXPECT_TRUE(Math::intersects(sphere, Math::Sphere{{5.0F, 0.0F, 0.0F}, 3.0F}));
    EXPECT_FALSE(Math::intersects(sphere, Math::Sphere{{5.0F, 0.0F, 0.0F}, 2.9F}));
    EXPECT_EQ(Math::bounds(sphere), (Math::Aabb3{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}}));
}

// The sphere-box test uses the closest point on the box, so a sphere near a corner
// is not wrongly reported as overlapping.
TEST(MathGeometry3DTest, SphereBoxUsesClosestPoint)
{
    const Math::Aabb3 unit{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    // Face proximity.
    EXPECT_TRUE(Math::intersects(Math::Sphere{{2.0F, 0.0F, 0.0F}, 1.0F}, unit));
    EXPECT_FALSE(Math::intersects(Math::Sphere{{2.1F, 0.0F, 0.0F}, 1.0F}, unit));
    // Corner proximity: the diagonal gap is sqrt(3) * 1, larger than the axis gap.
    const Math::Sphere nearCorner{{2.0F, 2.0F, 2.0F}, 1.5F};
    EXPECT_FALSE(Math::intersects(nearCorner, unit));
    EXPECT_TRUE(Math::intersects(Math::Sphere{{2.0F, 2.0F, 2.0F}, 1.8F}, unit));
    // Both argument orders agree.
    EXPECT_EQ(Math::intersects(nearCorner, unit), Math::intersects(unit, nearCorner));
    // A sphere fully inside still intersects.
    EXPECT_TRUE(Math::intersects(Math::Sphere{{0.0F, 0.0F, 0.0F}, 0.1F}, unit));
}

// The radius scales by the largest axis scale, which is conservative under
// non-uniform scale — the exact result would no longer be a sphere.
TEST(MathGeometry3DTest, TransformedSphereUsesLargestScale)
{
    const Math::Sphere local{{1.0F, 0.0F, 0.0F}, 2.0F};
    const std::optional<Math::Sphere> world = Math::transformed(
        local,
        Math::Vec3{10.0F, 0.0F, 0.0F},
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi),
        Math::Vec3{3.0F, 1.0F, 1.0F});
    ASSERT_TRUE(world.has_value());

    // Center: scaled by 3 on x, then rotated onto +Y, then translated.
    expectVectorNear(world->center, Math::Vec3{10.0F, 3.0F, 0.0F});
    EXPECT_NEAR(world->radius, 6.0F, Tolerance);
}

TEST(MathGeometry3DTest, TransformedSphereRejectsInvalidInput)
{
    const Math::Sphere valid{{}, 1.0F};
    const Math::Quaternion identity{};
    const Math::Vec3 unitScale{1.0F, 1.0F, 1.0F};

    // Non-positive scale would produce a mirrored or collapsed sphere.
    EXPECT_FALSE(Math::transformed(valid, Math::Vec3{}, identity, Math::Vec3{0.0F, 1.0F, 1.0F})
                     .has_value());
    EXPECT_FALSE(Math::transformed(valid, Math::Vec3{}, identity, Math::Vec3{-1.0F, 1.0F, 1.0F})
                     .has_value());
    EXPECT_FALSE(Math::transformed(valid, Math::Vec3{QuietNaN, 0.0F, 0.0F}, identity, unitScale)
                     .has_value());
    // A degenerate rotation cannot orient the offset center.
    EXPECT_FALSE(Math::transformed(valid, Math::Vec3{},
                                   Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F}, unitScale)
                     .has_value());
    EXPECT_FALSE(Math::transformed(Math::Sphere{{}, -1.0F}, Math::Vec3{}, identity, unitScale)
                     .has_value());
}

// Plane is constant-normal form: dot(normal, point) + distance, positive in front.
TEST(MathGeometry3DTest, PlaneSignedDistanceIsPositiveInFrontOfTheNormal)
{
    const std::optional<Math::Plane> plane =
        Math::planeFromPointNormal(Math::Vec3{0.0F, 2.0F, 0.0F}, Math::Vec3{0.0F, 5.0F, 0.0F});
    ASSERT_TRUE(plane.has_value());
    EXPECT_EQ(plane->normal, (Math::Vec3{0.0F, 1.0F, 0.0F})) << "the normal is normalized";
    EXPECT_NEAR(plane->distance, -2.0F, Tolerance);

    EXPECT_NEAR(Math::signedDistance(*plane, Math::Vec3{9.0F, 5.0F, 9.0F}), 3.0F, Tolerance);
    EXPECT_NEAR(Math::signedDistance(*plane, Math::Vec3{0.0F, 2.0F, 0.0F}), 0.0F, Tolerance);
    EXPECT_NEAR(Math::signedDistance(*plane, Math::Vec3{0.0F, 0.0F, 0.0F}), -2.0F, Tolerance);
    expectVectorNear(Math::projectOnto(*plane, Math::Vec3{4.0F, 7.0F, 8.0F}),
                     Math::Vec3{4.0F, 2.0F, 8.0F});
}

// Counter-clockwise winding yields a normal toward the viewer, matching the
// right-handed convention used elsewhere in this module.
TEST(MathGeometry3DTest, PlaneFromPointsUsesCounterClockwiseWinding)
{
    const std::optional<Math::Plane> plane = Math::planeFromPoints(
        Math::Vec3{0.0F, 0.0F, 0.0F},
        Math::Vec3{1.0F, 0.0F, 0.0F},
        Math::Vec3{0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(plane.has_value());
    expectVectorNear(plane->normal, Math::Vec3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(plane->distance, 0.0F, Tolerance);
}

TEST(MathGeometry3DTest, PlaneConstructionRejectsDegenerateInput)
{
    EXPECT_FALSE(Math::planeFromPointNormal(Math::Vec3{}, Math::Vec3{}).has_value());
    // Collinear points do not define a plane.
    EXPECT_FALSE(Math::planeFromPoints(Math::Vec3{}, Math::Vec3{1.0F, 0.0F, 0.0F},
                                       Math::Vec3{2.0F, 0.0F, 0.0F})
                     .has_value());
    EXPECT_FALSE(Math::normalizedPlane(Math::Plane{Math::Vec3{}, 1.0F}).has_value());
    EXPECT_FALSE(Math::normalizedPlane(Math::Plane{Math::Vec3{QuietNaN, 0.0F, 0.0F}, 1.0F})
                     .has_value());
}

// Normalizing scales the distance along with the normal, so the represented plane
// does not move.
TEST(MathGeometry3DTest, NormalizedPlaneKeepsTheSameSurface)
{
    const Math::Plane scaled{Math::Vec3{0.0F, 4.0F, 0.0F}, -8.0F};
    const std::optional<Math::Plane> unit = Math::normalizedPlane(scaled);
    ASSERT_TRUE(unit.has_value());
    EXPECT_NEAR(Math::length(unit->normal), 1.0F, Tolerance);
    // Both describe y == 2.
    EXPECT_NEAR(Math::signedDistance(*unit, Math::Vec3{0.0F, 2.0F, 0.0F}), 0.0F, Tolerance);
}

TEST(MathGeometry3DTest, NearestSignedDistanceUsesTheClosestBoxCorner)
{
    const Math::Plane ground{Math::Vec3{0.0F, 1.0F, 0.0F}, 0.0F};
    const Math::Aabb3 box{{-1.0F, 5.0F, -1.0F}, {1.0F, 7.0F, 1.0F}};
    // The closest corner sits at y == 5.
    EXPECT_NEAR(Math::nearestSignedDistance(ground, box), 5.0F, Tolerance);
    // Straddling the plane gives a negative nearest distance.
    EXPECT_LT(Math::nearestSignedDistance(ground,
                                          Math::Aabb3{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}}),
              0.0F);
}

TEST(MathGeometry3DTest, MakeRayNormalizesAndRejectsDegenerateDirection)
{
    const std::optional<Math::Ray> ray =
        Math::makeRay(Math::Vec3{1.0F, 2.0F, 3.0F}, Math::Vec3{0.0F, 0.0F, -9.0F});
    ASSERT_TRUE(ray.has_value());
    EXPECT_NEAR(Math::length(ray->direction), 1.0F, Tolerance);
    expectVectorNear(ray->direction, Math::Vec3{0.0F, 0.0F, -1.0F});

    EXPECT_FALSE(Math::makeRay(Math::Vec3{}, Math::Vec3{}).has_value());
    EXPECT_FALSE(Math::makeRay(Math::Vec3{QuietNaN, 0.0F, 0.0F}, Math::Vec3{0.0F, 0.0F, -1.0F})
                     .has_value());
    EXPECT_FALSE(Math::makeRay(Math::Vec3{}, Math::Vec3{Infinity, 0.0F, 0.0F}).has_value());
}

TEST(MathGeometry3DTest, RaycastAabbReportsEntryPointAndFaceNormal)
{
    const Math::Aabb3 unit{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const std::optional<Math::Ray> ray =
        Math::makeRay(Math::Vec3{-5.0F, 0.0F, 0.0F}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(ray.has_value());

    const std::optional<Math::RayHit> hit = Math::raycast(*ray, unit);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 4.0F, Tolerance);
    expectVectorNear(hit->point, Math::Vec3{-1.0F, 0.0F, 0.0F});
    // The normal faces back along the incoming ray.
    expectVectorNear(hit->normal, Math::Vec3{-1.0F, 0.0F, 0.0F});
}

TEST(MathGeometry3DTest, RaycastAabbMissesAndRespectsDirectionAndRange)
{
    const Math::Aabb3 unit{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const Math::Vec3 origin{-5.0F, 0.0F, 0.0F};

    // Pointing away from the box.
    const std::optional<Math::Ray> away = Math::makeRay(origin, Math::Vec3{-1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(away.has_value());
    EXPECT_FALSE(Math::raycast(*away, unit).has_value());

    // Passing beside the box.
    const std::optional<Math::Ray> beside =
        Math::makeRay(Math::Vec3{-5.0F, 3.0F, 0.0F}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(beside.has_value());
    EXPECT_FALSE(Math::raycast(*beside, unit).has_value());

    // Within range but truncated by maximumDistance.
    const std::optional<Math::Ray> toward = Math::makeRay(origin, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(toward.has_value());
    EXPECT_TRUE(Math::raycast(*toward, unit, 4.5F).has_value());
    EXPECT_FALSE(Math::raycast(*toward, unit, 3.5F).has_value());
}

// A ray starting inside reports distance 0 at its own origin rather than the exit
// point, so a caller inside geometry still gets a usable hit.
TEST(MathGeometry3DTest, RaycastAabbFromInsideReportsZeroDistance)
{
    const Math::Aabb3 unit{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};
    const std::optional<Math::Ray> ray =
        Math::makeRay(Math::Vec3{}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(ray.has_value());

    const std::optional<Math::RayHit> hit = Math::raycast(*ray, unit);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 0.0F, Tolerance);
    expectVectorNear(hit->point, Math::Vec3{});
}

// A direction component of zero divides to infinity, and the slab comparisons then
// accept or reject that axis exactly as an explicit parallel branch would.
TEST(MathGeometry3DTest, RaycastAabbHandlesAxisParallelRays)
{
    const Math::Aabb3 box{{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}};

    // Parallel to X, inside the Y/Z slabs: hits.
    const std::optional<Math::Ray> inside =
        Math::makeRay(Math::Vec3{-5.0F, 0.5F, 0.5F}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(inside.has_value());
    EXPECT_TRUE(Math::raycast(*inside, box).has_value());

    // Parallel to X, outside the Y slab: misses.
    const std::optional<Math::Ray> outside =
        Math::makeRay(Math::Vec3{-5.0F, 2.0F, 0.0F}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(outside.has_value());
    EXPECT_FALSE(Math::raycast(*outside, box).has_value());

    // Exactly on a slab boundary: the 0 * inf case, treated as inside that axis.
    const std::optional<Math::Ray> onBoundary =
        Math::makeRay(Math::Vec3{-5.0F, 1.0F, 0.0F}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(onBoundary.has_value());
    EXPECT_TRUE(Math::raycast(*onBoundary, box).has_value());
}

TEST(MathGeometry3DTest, RaycastSphereReportsNearestSurfacePoint)
{
    const Math::Sphere sphere{{0.0F, 0.0F, 0.0F}, 2.0F};
    const std::optional<Math::Ray> ray =
        Math::makeRay(Math::Vec3{0.0F, 0.0F, 10.0F}, Math::Vec3{0.0F, 0.0F, -1.0F});
    ASSERT_TRUE(ray.has_value());

    const std::optional<Math::RayHit> hit = Math::raycast(*ray, sphere);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 8.0F, Tolerance);
    expectVectorNear(hit->point, Math::Vec3{0.0F, 0.0F, 2.0F});
    expectVectorNear(hit->normal, Math::Vec3{0.0F, 0.0F, 1.0F});
}

// A tangent ray touches at exactly one point, the discriminant is zero, and the hit
// must still be reported rather than lost to rounding.
TEST(MathGeometry3DTest, RaycastSphereHandlesTangentAndMiss)
{
    const Math::Sphere sphere{{0.0F, 0.0F, 0.0F}, 2.0F};
    const std::optional<Math::Ray> tangent =
        Math::makeRay(Math::Vec3{2.0F, 0.0F, 10.0F}, Math::Vec3{0.0F, 0.0F, -1.0F});
    ASSERT_TRUE(tangent.has_value());
    const std::optional<Math::RayHit> hit = Math::raycast(*tangent, sphere);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 10.0F, 1.0e-3F);

    const std::optional<Math::Ray> miss =
        Math::makeRay(Math::Vec3{2.5F, 0.0F, 10.0F}, Math::Vec3{0.0F, 0.0F, -1.0F});
    ASSERT_TRUE(miss.has_value());
    EXPECT_FALSE(Math::raycast(*miss, sphere).has_value());

    // Pointing away: both roots are negative.
    const std::optional<Math::Ray> away =
        Math::makeRay(Math::Vec3{0.0F, 0.0F, 10.0F}, Math::Vec3{0.0F, 0.0F, 1.0F});
    ASSERT_TRUE(away.has_value());
    EXPECT_FALSE(Math::raycast(*away, sphere).has_value());
}

// From inside, the near root is negative, so the exit point is reported. Its normal
// points outward from the center, meaning it faces the same way as the ray.
TEST(MathGeometry3DTest, RaycastSphereFromInsideReportsExitPoint)
{
    const Math::Sphere sphere{{0.0F, 0.0F, 0.0F}, 2.0F};
    const std::optional<Math::Ray> ray =
        Math::makeRay(Math::Vec3{}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(ray.has_value());

    const std::optional<Math::RayHit> hit = Math::raycast(*ray, sphere);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0F, Tolerance);
    expectVectorNear(hit->point, Math::Vec3{2.0F, 0.0F, 0.0F});
}

TEST(MathGeometry3DTest, RaycastPlaneHitsFromBothSidesWithFacingNormal)
{
    const Math::Plane ground{Math::Vec3{0.0F, 1.0F, 0.0F}, 0.0F};

    const std::optional<Math::Ray> fromAbove =
        Math::makeRay(Math::Vec3{0.0F, 5.0F, 0.0F}, Math::Vec3{0.0F, -1.0F, 0.0F});
    ASSERT_TRUE(fromAbove.has_value());
    const std::optional<Math::RayHit> aboveHit = Math::raycast(*fromAbove, ground);
    ASSERT_TRUE(aboveHit.has_value());
    EXPECT_NEAR(aboveHit->distance, 5.0F, Tolerance);
    expectVectorNear(aboveHit->point, Math::Vec3{});
    expectVectorNear(aboveHit->normal, Math::Vec3{0.0F, 1.0F, 0.0F});

    // Hitting the back face flips the reported normal toward the ray.
    const std::optional<Math::Ray> fromBelow =
        Math::makeRay(Math::Vec3{0.0F, -5.0F, 0.0F}, Math::Vec3{0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(fromBelow.has_value());
    const std::optional<Math::RayHit> belowHit = Math::raycast(*fromBelow, ground);
    ASSERT_TRUE(belowHit.has_value());
    expectVectorNear(belowHit->normal, Math::Vec3{0.0F, -1.0F, 0.0F});
}

// A parallel ray misses even when it lies exactly in the plane: there is no single
// intersection point to report.
TEST(MathGeometry3DTest, RaycastPlaneRejectsParallelRays)
{
    const Math::Plane ground{Math::Vec3{0.0F, 1.0F, 0.0F}, 0.0F};

    const std::optional<Math::Ray> alongside =
        Math::makeRay(Math::Vec3{0.0F, 3.0F, 0.0F}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(alongside.has_value());
    EXPECT_FALSE(Math::raycast(*alongside, ground).has_value());

    const std::optional<Math::Ray> inPlane =
        Math::makeRay(Math::Vec3{}, Math::Vec3{1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(inPlane.has_value());
    EXPECT_FALSE(Math::raycast(*inPlane, ground).has_value());

    // Pointing away from the plane.
    const std::optional<Math::Ray> away =
        Math::makeRay(Math::Vec3{0.0F, 3.0F, 0.0F}, Math::Vec3{0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(away.has_value());
    EXPECT_FALSE(Math::raycast(*away, ground).has_value());
}

TEST(MathGeometry3DTest, RaycastRejectsInvalidInput)
{
    const Math::Ray nonFinite{Math::Vec3{QuietNaN, 0.0F, 0.0F}, Math::Vec3{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(Math::raycast(nonFinite, Math::Aabb3{}).has_value());
    EXPECT_FALSE(Math::raycast(nonFinite, Math::Sphere{{}, 1.0F}).has_value());
    EXPECT_FALSE(Math::raycast(nonFinite, Math::Plane{}).has_value());

    const std::optional<Math::Ray> ray =
        Math::makeRay(Math::Vec3{0.0F, 0.0F, 10.0F}, Math::Vec3{0.0F, 0.0F, -1.0F});
    ASSERT_TRUE(ray.has_value());
    // Inverted box and negative radius are rejected rather than half-interpreted.
    EXPECT_FALSE(Math::raycast(*ray, Math::Aabb3{{1.0F, 1.0F, 1.0F}, {-1.0F, -1.0F, -1.0F}})
                     .has_value());
    EXPECT_FALSE(Math::raycast(*ray, Math::Sphere{{}, -1.0F}).has_value());
    EXPECT_FALSE(Math::raycast(*ray, Math::Sphere{{}, 1.0F}, QuietNaN).has_value());
}

TEST(MathGeometry3DTest, ValidityChecksRejectNonFiniteAndInvertedShapes)
{
    EXPECT_TRUE(Math::isValid(Math::Aabb3{}));
    EXPECT_FALSE(Math::isValid(Math::Aabb3{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}}));
    EXPECT_FALSE(Math::isValid(Math::Aabb3{{0.0F, 0.0F, 0.0F}, {Infinity, 0.0F, 0.0F}}));
    EXPECT_TRUE(Math::isValid(Math::Sphere{}));
    EXPECT_FALSE(Math::isValid(Math::Sphere{{}, -0.5F}));
    EXPECT_FALSE(Math::isValid(Math::Sphere{{}, QuietNaN}));
    EXPECT_TRUE(Math::isFinite(Math::Plane{}));
    EXPECT_FALSE(Math::isFinite(Math::Plane{Math::Vec3{}, QuietNaN}));
    EXPECT_TRUE(Math::isFinite(Math::Ray{}));
}

} // namespace Tina::Tests
