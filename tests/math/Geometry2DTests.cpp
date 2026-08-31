#include <gtest/gtest.h>

#include <tina/math/Geometry2D.hpp>

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-5F;
constexpr float Infinity = std::numeric_limits<float>::infinity();
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

void expectBoxNear(const Math::Aabb2& actual, const Math::Aabb2& expected)
{
    EXPECT_NEAR(actual.lower.x, expected.lower.x, Tolerance);
    EXPECT_NEAR(actual.lower.y, expected.lower.y, Tolerance);
    EXPECT_NEAR(actual.upper.x, expected.upper.x, Tolerance);
    EXPECT_NEAR(actual.upper.y, expected.upper.y, Tolerance);
}

} // namespace

TEST(MathGeometry2DTest, AabbAccessors)
{
    const Math::Aabb2 box{{-2.0F, -4.0F}, {2.0F, 4.0F}};
    EXPECT_EQ(Math::center(box), (Math::Vec2{0.0F, 0.0F}));
    EXPECT_EQ(Math::extents(box), (Math::Vec2{4.0F, 8.0F}));
    EXPECT_EQ(Math::halfExtents(box), (Math::Vec2{2.0F, 4.0F}));
    EXPECT_FLOAT_EQ(Math::area(box), 32.0F);
}

TEST(MathGeometry2DTest, ConstructionRoundTripsCenterAndCorners)
{
    const Math::Aabb2 fromCenter =
        Math::fromCenterHalfExtents(Math::Vec2{1.0F, 2.0F}, Math::Vec2{3.0F, 4.0F});
    expectBoxNear(fromCenter, Math::Aabb2{{-2.0F, -2.0F}, {4.0F, 6.0F}});
    EXPECT_EQ(Math::center(fromCenter), (Math::Vec2{1.0F, 2.0F}));
}

// fromPoints normalizes swapped corners, so a rectangle dragged in any direction is
// always valid rather than silently inverted.
TEST(MathGeometry2DTest, FromPointsNormalizesSwappedCorners)
{
    const Math::Aabb2 expected{{1.0F, 2.0F}, {3.0F, 4.0F}};
    EXPECT_EQ(Math::fromPoints(Math::Vec2{1.0F, 2.0F}, Math::Vec2{3.0F, 4.0F}), expected);
    EXPECT_EQ(Math::fromPoints(Math::Vec2{3.0F, 4.0F}, Math::Vec2{1.0F, 2.0F}), expected);
    EXPECT_EQ(Math::fromPoints(Math::Vec2{3.0F, 2.0F}, Math::Vec2{1.0F, 4.0F}), expected);
    EXPECT_TRUE(Math::isValid(Math::fromPoints(Math::Vec2{3.0F, 4.0F}, Math::Vec2{1.0F, 2.0F})));
}

// Boundary counts as inside, so adjacent tiles and touching boxes report as
// overlapping. Callers needing strict separation compare extents themselves.
TEST(MathGeometry2DTest, BoundaryCountsAsInside)
{
    const Math::Aabb2 unit{{0.0F, 0.0F}, {1.0F, 1.0F}};
    EXPECT_TRUE(Math::contains(unit, Math::Vec2{0.0F, 0.0F}));
    EXPECT_TRUE(Math::contains(unit, Math::Vec2{1.0F, 1.0F}));
    EXPECT_TRUE(Math::contains(unit, Math::Vec2{0.5F, 0.5F}));
    EXPECT_FALSE(Math::contains(unit, Math::Vec2{1.0F, 1.001F}));

    EXPECT_TRUE(Math::intersects(unit, Math::Aabb2{{1.0F, 0.0F}, {2.0F, 1.0F}}));
    EXPECT_FALSE(Math::intersects(unit, Math::Aabb2{{1.001F, 0.0F}, {2.0F, 1.0F}}));
}

TEST(MathGeometry2DTest, ContainsBoxRequiresFullEnclosure)
{
    const Math::Aabb2 outer{{0.0F, 0.0F}, {10.0F, 10.0F}};
    EXPECT_TRUE(Math::contains(outer, Math::Aabb2{{1.0F, 1.0F}, {9.0F, 9.0F}}));
    EXPECT_TRUE(Math::contains(outer, outer));
    EXPECT_FALSE(Math::contains(outer, Math::Aabb2{{1.0F, 1.0F}, {11.0F, 9.0F}}));
}

