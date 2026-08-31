#include <gtest/gtest.h>

#include <tina/math/Quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

constexpr float Tolerance = 1.0e-5F;
constexpr float Infinity = std::numeric_limits<float>::infinity();
const float QuietNaN = std::numeric_limits<float>::quiet_NaN();
const Math::Quaternion Zero{0.0F, 0.0F, 0.0F, 0.0F};

void expectVectorNear(Math::Vec3 actual, Math::Vec3 expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

} // namespace

TEST(MathQuaternionTest, DefaultIsIdentity)
{
    EXPECT_EQ(Math::Quaternion{}, (Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(Math::isIdentity(Math::Quaternion{}));
    expectVectorNear(Math::rotate(Math::Quaternion{}, Math::Vec3{1.0F, 2.0F, 3.0F}),
                     Math::Vec3{1.0F, 2.0F, 3.0F});
}

TEST(MathQuaternionTest, AxisAngleRotatesAboutTheAxis)
{
    const Math::Quaternion aboutZ =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi);
    // +X rotated 90 degrees about +Z lands on +Y (right-handed).
    expectVectorNear(Math::rotate(aboutZ, Math::Vec3{1.0F, 0.0F, 0.0F}),
                     Math::Vec3{0.0F, 1.0F, 0.0F});
    // The axis itself is unchanged.
    expectVectorNear(Math::rotate(aboutZ, Math::Vec3{0.0F, 0.0F, 5.0F}),
                     Math::Vec3{0.0F, 0.0F, 5.0F});
}

TEST(MathQuaternionTest, AxisAngleNormalizesTheAxis)
{
    const Math::Quaternion fromUnit =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.7F);
    const Math::Quaternion fromScaled =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 9.0F}, 0.7F);
    EXPECT_NEAR(fromUnit.z, fromScaled.z, Tolerance);
    EXPECT_NEAR(fromUnit.w, fromScaled.w, Tolerance);
}

// A degenerate axis cannot describe a rotation, so it yields zero rather than
// identity: identity would silently turn invalid input into "no rotation".
TEST(MathQuaternionTest, AxisAngleRejectsDegenerateAxis)
{
    EXPECT_EQ(Math::fromAxisAngle(Math::Vec3{}, 1.0F), Zero);
    EXPECT_EQ(Math::fromAxisAngle(Math::Vec3{QuietNaN, 0.0F, 0.0F}, 1.0F), Zero);
}

// Rotations compose left-to-right: (parent * local) applies local first. This is
// the order Scene transform composition relies on.
TEST(MathQuaternionTest, MultiplicationAppliesRightOperandFirst)
{
    const Math::Quaternion aboutZ =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi);
    const Math::Quaternion aboutX =
        Math::fromAxisAngle(Math::Vec3{1.0F, 0.0F, 0.0F}, Math::HalfPi);

    const Math::Vec3 start{1.0F, 0.0F, 0.0F};
    expectVectorNear(Math::rotate(aboutX * aboutZ, start),
                     Math::rotate(aboutX, Math::rotate(aboutZ, start)));
    // Order matters; the two compositions are genuinely different rotations.
    EXPECT_FALSE(Math::approximatelyEqual(Math::rotate(aboutX * aboutZ, start),
                                          Math::rotate(aboutZ * aboutX, start)));
}

TEST(MathQuaternionTest, ConjugateAndInverseUndoRotation)
{
    const Math::Quaternion rotation =
        Math::fromAxisAngle(Math::Vec3{1.0F, 2.0F, 3.0F}, 0.9F);
    const Math::Vec3 point{4.0F, -5.0F, 6.0F};

    expectVectorNear(Math::rotate(Math::conjugate(rotation), Math::rotate(rotation, point)),
                     point);
    expectVectorNear(Math::rotate(Math::inverse(rotation), Math::rotate(rotation, point)),
                     point);
}

// inverse() divides by squared length, so it is also correct for non-unit input
// where conjugate() alone would not be.
TEST(MathQuaternionTest, InverseHandlesNonUnitInput)
{
    const Math::Quaternion scaled{0.0F, 0.0F, 2.0F, 2.0F};
    const Math::Quaternion product = scaled * Math::inverse(scaled);
    EXPECT_NEAR(product.x, 0.0F, Tolerance);
    EXPECT_NEAR(product.y, 0.0F, Tolerance);
    EXPECT_NEAR(product.z, 0.0F, Tolerance);
    EXPECT_NEAR(product.w, 1.0F, Tolerance);
}

