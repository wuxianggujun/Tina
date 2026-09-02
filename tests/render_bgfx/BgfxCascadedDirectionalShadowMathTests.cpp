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

TEST(BgfxCascadedDirectionalShadowMathTest, CascadeExtentIgnoresCameraPositionAndOrientation)
{
    auto moved = input();
    moved.camera.positionX = 137.5F;
    moved.camera.positionY = -22.25F;
    moved.camera.positionZ = 61.75F;
    auto turned = input();
    turned.camera.forwardX = 0.6F;
    turned.camera.forwardY = -0.3F;
    turned.camera.forwardZ = -0.74F;

    const auto reference = computeCascadedDirectionalShadowProjection(input(), false, false);
    const auto translated = computeCascadedDirectionalShadowProjection(moved, false, false);
    const auto rotated = computeCascadedDirectionalShadowProjection(turned, false, false);

    ASSERT_TRUE(reference.has_value()) << reference.error().message;
    ASSERT_TRUE(translated.has_value()) << translated.error().message;
    ASSERT_TRUE(rotated.has_value()) << rotated.error().message;
    for (usize cascadeIndex = 0; cascadeIndex < reference->cascades.size(); ++cascadeIndex)
    {
        const auto& expected = reference->cascades[cascadeIndex];
        // A window that resizes as the camera turns has no stable texel grid to snap to, so
        // the extent must depend on the split depths and the lens alone.
        EXPECT_FLOAT_EQ(translated->cascades[cascadeIndex].bounds.width(), expected.bounds.width());
        EXPECT_FLOAT_EQ(rotated->cascades[cascadeIndex].bounds.width(), expected.bounds.width());
        EXPECT_FLOAT_EQ(expected.bounds.height(), expected.bounds.width());
        EXPECT_FLOAT_EQ(translated->cascades[cascadeIndex].texelSizeMeters,
                        expected.texelSizeMeters);
        EXPECT_FLOAT_EQ(rotated->cascades[cascadeIndex].texelSizeMeters, expected.texelSizeMeters);
    }
}

TEST(BgfxCascadedDirectionalShadowMathTest, CameraTranslationMovesBoundsInWholeTexelSteps)
{
    // Sub-texel camera motion is the case that matters: unsnapped bounds would follow it
    // continuously and re-quantize the shadow map every frame, which is what makes edges crawl.
    auto nudged = input();
    nudged.camera.positionX += 0.017F;
    nudged.camera.positionZ -= 0.0413F;

    const auto reference = computeCascadedDirectionalShadowProjection(input(), false, false);
    const auto shifted = computeCascadedDirectionalShadowProjection(nudged, false, false);

    ASSERT_TRUE(reference.has_value()) << reference.error().message;
    ASSERT_TRUE(shifted.has_value()) << shifted.error().message;
    for (usize cascadeIndex = 0; cascadeIndex < reference->cascades.size(); ++cascadeIndex)
    {
        const auto& before = reference->cascades[cascadeIndex];
        const auto& after = shifted->cascades[cascadeIndex];
        ASSERT_GT(before.texelSizeMeters, 0.0F);
        for (float delta : {after.bounds.minX - before.bounds.minX,
                            after.bounds.minY - before.bounds.minY})
        {
            const float steps = delta / before.texelSizeMeters;
            EXPECT_NEAR(steps, std::round(steps), 1.0e-3F)
                << "cascade " << cascadeIndex << " slid by a fraction of a texel";
        }
    }
}

TEST(BgfxCascadedDirectionalShadowMathTest, SnappedWindowStillEnclosesTheWholeCameraSlice)
{
    // Snapping shifts the window off the slice centre, so the extent has to carry enough slack
    // to keep every frustum corner inside; a corner that falls outside loses its caster.
    const BgfxCascadedDirectionalShadowInput shadowInput = input();
    const auto result = computeCascadedDirectionalShadowProjection(shadowInput, false, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    const float tangent =
        std::tan(shadowInput.camera.verticalFovDegrees * 3.14159265358979323846F / 360.0F);
    for (const auto& cascade : result->cascades)
    {
        for (const float depth : {cascade.nearDepthMeters, cascade.farDepthMeters})
        {
            const float halfHeight = depth * tangent;
            const float halfWidth = halfHeight * shadowInput.camera.aspectRatio;
            for (const float horizontal : {-halfWidth, halfWidth})
            {
                for (const float vertical : {-halfHeight, halfHeight})
                {
                    // Camera looks down -Z from z=8 with +Y up, so a corner is a direct offset.
                    const HomogeneousPoint atlas = transformPoint(
                        cascade.samplingTransform, horizontal, vertical, 8.0F - depth);
                    ASSERT_GT(atlas.w, 0.0F);
                    EXPECT_GE(atlas.x / atlas.w, 0.0F);
                    EXPECT_LE(atlas.x / atlas.w, 1.0F);
                    EXPECT_GE(atlas.y / atlas.w, 0.0F);
                    EXPECT_LE(atlas.y / atlas.w, 1.0F);
                    EXPECT_GE(atlas.z / atlas.w, 0.0F);
                    EXPECT_LE(atlas.z / atlas.w, 1.0F);
                }
            }
        }
    }
}

TEST(BgfxCascadedDirectionalShadowMathTest, TexelSizeTracksTheConfiguredTileExtent)
{
    auto coarse = input();
    coarse.tileExtent = 512;
    auto fine = input();
    fine.tileExtent = 2048;

    const auto coarseResult = computeCascadedDirectionalShadowProjection(coarse, false, false);
    const auto fineResult = computeCascadedDirectionalShadowProjection(fine, false, false);

    ASSERT_TRUE(coarseResult.has_value()) << coarseResult.error().message;
    ASSERT_TRUE(fineResult.has_value()) << fineResult.error().message;
    for (usize cascadeIndex = 0; cascadeIndex < coarseResult->cascades.size(); ++cascadeIndex)
    {
        const auto& coarseCascade = coarseResult->cascades[cascadeIndex];
        const auto& fineCascade = fineResult->cascades[cascadeIndex];
        // The snap grid is the atlas grid, so a quarter-resolution tile must yield a texel
        // roughly four times as wide; the slack term keeps it from being exactly four.
        EXPECT_GT(coarseCascade.texelSizeMeters, fineCascade.texelSizeMeters * 3.9F);
        EXPECT_LT(coarseCascade.texelSizeMeters, fineCascade.texelSizeMeters * 4.1F);
        EXPECT_NEAR(coarseCascade.bounds.width() / coarseCascade.texelSizeMeters, 512.0F, 0.01F);
        EXPECT_NEAR(fineCascade.bounds.width() / fineCascade.texelSizeMeters, 2048.0F, 0.01F);
    }
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
