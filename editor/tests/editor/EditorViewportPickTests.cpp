#include <gtest/gtest.h>

#include <tina/editor/EditorViewportPick.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-4F;
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

constexpr float ViewportWidth = 800.0F;
constexpr float ViewportHeight = 600.0F;
constexpr float FovDegrees = 55.0F;

// Camera at the origin looking down -Z, matching the editor's 3D preview camera.
[[nodiscard]] Editor::EditorViewportRayQuery centeredQuery(
    float pointerX = ViewportWidth * 0.5F,
    float pointerY = ViewportHeight * 0.5F)
{
    return Editor::EditorViewportRayQuery{
        .cameraPosition = Math::Vec3{0.0F, 0.0F, 0.0F},
        .cameraRotation = Math::Quaternion{},
        .verticalFovDegrees = FovDegrees,
        .viewportWidth = ViewportWidth,
        .viewportHeight = ViewportHeight,
        .pointerX = pointerX,
        .pointerY = pointerY,
    };
}

void expectVectorNear(Math::Vec3 actual, Math::Vec3 expected, float tolerance = Tolerance)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

} // namespace

TEST(EditorViewportPickTest, CenterPixelAimsAlongTheCameraForward)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());
    expectVectorNear(ray->origin, Math::Vec3{});
    expectVectorNear(ray->direction, Math::Vec3{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(Math::length(ray->direction), 1.0F, Tolerance);
}

// Logical UI space grows downward while the camera up axis grows upward, so the
// vertical component has to flip. Getting this wrong makes picking mirror
// vertically, which looks almost right and is easy to miss.
TEST(EditorViewportPickTest, ScreenAxesMapToTheExpectedSigns)
{
    const auto topLeft = Editor::editorViewportPickRay(centeredQuery(0.0F, 0.0F));
    ASSERT_TRUE(topLeft.has_value());
    EXPECT_LT(topLeft->direction.x, 0.0F);
    EXPECT_GT(topLeft->direction.y, 0.0F);
    EXPECT_LT(topLeft->direction.z, 0.0F);

    const auto bottomRight =
        Editor::editorViewportPickRay(centeredQuery(ViewportWidth, ViewportHeight));
    ASSERT_TRUE(bottomRight.has_value());
    EXPECT_GT(bottomRight->direction.x, 0.0F);
    EXPECT_LT(bottomRight->direction.y, 0.0F);
    EXPECT_LT(bottomRight->direction.z, 0.0F);
}

// The vertical field of view sets the vertical half-angle; the aspect ratio widens
// the horizontal one. Swapping them is the classic projection bug.
TEST(EditorViewportPickTest, FieldOfViewAndAspectSetTheRayAngles)
{
    const auto topEdge =
        Editor::editorViewportPickRay(centeredQuery(ViewportWidth * 0.5F, 0.0F));
    ASSERT_TRUE(topEdge.has_value());
    // tan(halfFov) == y/-z at the top edge.
    const float verticalTangent = topEdge->direction.y / -topEdge->direction.z;
    EXPECT_NEAR(verticalTangent, std::tan(FovDegrees * Math::DegreesToRadians * 0.5F),
                Tolerance);

    const auto rightEdge =
        Editor::editorViewportPickRay(centeredQuery(ViewportWidth, ViewportHeight * 0.5F));
    ASSERT_TRUE(rightEdge.has_value());
    const float horizontalTangent = rightEdge->direction.x / -rightEdge->direction.z;
    EXPECT_NEAR(horizontalTangent,
                std::tan(FovDegrees * Math::DegreesToRadians * 0.5F)
                    * (ViewportWidth / ViewportHeight),
                Tolerance);
}

TEST(EditorViewportPickTest, RayStartsAtTheCameraAndFollowsItsRotation)
{
    Editor::EditorViewportRayQuery query = centeredQuery();
    query.cameraPosition = Math::Vec3{5.0F, 6.0F, 7.0F};
    // Yaw 90 degrees about +Y turns the camera's -Z toward -X.
    query.cameraRotation = Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, Math::HalfPi);

    const auto ray = Editor::editorViewportPickRay(query);
    ASSERT_TRUE(ray.has_value());
    expectVectorNear(ray->origin, Math::Vec3{5.0F, 6.0F, 7.0F});
    expectVectorNear(ray->direction, Math::Vec3{-1.0F, 0.0F, 0.0F});
}

