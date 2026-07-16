#include <gtest/gtest.h>

#include "engine/PerspectiveProjection.hpp"

#include <cmath>

namespace Tina::Engine {
namespace {

TEST(Camera3DTest, SixtyDegreeFieldOfViewIsPassedToBxInDegrees)
{
    float projection[16]{};
    constexpr float aspect = 16.0f / 9.0f;
    buildRightHandedPerspective(
        projection, 60.0f, aspect, 0.1f, 100.0f, true);

    const float expectedYScale = 1.0f / std::tan(bx::toRad(60.0f) * 0.5f);
    EXPECT_NEAR(projection[5], expectedYScale, 0.001f);
    EXPECT_NEAR(projection[0], expectedYScale / aspect, 0.001f);
    EXPECT_LT(projection[5], 2.0f)
        << "A radians-as-degrees regression produces an extreme scale near 109";
}

} // namespace
} // namespace Tina::Engine