TEST(MathQuaternionTest, NormalizedRejectsDegenerateInputWithZero)
{
    EXPECT_EQ(Math::normalized(Zero), Zero);
    EXPECT_EQ(Math::normalized(Math::Quaternion{QuietNaN, 0.0F, 0.0F, 1.0F}), Zero);
    EXPECT_EQ(Math::normalized(Math::Quaternion{Infinity, 0.0F, 0.0F, 1.0F}), Zero);
    EXPECT_EQ(Math::inverse(Zero), Zero);
}

TEST(MathQuaternionTest, NormalizedProducesUnitLength)
{
    const Math::Quaternion unit = Math::normalized(Math::Quaternion{1.0F, 2.0F, 3.0F, 4.0F});
    EXPECT_NEAR(Math::lengthSquared(unit), 1.0F, Tolerance);
}

TEST(MathQuaternionTest, IsIdentityAcceptsNegatedIdentity)
{
    // q and -q are the same rotation, so both count as identity.
    EXPECT_TRUE(Math::isIdentity(Math::Quaternion{0.0F, 0.0F, 0.0F, -1.0F}));
    EXPECT_FALSE(Math::isIdentity(
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.5F)));
    EXPECT_FALSE(Math::isIdentity(Zero));
    EXPECT_FALSE(Math::isIdentity(Math::Quaternion{QuietNaN, 0.0F, 0.0F, 1.0F}));
}

TEST(MathQuaternionTest, SlerpHitsBothEndpoints)
{
    const Math::Quaternion from =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.2F);
    const Math::Quaternion to =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 1.4F);

    const Math::Quaternion atStart = Math::slerp(from, to, 0.0F);
    const Math::Quaternion atEnd = Math::slerp(from, to, 1.0F);
    EXPECT_NEAR(atStart.z, from.z, Tolerance);
    EXPECT_NEAR(atStart.w, from.w, Tolerance);
    EXPECT_NEAR(atEnd.z, to.z, Tolerance);
    EXPECT_NEAR(atEnd.w, to.w, Tolerance);
}

TEST(MathQuaternionTest, SlerpMidpointBisectsTheAngle)
{
    const Math::Quaternion from =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.0F);
    const Math::Quaternion to =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi);
    const Math::Quaternion expected =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi * 0.5F);

    const Math::Quaternion midpoint = Math::slerp(from, to, 0.5F);
    EXPECT_NEAR(midpoint.z, expected.z, Tolerance);
    EXPECT_NEAR(midpoint.w, expected.w, Tolerance);
    EXPECT_NEAR(Math::lengthSquared(midpoint), 1.0F, Tolerance);
}

// With a negative dot the target is flipped, so interpolation takes the short arc.
// Without that flip the midpoint would rotate roughly the long way around.
TEST(MathQuaternionTest, SlerpTakesTheShortestPath)
{
    const Math::Quaternion from =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.1F);
    const Math::Quaternion to =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.3F);
    // Negating produces the same rotation but a negative dot product with `from`.
    const Math::Quaternion negatedTo{-to.x, -to.y, -to.z, -to.w};

    const Math::Quaternion viaPositive = Math::slerp(from, to, 0.5F);
    const Math::Quaternion viaNegative = Math::slerp(from, negatedTo, 0.5F);
    EXPECT_NEAR(viaPositive.z, viaNegative.z, Tolerance);
    EXPECT_NEAR(viaPositive.w, viaNegative.w, Tolerance);
}

// Nearly parallel inputs take the normalized-lerp branch, where slerp's sin(angle)
// divisor approaches zero. The result must still be unit length and near both ends.
TEST(MathQuaternionTest, SlerpFallsBackToLerpForNearlyParallelInput)
{
    const Math::Quaternion from =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.5F);
    const Math::Quaternion to =
        Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.5F + 1.0e-5F);
    ASSERT_GT(static_cast<double>(from.x) * to.x + static_cast<double>(from.y) * to.y
                  + static_cast<double>(from.z) * to.z + static_cast<double>(from.w) * to.w,
              0.9995)
        << "test premise: the inputs are inside the lerp fallback threshold";

    const Math::Quaternion midpoint = Math::slerp(from, to, 0.5F);
    EXPECT_NEAR(Math::lengthSquared(midpoint), 1.0F, Tolerance);
    EXPECT_NEAR(midpoint.z, from.z, Tolerance);
    EXPECT_NEAR(midpoint.w, from.w, Tolerance);
}

