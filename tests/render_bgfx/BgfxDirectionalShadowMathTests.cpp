#include "BgfxDirectionalShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] RenderPerspectiveCamera camera() noexcept
{
    return RenderPerspectiveCamera{
        .positionX = 0.0F,
        .positionY = 0.0F,
        .positionZ = 8.0F,
        .forwardX = 0.0F,
        .forwardY = 0.0F,
        .forwardZ = -1.0F,
        .upX = 0.0F,
        .upY = 1.0F,
        .upZ = 0.0F,
        .verticalFovDegrees = 60.0F,
        .nearPlaneMeters = 0.1F,
        .farPlaneMeters = 100.0F,
        .aspectRatio = 16.0F / 9.0F,
    };
}

TEST(BgfxDirectionalShadowMathTest, CameraSliceProducesFinitePositiveBounds)
{
    const auto result = computeDirectionalShadowBounds(BgfxDirectionalShadowBoundsInput{
        .camera = camera(),
        .light = Mesh3DDirectionalLight{.directionTowardLightX = 0.3F,
                                        .directionTowardLightY = 0.8F,
                                        .directionTowardLightZ = 0.4F},
        .shadowDistance = 40.0F,
        .depthPadding = 4.0F,
    });
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(std::isfinite(result->minX));
    EXPECT_TRUE(std::isfinite(result->maxX));
    EXPECT_GT(result->width(), 0.0F);
    EXPECT_GT(result->height(), 0.0F);
    EXPECT_GT(result->depth(), 0.0F);
}

TEST(BgfxDirectionalShadowMathTest, RejectsDegenerateLightAxis)
{
    auto input = BgfxDirectionalShadowBoundsInput{.camera = camera()};
    input.light.directionTowardLightX = 0.0F;
    input.light.directionTowardLightY = 0.0F;
    input.light.directionTowardLightZ = 0.0F;
    const auto result = computeDirectionalShadowBounds(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

TEST(BgfxDirectionalShadowMathTest, ShadowDistanceCannotEndBeforeCameraNearPlane)
{
    auto input = BgfxDirectionalShadowBoundsInput{.camera = camera(), .shadowDistance = 0.05F};
    const auto result = computeDirectionalShadowBounds(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

} // namespace
} // namespace Tina::Render::Bgfx