TEST(MathGeometry2DTest, MergeAndExpand)
{
    const Math::Aabb2 left{{0.0F, 0.0F}, {1.0F, 1.0F}};
    const Math::Aabb2 right{{5.0F, -3.0F}, {6.0F, -2.0F}};
    EXPECT_EQ(Math::merge(left, right), (Math::Aabb2{{0.0F, -3.0F}, {6.0F, 1.0F}}));
    EXPECT_EQ(Math::expand(left, Math::Vec2{-4.0F, 7.0F}),
              (Math::Aabb2{{-4.0F, 0.0F}, {1.0F, 7.0F}}));
    EXPECT_EQ(Math::expand(left, 2.0F), (Math::Aabb2{{-2.0F, -2.0F}, {3.0F, 3.0F}}));
}

// Componentwise clip produces an inverted box for disjoint inputs. That is
// deliberate: callers test intersects() first, which keeps the common path
// branch-free. isValid() detects the inverted result.
TEST(MathGeometry2DTest, IntersectionOfDisjointBoxesIsInverted)
{
    const Math::Aabb2 overlap = Math::intersection(Math::Aabb2{{0.0F, 0.0F}, {4.0F, 4.0F}},
                                                   Math::Aabb2{{2.0F, 1.0F}, {6.0F, 3.0F}});
    EXPECT_EQ(overlap, (Math::Aabb2{{2.0F, 1.0F}, {4.0F, 3.0F}}));
    EXPECT_TRUE(Math::isValid(overlap));

    const Math::Aabb2 disjoint = Math::intersection(Math::Aabb2{{0.0F, 0.0F}, {1.0F, 1.0F}},
                                                    Math::Aabb2{{5.0F, 5.0F}, {6.0F, 6.0F}});
    EXPECT_FALSE(Math::isValid(disjoint));
}

TEST(MathGeometry2DTest, ClampedPullsPointsOntoTheBox)
{
    const Math::Aabb2 box{{0.0F, 0.0F}, {10.0F, 5.0F}};
    EXPECT_EQ(Math::clamped(box, Math::Vec2{-3.0F, 2.0F}), (Math::Vec2{0.0F, 2.0F}));
    EXPECT_EQ(Math::clamped(box, Math::Vec2{20.0F, 9.0F}), (Math::Vec2{10.0F, 5.0F}));
    EXPECT_EQ(Math::clamped(box, Math::Vec2{4.0F, 3.0F}), (Math::Vec2{4.0F, 3.0F}));
}

// An empty box is valid: it is the identity for merge and the honest result of
// clipping a box away entirely.
TEST(MathGeometry2DTest, EmptyBoxIsValidButNonFiniteIsNot)
{
    EXPECT_TRUE(Math::isValid(Math::Aabb2{}));
    EXPECT_TRUE(Math::isValid(Math::Aabb2{{1.0F, 1.0F}, {1.0F, 1.0F}}));
    EXPECT_FALSE(Math::isValid(Math::Aabb2{{2.0F, 0.0F}, {1.0F, 1.0F}}));
    EXPECT_FALSE(Math::isValid(Math::Aabb2{{QuietNaN, 0.0F}, {1.0F, 1.0F}}));
    EXPECT_FALSE(Math::isValid(Math::Aabb2{{0.0F, 0.0F}, {Infinity, 1.0F}}));
}

// Zero rotation must leave the bounds untouched, including the translation.
TEST(MathGeometry2DTest, RotatedBoundsWithoutRotationOnlyTranslates)
{
    const Math::Aabb2 box{{-1.0F, -2.0F}, {3.0F, 4.0F}};
    expectBoxNear(Math::rotatedBounds(box, 0.0F), box);
    expectBoxNear(Math::rotatedBounds(box, 0.0F, Math::Vec2{10.0F, 20.0F}),
                  Math::Aabb2{{9.0F, 18.0F}, {13.0F, 24.0F}});
}

