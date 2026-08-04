#include "BgfxDirectionalShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
        .shadowDistanceMeters = 40.0F,
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
    auto input = BgfxDirectionalShadowBoundsInput{.camera = camera(), .shadowDistanceMeters = 0.05F};
    const auto result = computeDirectionalShadowBounds(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

TEST(BgfxDirectionalShadowMathTest, ProjectionProducesFiniteViewAndSamplingTransforms)
{
    const auto result = computeDirectionalShadowProjection(
        BgfxDirectionalShadowBoundsInput{
            .camera = camera(),
            .light = {.directionTowardLightX = -0.4F,
                      .directionTowardLightY = 0.8F,
                      .directionTowardLightZ = 0.3F},
            .shadowDistanceMeters = 40.0F,
            .depthPadding = 6.0F,
        },
        false,
        false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto allFinite = [](const std::array<float, 16>& matrix) {
        return std::ranges::all_of(matrix,
                                   [](float value) { return std::isfinite(value); });
    };
    EXPECT_TRUE(allFinite(result->lightView));
    EXPECT_TRUE(allFinite(result->lightProjection));
    EXPECT_TRUE(allFinite(result->samplingTransform));
    EXPECT_GT(result->bounds.width(), 0.0F);
    EXPECT_GT(result->bounds.height(), 0.0F);
    EXPECT_GT(result->bounds.depth(), 0.0F);
}

TEST(BgfxDirectionalShadowMathTest, SamplingTransformAccountsForFramebufferOrigin)
{
    const BgfxDirectionalShadowBoundsInput input{
        .camera = camera(),
        .light = {.directionTowardLightX = -0.4F,
                  .directionTowardLightY = 0.8F,
                  .directionTowardLightZ = 0.3F},
        .shadowDistanceMeters = 40.0F,
    };
    const auto topLeft =
        computeDirectionalShadowProjection(input, false, false);
    const auto bottomLeft =
        computeDirectionalShadowProjection(input, false, true);

    ASSERT_TRUE(topLeft.has_value()) << topLeft.error().message;
    ASSERT_TRUE(bottomLeft.has_value()) << bottomLeft.error().message;
    EXPECT_EQ(topLeft->bounds.width(), bottomLeft->bounds.width());
    EXPECT_EQ(topLeft->lightView, bottomLeft->lightView);
    EXPECT_EQ(topLeft->lightProjection, bottomLeft->lightProjection);
    EXPECT_NE(topLeft->samplingTransform, bottomLeft->samplingTransform);
}

} // namespace
} // namespace Tina::Render::Bgfx
