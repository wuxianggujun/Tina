#include <gtest/gtest.h>

#include <tina/math/Vec.hpp>

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-6F;
constexpr float Infinity = std::numeric_limits<float>::infinity();
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();

} // namespace

TEST(MathVecTest, ArithmeticIsComponentWise)
{
    const Math::Vec3 left{1.0F, 2.0F, 3.0F};
    const Math::Vec3 right{4.0F, 5.0F, 6.0F};

    EXPECT_EQ(left + right, (Math::Vec3{5.0F, 7.0F, 9.0F}));
    EXPECT_EQ(right - left, (Math::Vec3{3.0F, 3.0F, 3.0F}));
    EXPECT_EQ(-left, (Math::Vec3{-1.0F, -2.0F, -3.0F}));
    EXPECT_EQ(left * 2.0F, (Math::Vec3{2.0F, 4.0F, 6.0F}));
    EXPECT_EQ(2.0F * left, (Math::Vec3{2.0F, 4.0F, 6.0F}));
    EXPECT_EQ(left / 2.0F, (Math::Vec3{0.5F, 1.0F, 1.5F}));
}

// Vec * Vec is the componentwise product, used for non-uniform scale. It is not a
// dot product; a caller reading it as one would get a Vec3 back and not compile.
TEST(MathVecTest, VectorProductIsComponentWiseNotDot)
{
    EXPECT_EQ((Math::Vec3{2.0F, 3.0F, 4.0F} * Math::Vec3{5.0F, 6.0F, 7.0F}),
              (Math::Vec3{10.0F, 18.0F, 28.0F}));
    EXPECT_EQ((Math::Vec2{2.0F, 3.0F} * Math::Vec2{4.0F, 5.0F}), (Math::Vec2{8.0F, 15.0F}));
}

TEST(MathVecTest, DotAndCrossFollowRightHandedConvention)
{
    EXPECT_FLOAT_EQ(Math::dot(Math::Vec3{1.0F, 2.0F, 3.0F}, Math::Vec3{4.0F, 5.0F, 6.0F}), 32.0F);
    EXPECT_FLOAT_EQ(Math::dot(Math::Vec2{1.0F, 2.0F}, Math::Vec2{3.0F, 4.0F}), 11.0F);

    // X cross Y is +Z in a right-handed basis.
    EXPECT_EQ(Math::cross(Math::Vec3{1.0F, 0.0F, 0.0F}, Math::Vec3{0.0F, 1.0F, 0.0F}),
              (Math::Vec3{0.0F, 0.0F, 1.0F}));
    // Anticommutative.
    EXPECT_EQ(Math::cross(Math::Vec3{0.0F, 1.0F, 0.0F}, Math::Vec3{1.0F, 0.0F, 0.0F}),
              (Math::Vec3{0.0F, 0.0F, -1.0F}));
    // Orthogonal to both operands.
    const Math::Vec3 a{1.0F, 2.0F, 3.0F};
    const Math::Vec3 b{-4.0F, 5.0F, 6.0F};
    const Math::Vec3 product = Math::cross(a, b);
    EXPECT_NEAR(Math::dot(product, a), 0.0F, Tolerance);
    EXPECT_NEAR(Math::dot(product, b), 0.0F, Tolerance);
}

TEST(MathVecTest, CrossZMatchesThreeDimensionalCrossInThePlane)
{
    const Math::Vec2 left{2.0F, 3.0F};
    const Math::Vec2 right{4.0F, 5.0F};
    const Math::Vec3 lifted =
        Math::cross(Math::toVec3(left), Math::toVec3(right));
    EXPECT_FLOAT_EQ(Math::crossZ(left, right), lifted.z);
    // Positive when the second vector is counter-clockwise from the first.
    EXPECT_GT(Math::crossZ(Math::Vec2{1.0F, 0.0F}, Math::Vec2{0.0F, 1.0F}), 0.0F);
}