// A quarter turn swaps the axes exactly, so this is the one rotation where the
// conservative bound equals the true bound.
TEST(MathGeometry2DTest, RotatedBoundsSwapAxesOnQuarterTurn)
{
    const Math::Aabb2 box =
        Math::fromCenterHalfExtents(Math::Vec2{}, Math::Vec2{4.0F, 1.0F});
    expectBoxNear(Math::rotatedBounds(box, Math::HalfPi),
                  Math::fromCenterHalfExtents(Math::Vec2{}, Math::Vec2{1.0F, 4.0F}));
}

// Conservative, not exact: the result always contains the rotated box. That is the
// guarantee grid rasterization needs, where missing a cell is a correctness bug.
TEST(MathGeometry2DTest, RotatedBoundsContainEveryRotatedCorner)
{
    const Math::Aabb2 box{{-1.0F, -2.0F}, {3.0F, 4.0F}};
    constexpr float Angle = 0.7F;
    const Math::Vec2 translation{5.0F, -6.0F};
    const Math::Aabb2 bounds = Math::rotatedBounds(box, Angle, translation);
    ASSERT_TRUE(Math::isValid(bounds));

    const float cosine = std::cos(Angle);
    const float sine = std::sin(Angle);
    const Math::Vec2 corners[4]{
        {box.lower.x, box.lower.y},
        {box.upper.x, box.lower.y},
        {box.lower.x, box.upper.y},
        {box.upper.x, box.upper.y},
    };
    for (const Math::Vec2& corner : corners) {
        const Math::Vec2 rotated{
            translation.x + cosine * corner.x - sine * corner.y,
            translation.y + sine * corner.x + cosine * corner.y};
        // Expanded by the tolerance so the check does not fail on rounding alone.
        EXPECT_TRUE(Math::contains(Math::expand(bounds, Tolerance), rotated))
            << "corner (" << rotated.x << ", " << rotated.y << ") escaped the bounds";
    }
}

// The 45 degree case is where a conservative bound is strictly larger than the true
// one. Asserting that keeps someone from "tightening" it into an exact OBB bound and
// breaking the containment guarantee above.
TEST(MathGeometry2DTest, RotatedBoundsAreConservativeNotExact)
{
    const Math::Aabb2 unit =
        Math::fromCenterHalfExtents(Math::Vec2{}, Math::Vec2{1.0F, 1.0F});
    const Math::Aabb2 rotated = Math::rotatedBounds(unit, Math::radians(45.0F));
    // A unit square turned 45 degrees has half-extent sqrt(2), not 1.
    EXPECT_NEAR(Math::halfExtents(rotated).x, std::sqrt(2.0F), 1.0e-4F);
    EXPECT_GT(Math::area(rotated), Math::area(unit));
}