TEST(EditorViewportPickTest, RayConstructionRejectsDegenerateInput)
{
    Editor::EditorViewportRayQuery zeroWidth = centeredQuery();
    zeroWidth.viewportWidth = 0.0F;
    EXPECT_FALSE(Editor::editorViewportPickRay(zeroWidth).has_value());

    Editor::EditorViewportRayQuery zeroHeight = centeredQuery();
    zeroHeight.viewportHeight = 0.0F;
    EXPECT_FALSE(Editor::editorViewportPickRay(zeroHeight).has_value());

    Editor::EditorViewportRayQuery zeroFov = centeredQuery();
    zeroFov.verticalFovDegrees = 0.0F;
    EXPECT_FALSE(Editor::editorViewportPickRay(zeroFov).has_value());

    Editor::EditorViewportRayQuery fullTurnFov = centeredQuery();
    fullTurnFov.verticalFovDegrees = 180.0F;
    EXPECT_FALSE(Editor::editorViewportPickRay(fullTurnFov).has_value());

    // A zero quaternion cannot orient anything.
    Editor::EditorViewportRayQuery zeroRotation = centeredQuery();
    zeroRotation.cameraRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(Editor::editorViewportPickRay(zeroRotation).has_value());

    Editor::EditorViewportRayQuery nonFinitePointer = centeredQuery();
    nonFinitePointer.pointerX = QuietNaN;
    EXPECT_FALSE(Editor::editorViewportPickRay(nonFinitePointer).has_value());

    Editor::EditorViewportRayQuery nonFiniteCamera = centeredQuery();
    nonFiniteCamera.cameraPosition = Math::Vec3{QuietNaN, 0.0F, 0.0F};
    EXPECT_FALSE(Editor::editorViewportPickRay(nonFiniteCamera).has_value());
}

// A non-unit camera rotation is normalized rather than rejected: authored
// quaternions drift, and refusing them would make picking fail intermittently.
TEST(EditorViewportPickTest, NonUnitCameraRotationIsNormalized)
{
    Editor::EditorViewportRayQuery query = centeredQuery();
    query.cameraRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 4.0F};

    const auto ray = Editor::editorViewportPickRay(query);
    ASSERT_TRUE(ray.has_value());
    expectVectorNear(ray->direction, Math::Vec3{0.0F, 0.0F, -1.0F});
}

TEST(EditorViewportPickTest, MissingEverythingReportsNoHit)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    // Empty candidate set.
    EXPECT_FALSE(Editor::pickNearestViewportCandidate(*ray, {}).has_value());

    // Behind the camera.
    const std::array candidates{
        Editor::EditorViewportPickCandidate{
            .stableId = 1, .worldBounds = Math::Sphere{{0.0F, 0.0F, 10.0F}, 1.0F}},
    };
    EXPECT_FALSE(Editor::pickNearestViewportCandidate(*ray, candidates).has_value());

    // Off to the side.
    const std::array offAxis{
        Editor::EditorViewportPickCandidate{
            .stableId = 2, .worldBounds = Math::Sphere{{50.0F, 0.0F, -10.0F}, 1.0F}},
    };
    EXPECT_FALSE(Editor::pickNearestViewportCandidate(*ray, offAxis).has_value());
}

TEST(EditorViewportPickTest, SingleCandidateReportsDistanceAndSurfacePoint)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    const std::array candidates{
        Editor::EditorViewportPickCandidate{
            .stableId = 42, .worldBounds = Math::Sphere{{0.0F, 0.0F, -10.0F}, 2.0F}},
    };
    const auto hit = Editor::pickNearestViewportCandidate(*ray, candidates);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->stableId, 42U);
    EXPECT_NEAR(hit->distanceMeters, 8.0F, Tolerance);
    expectVectorNear(hit->worldPoint, Math::Vec3{0.0F, 0.0F, -8.0F});
}

