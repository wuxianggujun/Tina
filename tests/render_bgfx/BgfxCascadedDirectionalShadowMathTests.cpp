#include "BgfxCascadedDirectionalShadowMath.hpp"

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

[[nodiscard]] BgfxCascadedDirectionalShadowInput input() noexcept
{
    return BgfxCascadedDirectionalShadowInput{
        .camera = camera(),
        .light = {.directionTowardLightX = -0.4F,
                  .directionTowardLightY = 0.8F,
                  .directionTowardLightZ = 0.3F},
        .maximumDistanceMeters = 40.0F,
        .depthPaddingMeters = 6.0F,
    };
}

[[nodiscard]] bool allFinite(const std::array<float, 16>& matrix) noexcept
{
    return std::ranges::all_of(matrix,
                               [](float value) { return std::isfinite(value); });
}

struct HomogeneousPoint final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

[[nodiscard]] HomogeneousPoint transformPoint(const std::array<float, 16>& matrix,
                                              float x, float y, float z) noexcept
{
    return HomogeneousPoint{
        .x = x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12],
        .y = x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13],
        .z = x * matrix[2] + y * matrix[6] + z * matrix[10] + matrix[14],
        .w = x * matrix[3] + y * matrix[7] + z * matrix[11] + matrix[15],
    };
}

TEST(BgfxCascadedDirectionalShadowMathTest, PracticalSplitsUseFrozenLambdaAndMaximumDistance)
{
    const auto splits = computeCascadedDirectionalShadowSplitDepths(0.1F, 40.0F);

    ASSERT_TRUE(splits.has_value()) << splits.error().message;
    EXPECT_FLOAT_EQ(BgfxCascadedDirectionalShadowSplitLambda, 0.65F);
    EXPECT_NEAR((*splits)[0], 3.81694F, 0.0001F);
    EXPECT_NEAR((*splits)[1], 8.31750F, 0.0001F);
    EXPECT_NEAR((*splits)[2], 16.3225F, 0.0001F);
    EXPECT_FLOAT_EQ((*splits)[3], 40.0F);
}

TEST(BgfxCascadedDirectionalShadowMathTest, ProjectionBuildsFourContiguousFiniteCameraSlices)
{
    const auto result = computeCascadedDirectionalShadowProjection(input(), false, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->cascades.size(), 4U);
    float expectedNear = input().camera.nearPlaneMeters;
    for (usize cascadeIndex = 0; cascadeIndex < result->cascades.size(); ++cascadeIndex)
    {
        const auto& cascade = result->cascades[cascadeIndex];
        EXPECT_FLOAT_EQ(cascade.nearDepthMeters, expectedNear);
        EXPECT_FLOAT_EQ(cascade.farDepthMeters, result->splitDepthsMeters[cascadeIndex]);
        EXPECT_TRUE(allFinite(cascade.lightView));
        EXPECT_TRUE(allFinite(cascade.lightProjection));
        EXPECT_TRUE(allFinite(cascade.samplingTransform));
        EXPECT_GT(cascade.bounds.width(), 0.0F);
        EXPECT_GT(cascade.bounds.height(), 0.0F);
        EXPECT_GT(cascade.bounds.depth(), 0.0F);
        expectedNear = cascade.farDepthMeters;
    }
}