TEST(MathVecTest, LengthAndDistance)
{
    EXPECT_FLOAT_EQ(Math::lengthSquared(Math::Vec3{3.0F, 4.0F, 0.0F}), 25.0F);
    EXPECT_FLOAT_EQ(Math::length(Math::Vec3{3.0F, 4.0F, 0.0F}), 5.0F);
    EXPECT_FLOAT_EQ(Math::length(Math::Vec2{3.0F, 4.0F}), 5.0F);
    EXPECT_FLOAT_EQ(Math::distance(Math::Vec3{1.0F, 1.0F, 1.0F}, Math::Vec3{4.0F, 5.0F, 1.0F}),
                    5.0F);
    EXPECT_FLOAT_EQ(
        Math::distanceSquared(Math::Vec2{1.0F, 1.0F}, Math::Vec2{4.0F, 5.0F}), 25.0F);
}

TEST(MathVecTest, NormalizedProducesUnitLength)
{
    const Math::Vec3 unit = Math::normalized(Math::Vec3{0.0F, 3.0F, 4.0F});
    EXPECT_NEAR(Math::length(unit), 1.0F, Tolerance);
    EXPECT_NEAR(unit.y, 0.6F, Tolerance);
    EXPECT_NEAR(unit.z, 0.8F, Tolerance);

    const Math::Vec2 unit2 = Math::normalized(Math::Vec2{3.0F, 4.0F});
    EXPECT_NEAR(Math::length(unit2), 1.0F, Tolerance);
}

// A degenerate or non-finite input yields the zero vector, never NaN and never a
// silently unnormalized value. Callers test against zero to detect the failure.
TEST(MathVecTest, NormalizedRejectsDegenerateInputWithZero)
{
    EXPECT_EQ(Math::normalized(Math::Vec3{}), (Math::Vec3{}));
    EXPECT_EQ(Math::normalized(Math::Vec2{}), (Math::Vec2{}));
    EXPECT_EQ(Math::normalized(Math::Vec3{QuietNaN, 0.0F, 0.0F}), (Math::Vec3{}));
    EXPECT_EQ(Math::normalized(Math::Vec3{Infinity, 0.0F, 0.0F}), (Math::Vec3{}));
}

// Accumulating the squared length in double is what keeps large magnitudes
// normalizable: 1e20 squares to 1e40, which is +inf in float and would reject a
// perfectly ordinary vector.
TEST(MathVecTest, NormalizedHandlesMagnitudesWhoseFloatSquareOverflows)
{
    // Read through a volatile so the square is evaluated at runtime; folding it at
    // compile time is a constant-overflow diagnostic, not a usable premise check.
    const volatile float huge = 1.0e20F;
    const float Huge = huge;
    ASSERT_FALSE(std::isfinite(Huge * Huge)) << "test premise: the float square overflows";

    const Math::Vec3 unit = Math::normalized(Math::Vec3{Huge, 0.0F, 0.0F});
    EXPECT_EQ(unit, (Math::Vec3{1.0F, 0.0F, 0.0F}));
    EXPECT_NEAR(Math::length(Math::normalized(Math::Vec3{Huge, Huge, Huge})), 1.0F, Tolerance);
}

// Below the threshold a vector is reported as degenerate even though float could
// still represent its direction. The cutoff is on the SQUARED length, so it sits at
// 1e-12 — at meter scale such a vector carries no usable direction, and the same
// constant defines the quaternion contract Scene transform validation has always
// used. Asserting it here keeps the boundary from drifting silently.
TEST(MathVecTest, NormalizedTreatsVectorsBelowTheThresholdAsDegenerate)
{
    static_assert(Math::MinimumNormalizableLengthSquared == 1.0e-24);

    // Comfortably above the cutoff: 1e-5 squared is 1e-10.
    EXPECT_EQ(Math::normalized(Math::Vec3{1.0e-5F, 0.0F, 0.0F}),
              (Math::Vec3{1.0F, 0.0F, 0.0F}));
    // Comfortably below: 1e-30 squared is 1e-60.
    EXPECT_EQ(Math::normalized(Math::Vec3{1.0e-30F, 0.0F, 0.0F}), (Math::Vec3{}));
    EXPECT_EQ(Math::normalized(Math::Vec2{1.0e-30F, 0.0F}), (Math::Vec2{}));
}

