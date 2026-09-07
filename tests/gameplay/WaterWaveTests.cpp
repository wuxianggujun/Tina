#include <tina/gameplay/WaterSurfaceAnimator.hpp>
#include <tina/render/WaterWaveUniforms.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <utility>

namespace {
using namespace Tina;

TEST(WaterWave, EvaluatesFinite2DAnd3DValues) {
    Gameplay::WaterWave2D wave{};
    auto height = Gameplay::evaluateWave(wave, {1.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(height);
    EXPECT_TRUE(std::isfinite(*height));
    auto position = Gameplay::evaluateWave3D(Gameplay::WaterWave3D{}, {0.0F, 0.0F, 1.0F}, 0.5F);
    ASSERT_TRUE(position);
    EXPECT_TRUE(Math::isFinite(*position));
}

TEST(WaterWave, AnimatorWrapsTimeAndBuildsNamedUniforms) {
    auto created = Gameplay::WaterSurfaceAnimator::Create({});
    ASSERT_TRUE(created);
    auto animator = std::move(*created);
    ASSERT_TRUE(animator.advance(5000.0F));
    EXPECT_LT(animator.params().timeSeconds, 4096.0F);
    auto uniforms = Render::makeWaterWaveUniforms(animator.params());
    ASSERT_TRUE(uniforms);
    EXPECT_EQ(uniforms->descriptor().values.size(), 4U);
    EXPECT_STREQ(uniforms->values[0].name.data(), "u_waterWave2DA");
}
}