// This is the defect the ray path exists to fix. Both spheres sit under the same
// pixel; the near one must win. A screen-area tiebreak would have to pick one by
// apparent size instead, which is exactly the wrong answer.
TEST(EditorViewportPickTest, NearestCandidateWinsRegardlessOfOrder)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    const Editor::EditorViewportPickCandidate near{
        .stableId = 1, .worldBounds = Math::Sphere{{0.0F, 0.0F, -5.0F}, 1.0F}};
    const Editor::EditorViewportPickCandidate far{
        .stableId = 2, .worldBounds = Math::Sphere{{0.0F, 0.0F, -20.0F}, 3.0F}};

    const std::array nearFirst{near, far};
    const auto fromNearFirst = Editor::pickNearestViewportCandidate(*ray, nearFirst);
    ASSERT_TRUE(fromNearFirst.has_value());
    EXPECT_EQ(fromNearFirst->stableId, 1U);

    // Reversing the candidate order must not change the outcome.
    const std::array farFirst{far, near};
    const auto fromFarFirst = Editor::pickNearestViewportCandidate(*ray, farFirst);
    ASSERT_TRUE(fromFarFirst.has_value());
    EXPECT_EQ(fromFarFirst->stableId, 1U);
}

// Regression guard against the replaced smallest-screen-area tiebreak: the near
// object is deliberately the SMALLER one on screen, so an area-based rule would
// have preferred it only by accident here — and would prefer the far one as soon
// as the far object is small. Pin the behaviour to depth, not apparent size.
TEST(EditorViewportPickTest, ADistantSmallCandidateNeverStealsTheClick)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    // Large near sphere, tiny distant sphere. On screen the distant one is far
    // smaller, so a smallest-area rule would select it.
    const std::array candidates{
        Editor::EditorViewportPickCandidate{
            .stableId = 10, .worldBounds = Math::Sphere{{0.0F, 0.0F, -6.0F}, 3.0F}},
        Editor::EditorViewportPickCandidate{
            .stableId = 11, .worldBounds = Math::Sphere{{0.0F, 0.0F, -60.0F}, 0.2F}},
    };
    const auto hit = Editor::pickNearestViewportCandidate(*ray, candidates);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->stableId, 10U) << "the near object must win over a smaller distant one";
}

// The second defect: the candidate carries a sphere CENTER. A mesh whose bounds sit
// away from its transform origin is pickable where it actually appears, and NOT at
// its origin.
TEST(EditorViewportPickTest, OffsetBoundsArePickedAtTheirOffsetPosition)
{
    // Aim slightly right of centre.
    const auto ray = Editor::editorViewportPickRay(
        centeredQuery(ViewportWidth * 0.5F + 80.0F, ViewportHeight * 0.5F));
    ASSERT_TRUE(ray.has_value());

    // Sphere centred away from the origin, on the ray.
    const Math::Vec3 offsetCenter = Math::pointAt(*ray, 12.0F);
    const std::array offsetCandidate{
        Editor::EditorViewportPickCandidate{
            .stableId = 7, .worldBounds = Math::Sphere{offsetCenter, 1.0F}},
    };
    const auto hit = Editor::pickNearestViewportCandidate(*ray, offsetCandidate);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->stableId, 7U);
    EXPECT_NEAR(hit->distanceMeters, 11.0F, 1.0e-3F);

    // The same object bounded at the origin instead is NOT under this ray, which is
    // what the old origin-only hot zone would have used.
    const std::array originCandidate{
        Editor::EditorViewportPickCandidate{
            .stableId = 7, .worldBounds = Math::Sphere{{0.0F, 0.0F, -12.0F}, 1.0F}},
    };
    EXPECT_FALSE(Editor::pickNearestViewportCandidate(*ray, originCandidate).has_value());
}

// Equal distances resolve by lower stableId so the result cannot depend on
// iteration order.
TEST(EditorViewportPickTest, EqualDistancesBreakTiesOnTheLowerStableId)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    const Editor::EditorViewportPickCandidate first{
        .stableId = 5, .worldBounds = Math::Sphere{{0.0F, 0.0F, -10.0F}, 2.0F}};
    const Editor::EditorViewportPickCandidate second{
        .stableId = 3, .worldBounds = Math::Sphere{{0.0F, 0.0F, -10.0F}, 2.0F}};

    const std::array ascending{first, second};
    const auto fromAscending = Editor::pickNearestViewportCandidate(*ray, ascending);
    ASSERT_TRUE(fromAscending.has_value());
    EXPECT_EQ(fromAscending->stableId, 3U);

    const std::array descending{second, first};
    const auto fromDescending = Editor::pickNearestViewportCandidate(*ray, descending);
    ASSERT_TRUE(fromDescending.has_value());
    EXPECT_EQ(fromDescending->stableId, 3U);
}