TEST(MathVecTest, LerpAndComponentWiseHelpers)
{
    EXPECT_EQ(Math::lerp(Math::Vec3{0.0F, 0.0F, 0.0F}, Math::Vec3{10.0F, 20.0F, 30.0F}, 0.5F),
              (Math::Vec3{5.0F, 10.0F, 15.0F}));
    EXPECT_EQ(Math::minimum(Math::Vec3{1.0F, 5.0F, 3.0F}, Math::Vec3{4.0F, 2.0F, 6.0F}),
              (Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(Math::maximum(Math::Vec3{1.0F, 5.0F, 3.0F}, Math::Vec3{4.0F, 2.0F, 6.0F}),
              (Math::Vec3{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(Math::absolute(Math::Vec3{-1.0F, 2.0F, -3.0F}), (Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_FLOAT_EQ(Math::largestComponent(Math::Vec3{-1.0F, 7.0F, 3.0F}), 7.0F);
    EXPECT_FLOAT_EQ(Math::smallestComponent(Math::Vec3{-1.0F, 7.0F, 3.0F}), -1.0F);
}

TEST(MathVecTest, ConversionsPreserveComponents)
{
    EXPECT_EQ(Math::xy(Math::Vec3{1.0F, 2.0F, 3.0F}), (Math::Vec2{1.0F, 2.0F}));
    EXPECT_EQ(Math::xyz(Math::Vec4{1.0F, 2.0F, 3.0F, 4.0F}), (Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(Math::toVec3(Math::Vec2{1.0F, 2.0F}), (Math::Vec3{1.0F, 2.0F, 0.0F}));
    EXPECT_EQ(Math::toVec3(Math::Vec2{1.0F, 2.0F}, 9.0F), (Math::Vec3{1.0F, 2.0F, 9.0F}));
}

// w distinguishes a point from a direction, which is what decides whether a Mat4
// translation applies. Defaulting Vec4 to w = 0 makes it a direction.
TEST(MathVecTest, PointAndDirectionDifferOnlyInW)
{
    EXPECT_EQ(Math::toPoint(Math::Vec3{1.0F, 2.0F, 3.0F}),
              (Math::Vec4{1.0F, 2.0F, 3.0F, 1.0F}));
    EXPECT_EQ(Math::toDirection(Math::Vec3{1.0F, 2.0F, 3.0F}),
              (Math::Vec4{1.0F, 2.0F, 3.0F, 0.0F}));
    EXPECT_FLOAT_EQ(Math::Vec4{}.w, 0.0F);
}

TEST(MathVecTest, IsFiniteRejectsNonFiniteComponents)
{
    EXPECT_TRUE(Math::isFinite(Math::Vec3{1.0F, 2.0F, 3.0F}));
    EXPECT_FALSE(Math::isFinite(Math::Vec3{1.0F, QuietNaN, 3.0F}));
    EXPECT_FALSE(Math::isFinite(Math::Vec3{1.0F, 2.0F, Infinity}));
    EXPECT_FALSE(Math::isFinite(Math::Vec2{QuietNaN, 0.0F}));
    EXPECT_FALSE(Math::isFinite(Math::Vec4{0.0F, 0.0F, 0.0F, Infinity}));
}

// Comparison is scale-relative, so it stays meaningful for large coordinates where
// a fixed absolute epsilon would reject values differing in their last bits.
TEST(MathVecTest, ApproximatelyEqualScalesWithMagnitude)
{
    EXPECT_TRUE(Math::approximatelyEqual(1.0F, 1.0F + 1.0e-7F));
    EXPECT_FALSE(Math::approximatelyEqual(1.0F, 1.1F));
    EXPECT_TRUE(Math::approximatelyEqual(1.0e6F, 1.0e6F + 1.0F));
    EXPECT_FALSE(Math::approximatelyEqual(QuietNaN, QuietNaN));
    EXPECT_TRUE(Math::approximatelyEqual(Math::Vec3{1.0F, 2.0F, 3.0F},
                                         Math::Vec3{1.0F, 2.0F, 3.0F + 1.0e-7F}));
    EXPECT_FALSE(Math::approximatelyEqual(Math::Vec2{1.0F, 2.0F}, Math::Vec2{1.0F, 2.5F}));
}

} // namespace Tina::Tests
