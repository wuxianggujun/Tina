#include "BgfxPointLightShadowMath.hpp"

#include <tina/render/RenderErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Tina::Render::Bgfx {
namespace {

struct HomogeneousPoint final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

[[nodiscard]] HomogeneousPoint transformPoint(
    const std::array<float, 16>& matrix,
    const std::array<float, 3>& point) noexcept
{
    return {
        .x = point[0] * matrix[0] + point[1] * matrix[4] +
             point[2] * matrix[8] + matrix[12],
        .y = point[0] * matrix[1] + point[1] * matrix[5] +
             point[2] * matrix[9] + matrix[13],
        .z = point[0] * matrix[2] + point[1] * matrix[6] +
             point[2] * matrix[10] + matrix[14],
        .w = point[0] * matrix[3] + point[1] * matrix[7] +
             point[2] * matrix[11] + matrix[15],
    };
}

[[nodiscard]] BgfxPointLightShadowInput input() noexcept
{
    return {
        .light = {
            .positionX = 2.0F,
            .positionY = 3.0F,
            .positionZ = 4.0F,
            .influenceRadius = 20.0F,
        },
        .nearPlaneMeters = 0.25F,
    };
}

TEST(BgfxPointLightShadowMathTest, BuildsSixFiniteNinetyDegreeFaces)
{
    const auto result = computePointLightShadowProjection(input(), false, false);

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FLOAT_EQ(result->nearPlaneMeters, 0.25F);
    EXPECT_FLOAT_EQ(result->farPlaneMeters, 20.0F);
    ASSERT_EQ(result->faces.size(), Mesh3DPointLightShadow::FaceCount);
    for (const BgfxPointLightShadowFace& face : result->faces)
    {
        EXPECT_TRUE(std::ranges::all_of(face.lightView, [](float value) {
            return std::isfinite(value);
        }));
        EXPECT_TRUE(std::ranges::all_of(face.lightProjection, [](float value) {
            return std::isfinite(value);
        }));
        EXPECT_TRUE(std::ranges::all_of(face.samplingTransform, [](float value) {
            return std::isfinite(value);
        }));
    }
}

TEST(BgfxPointLightShadowMathTest, FaceOrderMapsAxisCenterlinesToMapCenters)
{
    constexpr std::array<std::array<float, 3>, BgfxPointLightShadowFaceCount> Directions{
        std::array{1.0F, 0.0F, 0.0F}, std::array{-1.0F, 0.0F, 0.0F},
        std::array{0.0F, 1.0F, 0.0F}, std::array{0.0F, -1.0F, 0.0F},
        std::array{0.0F, 0.0F, 1.0F}, std::array{0.0F, 0.0F, -1.0F}};
    const auto result = computePointLightShadowProjection(input(), false, false);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    for (usize faceIndex = 0; faceIndex < result->faces.size(); ++faceIndex)
    {
        const std::array<float, 3> point{
            input().light.positionX + Directions[faceIndex][0] * 5.0F,
            input().light.positionY + Directions[faceIndex][1] * 5.0F,
            input().light.positionZ + Directions[faceIndex][2] * 5.0F,
        };
        const HomogeneousPoint shadow = transformPoint(
            result->faces[faceIndex].samplingTransform, point);
        ASSERT_GT(shadow.w, 0.0F);
        EXPECT_NEAR(shadow.x / shadow.w, 0.5F, 0.0001F);
        EXPECT_NEAR(shadow.y / shadow.w, 0.5F, 0.0001F);
        EXPECT_GE(shadow.z / shadow.w, 0.0F);
        EXPECT_LE(shadow.z / shadow.w, 1.0F);
    }
}

TEST(BgfxPointLightShadowMathTest, SamplingTransformsAccountForFramebufferOrigin)
{
    const auto topLeft = computePointLightShadowProjection(input(), true, false);
    const auto bottomLeft = computePointLightShadowProjection(input(), true, true);

    ASSERT_TRUE(topLeft.has_value()) << topLeft.error().message;
    ASSERT_TRUE(bottomLeft.has_value()) << bottomLeft.error().message;
    for (usize faceIndex = 0; faceIndex < topLeft->faces.size(); ++faceIndex)
    {
        EXPECT_EQ(topLeft->faces[faceIndex].lightView,
                  bottomLeft->faces[faceIndex].lightView);
        EXPECT_EQ(topLeft->faces[faceIndex].lightProjection,
                  bottomLeft->faces[faceIndex].lightProjection);
        EXPECT_NE(topLeft->faces[faceIndex].samplingTransform,
                  bottomLeft->faces[faceIndex].samplingTransform);
    }
}

TEST(BgfxPointLightShadowMathTest, RejectsNearPlaneOutsideInfluenceRange)
{
    auto shadowInput = input();
    shadowInput.nearPlaneMeters = shadowInput.light.influenceRadius;

    const auto result = computePointLightShadowProjection(shadowInput, false, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, RenderErrorCode::InvalidMesh3DLighting);
}

} // namespace
} // namespace Tina::Render::Bgfx