TEST(BgfxCascadedDirectionalShadowMathTest, SamplingTransformsMapEachSliceCenterIntoItsAtlasTile)
{
    const BgfxCascadedDirectionalShadowInput shadowInput = input();
    const auto result = computeCascadedDirectionalShadowProjection(shadowInput, false, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    for (usize cascadeIndex = 0; cascadeIndex < result->cascades.size(); ++cascadeIndex)
    {
        const auto& cascade = result->cascades[cascadeIndex];
        const float centerDepth = (cascade.nearDepthMeters + cascade.farDepthMeters) * 0.5F;
        const HomogeneousPoint atlas = transformPoint(
            cascade.samplingTransform, 0.0F, 0.0F, 8.0F - centerDepth);
        ASSERT_GT(atlas.w, 0.0F);
        const float atlasX = atlas.x / atlas.w;
        const float atlasY = atlas.y / atlas.w;
        const float tileMinimumX = cascadeIndex % 2U == 0U ? 0.0F : 0.5F;
        const float tileMinimumY = cascadeIndex < 2U ? 0.0F : 0.5F;
        EXPECT_GE(atlasX, tileMinimumX);
        EXPECT_LE(atlasX, tileMinimumX + 0.5F);
        EXPECT_GE(atlasY, tileMinimumY);
        EXPECT_LE(atlasY, tileMinimumY + 0.5F);
        EXPECT_GE(atlas.z / atlas.w, 0.0F);
        EXPECT_LE(atlas.z / atlas.w, 1.0F);
    }
}

TEST(BgfxCascadedDirectionalShadowMathTest, HomogeneousDepthMapsEverySliceIntoAtlasDepthRange)
{
    const auto result = computeCascadedDirectionalShadowProjection(input(), true, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    for (const auto& cascade : result->cascades)
    {
        ASSERT_TRUE(allFinite(cascade.samplingTransform));
        const std::array<float, 3> sampleDepths{
            cascade.nearDepthMeters,
            (cascade.nearDepthMeters + cascade.farDepthMeters) * 0.5F,
            cascade.farDepthMeters,
        };
        for (float viewDepth : sampleDepths)
        {
            const HomogeneousPoint atlas = transformPoint(
                cascade.samplingTransform, 0.0F, 0.0F, 8.0F - viewDepth);
            ASSERT_GT(atlas.w, 0.0F);
            EXPECT_GE(atlas.z / atlas.w, 0.0F);
            EXPECT_LE(atlas.z / atlas.w, 1.0F);
        }
    }
}

TEST(BgfxCascadedDirectionalShadowMathTest, RejectsDegenerateLightAxis)
{
    auto shadowInput = input();
    shadowInput.light.directionTowardLightX = 0.0F;
    shadowInput.light.directionTowardLightY = 0.0F;
    shadowInput.light.directionTowardLightZ = 0.0F;

    const auto result = computeCascadedDirectionalShadowProjection(shadowInput, false, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

TEST(BgfxCascadedDirectionalShadowMathTest, MaximumDistanceCannotEndBeforeCameraNearPlane)
{
    auto shadowInput = input();
    shadowInput.maximumDistanceMeters = 0.05F;

    const auto result = computeCascadedDirectionalShadowProjection(shadowInput, false, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

TEST(BgfxCascadedDirectionalShadowMathTest, SamplingTransformsAccountForFramebufferOrigin)
{
    const auto topLeft = computeCascadedDirectionalShadowProjection(input(), false, false);
    const auto bottomLeft = computeCascadedDirectionalShadowProjection(input(), false, true);

    ASSERT_TRUE(topLeft.has_value()) << topLeft.error().message;
    ASSERT_TRUE(bottomLeft.has_value()) << bottomLeft.error().message;
    EXPECT_EQ(topLeft->splitDepthsMeters, bottomLeft->splitDepthsMeters);
    for (usize cascadeIndex = 0; cascadeIndex < topLeft->cascades.size(); ++cascadeIndex)
    {
        EXPECT_EQ(topLeft->cascades[cascadeIndex].bounds.width(),
                  bottomLeft->cascades[cascadeIndex].bounds.width());
        EXPECT_EQ(topLeft->cascades[cascadeIndex].lightView,
                  bottomLeft->cascades[cascadeIndex].lightView);
        EXPECT_EQ(topLeft->cascades[cascadeIndex].lightProjection,
                  bottomLeft->cascades[cascadeIndex].lightProjection);
        EXPECT_NE(topLeft->cascades[cascadeIndex].samplingTransform,
                  bottomLeft->cascades[cascadeIndex].samplingTransform);
    }
}

} // namespace
} // namespace Tina::Render::Bgfx