// A camera inside an object still picks it, reporting the exit point. Refusing the
// hit would make the object unselectable exactly when it fills the view.
TEST(EditorViewportPickTest, ACameraInsideACandidateStillPicksIt)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    const std::array candidates{
        Editor::EditorViewportPickCandidate{
            .stableId = 9, .worldBounds = Math::Sphere{{0.0F, 0.0F, 0.0F}, 4.0F}},
    };
    const auto hit = Editor::pickNearestViewportCandidate(*ray, candidates);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->stableId, 9U);
    EXPECT_NEAR(hit->distanceMeters, 4.0F, Tolerance);
}

TEST(EditorViewportPickTest, InvalidCandidatesAreSkippedNotFatal)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    const std::array candidates{
        // Zero stableId is the "absent" marker used across editor selection.
        Editor::EditorViewportPickCandidate{
            .stableId = 0, .worldBounds = Math::Sphere{{0.0F, 0.0F, -2.0F}, 1.0F}},
        // Negative radius is not a volume.
        Editor::EditorViewportPickCandidate{
            .stableId = 1, .worldBounds = Math::Sphere{{0.0F, 0.0F, -3.0F}, -1.0F}},
        // Non-finite centre.
        Editor::EditorViewportPickCandidate{
            .stableId = 2, .worldBounds = Math::Sphere{{QuietNaN, 0.0F, -4.0F}, 1.0F}},
        // The only usable candidate, and the farthest of the four.
        Editor::EditorViewportPickCandidate{
            .stableId = 3, .worldBounds = Math::Sphere{{0.0F, 0.0F, -30.0F}, 2.0F}},
    };
    const auto hit = Editor::pickNearestViewportCandidate(*ray, candidates);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->stableId, 3U);
}

TEST(EditorViewportPickTest, NonFiniteRayReportsNoHit)
{
    const Math::Ray broken{Math::Vec3{QuietNaN, 0.0F, 0.0F}, Math::Vec3{0.0F, 0.0F, -1.0F}};
    const std::array candidates{
        Editor::EditorViewportPickCandidate{
            .stableId = 1, .worldBounds = Math::Sphere{{0.0F, 0.0F, -5.0F}, 1.0F}},
    };
    EXPECT_FALSE(Editor::pickNearestViewportCandidate(broken, candidates).has_value());
}

// Fixed capacity, matching the marquee surface. Candidates past the bound are
// ignored rather than growing the scan.
TEST(EditorViewportPickTest, ScanIsBoundedByTheDeclaredCapacity)
{
    const auto ray = Editor::editorViewportPickRay(centeredQuery());
    ASSERT_TRUE(ray.has_value());

    std::vector<Editor::EditorViewportPickCandidate> candidates;
    // Fill capacity with far spheres, all on the ray.
    for (Core::usize index = 0; index < Editor::EditorViewportPickCandidateCapacity; ++index) {
        candidates.push_back(Editor::EditorViewportPickCandidate{
            .stableId = static_cast<Core::u64>(index + 1),
            .worldBounds = Math::Sphere{{0.0F, 0.0F, -100.0F}, 1.0F}});
    }
    // One extra, nearer candidate beyond the capacity bound.
    candidates.push_back(Editor::EditorViewportPickCandidate{
        .stableId = 9999, .worldBounds = Math::Sphere{{0.0F, 0.0F, -5.0F}, 1.0F}});

    const auto hit = Editor::pickNearestViewportCandidate(*ray, candidates);
    ASSERT_TRUE(hit.has_value());
    // The nearer out-of-capacity candidate is not considered.
    EXPECT_NE(hit->stableId, 9999U);
    EXPECT_NEAR(hit->distanceMeters, 99.0F, 1.0e-3F);
}

} // namespace Tina::Tests
