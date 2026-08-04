#include "BgfxSpotLightShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Tina::Render::Bgfx {
namespace {

[[nodiscard]] BgfxSpotLightShadowInput input() noexcept
{
    return BgfxSpotLightShadowInput{
        .light = {
            .positionX = 2.0F,
            .positionY = 3.0F,
            .positionZ = 4.0F,
            .influenceRadius = 20.0F,
            .directionFromLightX = 0.0F,
            .directionFromLightY = 0.0F,
            .directionFromLightZ = -1.0F,
            .outerConeCosine = 0.8660254F,
        },
        .nearPlaneMeters = 0.25F,
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

[[nodiscard]] HomogeneousPoint transformPoint(
    const std::array<float, 16>& matrix,
    float x, float y, float z) noexcept
{
    return {
        .x = x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12],
        .y = x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13],
        .z = x * matrix[2] + y * matrix[6] + z * matrix[10] + matrix[14],
        .w = x * matrix[3] + y * matrix[7] + z * matrix[11] + matrix[15],
    };
}

TEST(BgfxSpotLightShadowMathTest, BuildsFinitePerspectiveFromSpotConeAndRange)
{
    const auto result = computeSpotLightShadowProjection(input(), false, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_NEAR(result->fieldOfViewDegrees, 60.0F, 0.001F);
    EXPECT_FLOAT_EQ(result->nearPlaneMeters, 0.25F);
    EXPECT_FLOAT_EQ(result->farPlaneMeters, 20.0F);
    EXPECT_TRUE(allFinite(result->lightView));
    EXPECT_TRUE(allFinite(result->lightProjection));
    EXPECT_TRUE(allFinite(result->samplingTransform));
}

TEST(BgfxSpotLightShadowMathTest, SupportsLegalWideConeAboveNinetyDegreeFullFov)
{
    auto shadowInput = input();
    shadowInput.light.outerConeCosine = 0.5F;

    const auto result = computeSpotLightShadowProjection(shadowInput, false, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_NEAR(result->fieldOfViewDegrees, 120.0F, 0.001F);
    EXPECT_TRUE(allFinite(result->lightProjection));
}

TEST(BgfxSpotLightShadowMathTest, MapsCenterlineAndDepthIntoShadowTexture)
{
    for (bool homogeneousDepth : {false, true})
    {
        const auto result = computeSpotLightShadowProjection(
            input(), homogeneousDepth, false);
        ASSERT_TRUE(result.has_value()) << result.error().message;

        for (float distance : {result->nearPlaneMeters,
                               (result->nearPlaneMeters + result->farPlaneMeters) * 0.5F,
                               result->farPlaneMeters})
        {
            const HomogeneousPoint shadow = transformPoint(
                result->samplingTransform, 2.0F, 3.0F, 4.0F - distance);
            ASSERT_GT(shadow.w, 0.0F);
            EXPECT_NEAR(shadow.x / shadow.w, 0.5F, 0.0001F);
            EXPECT_NEAR(shadow.y / shadow.w, 0.5F, 0.0001F);
            EXPECT_GE(shadow.z / shadow.w, 0.0F);
            EXPECT_LE(shadow.z / shadow.w, 1.0F);
        }
    }
}

TEST(BgfxSpotLightShadowMathTest, SamplingTransformAccountsForFramebufferOrigin)
{
    const auto topLeft = computeSpotLightShadowProjection(input(), false, false);
    const auto bottomLeft = computeSpotLightShadowProjection(input(), false, true);

    ASSERT_TRUE(topLeft.has_value()) << topLeft.error().message;
    ASSERT_TRUE(bottomLeft.has_value()) << bottomLeft.error().message;
    EXPECT_EQ(topLeft->lightView, bottomLeft->lightView);
    EXPECT_EQ(topLeft->lightProjection, bottomLeft->lightProjection);
    EXPECT_NE(topLeft->samplingTransform, bottomLeft->samplingTransform);

    const HomogeneousPoint top = transformPoint(
        topLeft->samplingTransform, 2.0F, 4.0F, -6.0F);
    const HomogeneousPoint bottom = transformPoint(
        bottomLeft->samplingTransform, 2.0F, 4.0F, -6.0F);
    ASSERT_GT(top.w, 0.0F);
    ASSERT_GT(bottom.w, 0.0F);
    EXPECT_NE(top.y / top.w, bottom.y / bottom.w);
    EXPECT_NEAR(top.y / top.w + bottom.y / bottom.w, 1.0F, 0.0001F);
}

TEST(BgfxSpotLightShadowMathTest, RejectsDegenerateDirection)
{
    auto shadowInput = input();
    shadowInput.light.directionFromLightX = 0.0F;
    shadowInput.light.directionFromLightY = 0.0F;
    shadowInput.light.directionFromLightZ = 0.0F;

    const auto result = computeSpotLightShadowProjection(shadowInput, false, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

TEST(BgfxSpotLightShadowMathTest, RejectsNearPlaneOutsideInfluenceRange)
{
    auto shadowInput = input();
    shadowInput.nearPlaneMeters = shadowInput.light.influenceRadius;

    const auto result = computeSpotLightShadowProjection(shadowInput, false, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

TEST(BgfxSpotLightShadowMathTest, RejectsHalfAngleAtNinetyDegrees)
{
    auto shadowInput = input();
    shadowInput.light.outerConeCosine = 0.0F;

    const auto result = computeSpotLightShadowProjection(shadowInput, false, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

} // namespace
} // namespace Tina::Render::Bgfx