TEST(MathQuaternionTest, SlerpOfIdenticalRotationsIsStable)
{
    const Math::Quaternion rotation =
        Math::fromAxisAngle(Math::Vec3{1.0F, 1.0F, 0.0F}, 1.1F);
    const Math::Quaternion result = Math::slerp(rotation, rotation, 0.5F);
    EXPECT_NEAR(result.x, rotation.x, Tolerance);
    EXPECT_NEAR(result.y, rotation.y, Tolerance);
    EXPECT_NEAR(result.z, rotation.z, Tolerance);
    EXPECT_NEAR(result.w, rotation.w, Tolerance);
}

// Regression gate against the shortestPathSlerp this replaced in
// src/scene/Animator3D.cpp. That function produced the published skinned-pose
// evidence, so any divergence here would show up as animation drift rather than a
// build failure. The body is reproduced verbatim, double accumulation included.
TEST(MathQuaternionTest, SlerpMatchesTheReplacedAnimatorImplementation)
{
    const auto legacyShortestPathSlerp =
        [](Math::Quaternion from, Math::Quaternion to, float alpha) {
            from = Math::normalized(from);
            to = Math::normalized(to);
            double cosine = static_cast<double>(from.x) * to.x
                + static_cast<double>(from.y) * to.y
                + static_cast<double>(from.z) * to.z
                + static_cast<double>(from.w) * to.w;
            if (cosine < 0.0) {
                to = {-to.x, -to.y, -to.z, -to.w};
                cosine = -cosine;
            }
            cosine = std::clamp(cosine, 0.0, 1.0);
            if (cosine > 0.9995) {
                return Math::normalized(Math::Quaternion{
                    from.x + (to.x - from.x) * alpha,
                    from.y + (to.y - from.y) * alpha,
                    from.z + (to.z - from.z) * alpha,
                    from.w + (to.w - from.w) * alpha,
                });
            }
            const double angle = std::acos(cosine);
            const double sine = std::sin(angle);
            const double fromScale =
                std::sin((1.0 - static_cast<double>(alpha)) * angle) / sine;
            const double toScale = std::sin(static_cast<double>(alpha) * angle) / sine;
            return Math::normalized(Math::Quaternion{
                static_cast<float>(fromScale * from.x + toScale * to.x),
                static_cast<float>(fromScale * from.y + toScale * to.y),
                static_cast<float>(fromScale * from.z + toScale * to.z),
                static_cast<float>(fromScale * from.w + toScale * to.w),
            });
        };

    // Includes the near-parallel pair that exercises the lerp fallback branch and
    // an opposed pair that exercises the shortest-path flip.
    const Math::Quaternion pairs[][2]{
        {Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.0F),
            Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, Math::HalfPi)},
        {Math::fromAxisAngle(Math::Vec3{1.0F, 2.0F, 3.0F}, 0.3F),
            Math::fromAxisAngle(Math::Vec3{-2.0F, 1.0F, 0.5F}, 2.7F)},
        {Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, 0.5F),
            Math::fromAxisAngle(Math::Vec3{0.0F, 1.0F, 0.0F}, 0.5F + 1.0e-6F)},
        {Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.2F),
            Math::fromAxisAngle(Math::Vec3{0.0F, 0.0F, 1.0F}, 0.2F + Math::Pi * 1.9F)},
    };
    for (const auto& pair : pairs) {
        for (const float alpha : {0.0F, 0.125F, 0.25F, 0.5F, 0.75F, 1.0F}) {
            const Math::Quaternion expected =
                legacyShortestPathSlerp(pair[0], pair[1], alpha);
            const Math::Quaternion actual = Math::slerp(pair[0], pair[1], alpha);
            EXPECT_FLOAT_EQ(actual.x, expected.x) << "alpha " << alpha;
            EXPECT_FLOAT_EQ(actual.y, expected.y) << "alpha " << alpha;
            EXPECT_FLOAT_EQ(actual.z, expected.z) << "alpha " << alpha;
            EXPECT_FLOAT_EQ(actual.w, expected.w) << "alpha " << alpha;
        }
    }
}

TEST(MathQuaternionTest, IsFiniteRejectsNonFiniteComponents)
{
    EXPECT_TRUE(Math::isFinite(Math::Quaternion{}));
    EXPECT_FALSE(Math::isFinite(Math::Quaternion{0.0F, 0.0F, 0.0F, QuietNaN}));
    EXPECT_FALSE(Math::isFinite(Math::Quaternion{Infinity, 0.0F, 0.0F, 1.0F}));
}

} // namespace Tina::Tests
