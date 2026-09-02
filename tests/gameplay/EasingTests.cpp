#include <tina/gameplay/Easing.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace Tina::Gameplay {
namespace {

constexpr std::array<Easing, 28> AllCurves{
    Easing::Linear,
    Easing::QuadraticIn,      Easing::QuadraticOut,      Easing::QuadraticInOut,
    Easing::CubicIn,          Easing::CubicOut,          Easing::CubicInOut,
    Easing::QuarticIn,        Easing::QuarticOut,        Easing::QuarticInOut,
    Easing::SineIn,           Easing::SineOut,           Easing::SineInOut,
    Easing::ExponentialIn,    Easing::ExponentialOut,    Easing::ExponentialInOut,
    Easing::CircularIn,       Easing::CircularOut,       Easing::CircularInOut,
    Easing::BackIn,           Easing::BackOut,           Easing::BackInOut,
    Easing::ElasticIn,        Easing::ElasticOut,        Easing::ElasticInOut,
    Easing::BounceIn,         Easing::BounceOut,         Easing::BounceInOut,
};

// Every InOut curve passes through the midpoint at the midpoint. Asserted for all
// nine at once because the two halves of an InOut are written as separate
// expressions, and a sign or coefficient error in one half shows up here rather
// than only in a visual review.
constexpr std::array<Easing, 9> InOutCurves{
    Easing::QuadraticInOut, Easing::CubicInOut,       Easing::QuarticInOut,
    Easing::SineInOut,      Easing::ExponentialInOut, Easing::CircularInOut,
    Easing::BackInOut,      Easing::ElasticInOut,     Easing::BounceInOut,
};

// Curves that must never go backwards. Back/Elastic/Bounce are excluded because
// overshooting and bouncing is the whole reason they exist.
constexpr std::array<Easing, 19> MonotonicCurves{
    Easing::Linear,
    Easing::QuadraticIn,   Easing::QuadraticOut,   Easing::QuadraticInOut,
    Easing::CubicIn,       Easing::CubicOut,       Easing::CubicInOut,
    Easing::QuarticIn,     Easing::QuarticOut,     Easing::QuarticInOut,
    Easing::SineIn,        Easing::SineOut,        Easing::SineInOut,
    Easing::ExponentialIn, Easing::ExponentialOut, Easing::ExponentialInOut,
    Easing::CircularIn,    Easing::CircularOut,    Easing::CircularInOut,
};

// Curves whose defining feature is leaving [0,1] partway through.
constexpr std::array<Easing, 6> OvershootCurves{
    Easing::BackIn,    Easing::BackOut,    Easing::BackInOut,
    Easing::ElasticIn, Easing::ElasticOut, Easing::ElasticInOut,
};

} // namespace

// The endpoints are returned unchanged rather than run through the formula. This is
// what puts a tween on its authored target: ExponentialIn's own formula evaluates to
// 2^-10 at t=0, so a curve that trusted the formula would start a hair off zero and
// end a hair short of one.
TEST(EasingTests, EndpointsAreExactForEveryCurve)
{
    for (const Easing curve : AllCurves) {
        EXPECT_FLOAT_EQ(evaluateEasing(curve, 0.0F), 0.0F)
            << "curve " << static_cast<int>(curve);
        EXPECT_FLOAT_EQ(evaluateEasing(curve, 1.0F), 1.0F)
            << "curve " << static_cast<int>(curve);
    }
}

// Progress outside [0,1] is clamped to the endpoints rather than extrapolated. A
// caller that accumulated slightly past its duration must not drive the setter past
// the authored target.
TEST(EasingTests, ProgressOutsideTheUnitRangeClampsToEndpoints)
{
    for (const Easing curve : AllCurves) {
        EXPECT_FLOAT_EQ(evaluateEasing(curve, -0.5F), 0.0F)
            << "curve " << static_cast<int>(curve);
        EXPECT_FLOAT_EQ(evaluateEasing(curve, 1.5F), 1.0F)
            << "curve " << static_cast<int>(curve);
    }
}