// Regression gate against the rotated-AABB arithmetic this replaced in
// src/asset/PhysicsNavigationSync2D.cpp. That code rasterized dynamic navigation
// blockers, and its published solid/blocked cell counts depend on exactly which
// cells the bounds cover — so the result must match element for element.
TEST(MathGeometry2DTest, RotatedBoundsMatchTheReplacedPhysicsNavigationArithmetic)
{
    const auto legacyProjectBody = [](const Math::Aabb2& localBounds,
                                       float angleRadians,
                                       Math::Vec2 position) {
        const float localCenterX = (localBounds.lower.x + localBounds.upper.x) * 0.5F;
        const float localCenterY = (localBounds.lower.y + localBounds.upper.y) * 0.5F;
        const float localHalfWidth = (localBounds.upper.x - localBounds.lower.x) * 0.5F;
        const float localHalfHeight = (localBounds.upper.y - localBounds.lower.y) * 0.5F;
        const float cosine = std::cos(angleRadians);
        const float sine = std::sin(angleRadians);
        const float worldCenterX = position.x + cosine * localCenterX - sine * localCenterY;
        const float worldCenterY = position.y + sine * localCenterX + cosine * localCenterY;
        const float worldHalfWidth =
            std::abs(cosine) * localHalfWidth + std::abs(sine) * localHalfHeight;
        const float worldHalfHeight =
            std::abs(sine) * localHalfWidth + std::abs(cosine) * localHalfHeight;
        return Math::Aabb2{
            {worldCenterX - worldHalfWidth, worldCenterY - worldHalfHeight},
            {worldCenterX + worldHalfWidth, worldCenterY + worldHalfHeight}};
    };

    const Math::Aabb2 boxes[]{
        {{-0.5F, -0.5F}, {0.5F, 0.5F}},
        {{0.0F, 0.0F}, {2.0F, 1.0F}},
        {{-3.0F, 1.0F}, {-1.0F, 4.0F}},
    };
    const Math::Vec2 positions[]{{0.0F, 0.0F}, {1.5F, 1.5F}, {-7.25F, 12.5F}};
    for (const Math::Aabb2& box : boxes) {
        for (const Math::Vec2& position : positions) {
            for (const float angle :
                 {0.0F, 0.25F, Math::HalfPi, 1.9F, Math::Pi, -0.6F, 5.5F}) {
                const Math::Aabb2 expected = legacyProjectBody(box, angle, position);
                const Math::Aabb2 actual = Math::rotatedBounds(box, angle, position);
                EXPECT_FLOAT_EQ(actual.lower.x, expected.lower.x) << "angle " << angle;
                EXPECT_FLOAT_EQ(actual.lower.y, expected.lower.y) << "angle " << angle;
                EXPECT_FLOAT_EQ(actual.upper.x, expected.upper.x) << "angle " << angle;
                EXPECT_FLOAT_EQ(actual.upper.y, expected.upper.y) << "angle " << angle;
            }
        }
    }
}

TEST(MathGeometry2DTest, RectAccessors)
{
    const Math::Rect rect{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_EQ(Math::origin(rect), (Math::Vec2{1.0F, 2.0F}));
    EXPECT_EQ(Math::size(rect), (Math::Vec2{3.0F, 4.0F}));
    EXPECT_EQ(Math::center(rect), (Math::Vec2{2.5F, 4.0F}));
    EXPECT_FLOAT_EQ(Math::right(rect), 4.0F);
    EXPECT_FLOAT_EQ(Math::bottom(rect), 6.0F);
}

TEST(MathGeometry2DTest, RectContainmentAndIntersection)
{
    const Math::Rect rect{0.0F, 0.0F, 10.0F, 5.0F};
    EXPECT_TRUE(Math::contains(rect, Math::Vec2{0.0F, 0.0F}));
    EXPECT_TRUE(Math::contains(rect, Math::Vec2{10.0F, 5.0F}));
    EXPECT_FALSE(Math::contains(rect, Math::Vec2{10.1F, 2.0F}));

    EXPECT_TRUE(Math::intersects(rect, Math::Rect{5.0F, 2.0F, 10.0F, 10.0F}));
    EXPECT_TRUE(Math::intersects(rect, Math::Rect{10.0F, 0.0F, 1.0F, 1.0F}));
    EXPECT_FALSE(Math::intersects(rect, Math::Rect{10.5F, 0.0F, 1.0F, 1.0F}));
}

// Rect and Aabb2 are the same size but mean different things, so conversion is
// explicit. An implicit one would read a width as an x coordinate.
TEST(MathGeometry2DTest, RectAndAabbConvertBothWays)
{
    const Math::Rect rect{1.0F, 2.0F, 3.0F, 4.0F};
    const Math::Aabb2 box{{1.0F, 2.0F}, {4.0F, 6.0F}};
    EXPECT_EQ(Math::toAabb2(rect), box);
    EXPECT_EQ(Math::toRect(box), rect);
    EXPECT_EQ(Math::toRect(Math::toAabb2(rect)), rect);
}

TEST(MathGeometry2DTest, RectValidityRejectsNegativeExtentAndNonFinite)
{
    EXPECT_TRUE(Math::isValid(Math::Rect{}));
    EXPECT_TRUE(Math::isValid(Math::Rect{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(Math::isValid(Math::Rect{0.0F, 0.0F, -1.0F, 4.0F}));
    EXPECT_FALSE(Math::isValid(Math::Rect{0.0F, 0.0F, 1.0F, QuietNaN}));
}

} // namespace Tina::Tests