// Non-finite progress is treated as a finished tween instead of being propagated.
// Returning NaN would write NaN into a gameplay value through the setter, and the
// resulting corruption surfaces arbitrarily far from the tween that caused it.
TEST(EasingTests, NonFiniteProgressIsTreatedAsFinished)
{
    constexpr float quietNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float infinity = std::numeric_limits<float>::infinity();

    for (const Easing curve : AllCurves) {
        EXPECT_FLOAT_EQ(evaluateEasing(curve, quietNaN), 1.0F)
            << "curve " << static_cast<int>(curve);
        EXPECT_FLOAT_EQ(evaluateEasing(curve, infinity), 1.0F)
            << "curve " << static_cast<int>(curve);
        // Negative infinity is finite-checked before the <= 0 branch, so it also
        // reports finished rather than 0.
        EXPECT_FLOAT_EQ(evaluateEasing(curve, -infinity), 1.0F)
            << "curve " << static_cast<int>(curve);
    }
}

TEST(EasingTests, EveryCurveIsFiniteAcrossTheUnitRange)
{
    for (const Easing curve : AllCurves) {
        for (int step = 0; step <= 100; ++step) {
            const float t = static_cast<float>(step) / 100.0F;
            EXPECT_TRUE(std::isfinite(evaluateEasing(curve, t)))
                << "curve " << static_cast<int>(curve) << " at t=" << t;
        }
    }
}

TEST(EasingTests, NonOvershootingCurvesNeverGoBackwards)
{
    for (const Easing curve : MonotonicCurves) {
        float previous = evaluateEasing(curve, 0.0F);
        for (int step = 1; step <= 1000; ++step) {
            const float t = static_cast<float>(step) / 1000.0F;
            const float current = evaluateEasing(curve, t);
            EXPECT_GE(current, previous)
                << "curve " << static_cast<int>(curve) << " decreased at t=" << t;
            previous = current;
        }
    }
}

TEST(EasingTests, NonOvershootingCurvesStayWithinTheUnitRange)
{
    for (const Easing curve : MonotonicCurves) {
        for (int step = 0; step <= 1000; ++step) {
            const float t = static_cast<float>(step) / 1000.0F;
            const float value = evaluateEasing(curve, t);
            EXPECT_GE(value, 0.0F) << "curve " << static_cast<int>(curve) << " at t=" << t;
            EXPECT_LE(value, 1.0F) << "curve " << static_cast<int>(curve) << " at t=" << t;
        }
    }
}

// Back and Elastic must actually leave [0,1] somewhere in the middle. Asserted
// rather than assumed: a Back curve whose overshoot coefficient was dropped still
// starts at 0 and ends at 1, so the endpoint tests above would not notice, and the
// visible symptom is only that the motion looks flat.
TEST(EasingTests, OvershootingCurvesLeaveTheUnitRangeMidRun)
{
    for (const Easing curve : OvershootCurves) {
        bool leftRange = false;
        for (int step = 1; step < 1000; ++step) {
            const float t = static_cast<float>(step) / 1000.0F;
            const float value = evaluateEasing(curve, t);
            if (value < 0.0F || value > 1.0F) {
                leftRange = true;
                break;
            }
        }
        EXPECT_TRUE(leftRange) << "curve " << static_cast<int>(curve) << " never overshot";
    }
}

// Bounce stays inside [0,1] but is not monotonic: each landing is a local maximum
// followed by a dip. Both halves of that statement are asserted, because a bounce
// implemented as a monotonic ease-out looks like a slow stop rather than a bounce.
TEST(EasingTests, BounceStaysInRangeButIsNotMonotonic)
{
    constexpr std::array<Easing, 3> bounceCurves{
        Easing::BounceIn,
        Easing::BounceOut,
        Easing::BounceInOut,
    };

    for (const Easing curve : bounceCurves) {
        bool decreasedSomewhere = false;
        float previous = evaluateEasing(curve, 0.0F);
        for (int step = 1; step <= 1000; ++step) {
            const float t = static_cast<float>(step) / 1000.0F;
            const float current = evaluateEasing(curve, t);
            EXPECT_GE(current, 0.0F) << "curve " << static_cast<int>(curve) << " at t=" << t;
            EXPECT_LE(current, 1.0F) << "curve " << static_cast<int>(curve) << " at t=" << t;
            if (current < previous) {
                decreasedSomewhere = true;
            }
            previous = current;
        }
        EXPECT_TRUE(decreasedSomewhere)
            << "curve " << static_cast<int>(curve) << " never dipped, so it is not bouncing";
    }
}

TEST(EasingTests, InOutCurvesCrossTheMidpointAtTheMidpoint)
{
    for (const Easing curve : InOutCurves) {
        EXPECT_NEAR(evaluateEasing(curve, 0.5F), 0.5F, 1.0e-5F)
            << "curve " << static_cast<int>(curve);
    }
}

// An In curve is slow to start and an Out curve is quick to start. Stated as a
// comparison against Linear at the same progress, which is what distinguishes the
// two and is exactly what a transposed In/Out pair would get backwards.
TEST(EasingTests, InCurvesTrailLinearAndOutCurvesLeadIt)
{
    struct Pair final {
        Easing in;
        Easing out;
    };
    constexpr std::array<Pair, 6> pairs{
        Pair{Easing::QuadraticIn, Easing::QuadraticOut},
        Pair{Easing::CubicIn, Easing::CubicOut},
        Pair{Easing::QuarticIn, Easing::QuarticOut},
        Pair{Easing::SineIn, Easing::SineOut},
        Pair{Easing::ExponentialIn, Easing::ExponentialOut},
        Pair{Easing::CircularIn, Easing::CircularOut},
    };

    constexpr float quarter = 0.25F;
    for (const Pair pair : pairs) {
        EXPECT_LT(evaluateEasing(pair.in, quarter), quarter)
            << "curve " << static_cast<int>(pair.in) << " did not trail linear";
        EXPECT_GT(evaluateEasing(pair.out, quarter), quarter)
            << "curve " << static_cast<int>(pair.out) << " did not lead linear";
    }
}

TEST(EasingTests, ValidityFollowsTheEnumRangeAndRejectsCount)
{
    for (const Easing curve : AllCurves) {
        EXPECT_TRUE(isValidEasing(curve)) << "curve " << static_cast<int>(curve);
    }
    // Count is the range bound, not a curve. Accepting it would let a tween be
    // authored with an easing that falls through to the raw progress.
    EXPECT_FALSE(isValidEasing(Easing::Count));
    EXPECT_FALSE(isValidEasing(static_cast<Easing>(28)));
    EXPECT_FALSE(isValidEasing(static_cast<Easing>(255)));
}

// The enum values are part of authored content, so renumbering silently retargets
// every existing tween. Pinned here rather than left to a comment in the header.
TEST(EasingTests, EnumValuesArePinnedBecauseTheyAreAuthoredIntoContent)
{
    EXPECT_EQ(static_cast<u8>(Easing::Linear), 0U);
    EXPECT_EQ(static_cast<u8>(Easing::QuadraticIn), 1U);
    EXPECT_EQ(static_cast<u8>(Easing::CubicIn), 4U);
    EXPECT_EQ(static_cast<u8>(Easing::QuarticIn), 7U);
    EXPECT_EQ(static_cast<u8>(Easing::SineIn), 10U);
    EXPECT_EQ(static_cast<u8>(Easing::ExponentialIn), 13U);
    EXPECT_EQ(static_cast<u8>(Easing::CircularIn), 16U);
    EXPECT_EQ(static_cast<u8>(Easing::BackIn), 19U);
    EXPECT_EQ(static_cast<u8>(Easing::ElasticIn), 22U);
    EXPECT_EQ(static_cast<u8>(Easing::BounceIn), 25U);
    EXPECT_EQ(static_cast<u8>(Easing::Count), 28U);
}

} // namespace Tina::Gameplay
