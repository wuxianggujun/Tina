#include <tina/render/Camera2DProjection.hpp>

#include <gtest/gtest.h>

namespace {

using Tina::Render::Camera2DProjectionQuery;
using Tina::Render::Camera2DSurfaceViewport;
using Tina::Render::FixedWorldHeight2D;
using Tina::Render::PixelPerfect2D;
using Tina::Render::RenderPixelSnapPolicy;
using Tina::Render::makeResolvedCamera2DInput;
using Tina::Render::resolveCamera2DProjection;

[[nodiscard]] Camera2DProjectionQuery baseQuery()
{
    return Camera2DProjectionQuery{
        .stableCameraKey = 1,
        .centerX = 4.0F,
        .centerY = 2.0F,
        .projection = FixedWorldHeight2D{.heightMeters = 6.0F},
        .pixelSnap = RenderPixelSnapPolicy::CameraTranslation,
        .surfaceViewport = Camera2DSurfaceViewport{.pixelWidth = 960, .pixelHeight = 540},
    };
}

} // namespace

TEST(Camera2DProjectionTest, FixedWorldHeightDerivesPpmAndAspectWidth)
{
    // height=6m, fb 960x540 → ppm = 540/6 = 90; width = 6 * (960/540) = 10.666...
    auto result = resolveCamera2DProjection(baseQuery());
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FLOAT_EQ(result->worldHeight, 6.0F);
    EXPECT_FLOAT_EQ(result->worldWidth, 6.0F * (960.0F / 540.0F));
    EXPECT_FLOAT_EQ(result->actualPixelsPerMeter, 90.0F);
    EXPECT_EQ(result->pixelSnap, RenderPixelSnapPolicy::CameraTranslation);
    EXPECT_EQ(result->integerScale, 1U);
}

TEST(Camera2DProjectionTest, FixedWorldHeightResizeChangesPpmAndWorldWidth)
{
    auto query = baseQuery();
    query.surfaceViewport = {.pixelWidth = 1920, .pixelHeight = 1080};
    auto result = resolveCamera2DProjection(query);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FLOAT_EQ(result->worldHeight, 6.0F);
    EXPECT_FLOAT_EQ(result->actualPixelsPerMeter, 180.0F); // 1080/6
    EXPECT_FLOAT_EQ(result->worldWidth, 6.0F * (1920.0F / 1080.0F));
}

TEST(Camera2DProjectionTest, PixelPerfectIntegerScaleAndForcedSnap)
{
    Camera2DProjectionQuery query{
        .stableCameraKey = 7,
        .centerX = 0.0F,
        .centerY = 0.0F,
        .projection =
            PixelPerfect2D{
                .referencePixelsPerMeter = 16.0F,
                .referenceHeightPixels = 270,
            },
        .pixelSnap = RenderPixelSnapPolicy::CameraAndSprites,
        // 540 / 270 = 2 → integerScale 2; actualPPM = 32; worldH = 540/32 = 16.875
        .surfaceViewport = {.pixelWidth = 960, .pixelHeight = 540},
    };
    auto result = resolveCamera2DProjection(query);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->integerScale, 2U);
    EXPECT_FLOAT_EQ(result->actualPixelsPerMeter, 32.0F);
    EXPECT_FLOAT_EQ(result->worldHeight, 540.0F / 32.0F);
    EXPECT_FLOAT_EQ(result->worldWidth, result->worldHeight * (960.0F / 540.0F));
    EXPECT_EQ(result->pixelSnap, RenderPixelSnapPolicy::CameraAndSprites);
}

TEST(Camera2DProjectionTest, PixelPerfectRejectsNonCameraAndSpritesSnap)
{
    Camera2DProjectionQuery query{
        .stableCameraKey = 1,
        .projection = PixelPerfect2D{.referencePixelsPerMeter = 16.0F, .referenceHeightPixels = 288},
        .pixelSnap = RenderPixelSnapPolicy::CameraTranslation,
        .surfaceViewport = {.pixelWidth = 640, .pixelHeight = 360},
    };
    EXPECT_FALSE(resolveCamera2DProjection(query).has_value());
}

TEST(Camera2DProjectionTest, ZeroSurfaceIsStructuredFailure)
{
    auto query = baseQuery();
    query.surfaceViewport = {.pixelWidth = 0, .pixelHeight = 0};
    EXPECT_FALSE(resolveCamera2DProjection(query).has_value());
}

TEST(Camera2DProjectionTest, RejectsInvalidAuthoredHeights)
{
    auto query = baseQuery();
    query.projection = FixedWorldHeight2D{.heightMeters = 0.0F};
    EXPECT_FALSE(resolveCamera2DProjection(query).has_value());

    query.projection = PixelPerfect2D{.referencePixelsPerMeter = 0.0F, .referenceHeightPixels = 288};
    query.pixelSnap = RenderPixelSnapPolicy::CameraAndSprites;
    EXPECT_FALSE(resolveCamera2DProjection(query).has_value());
}

TEST(Camera2DProjectionTest, NormalizedViewportScalesPixelExtent)
{
    auto query = baseQuery();
    // Half-height viewport band: 540 * 0.5 = 270 px high
    query.normalizedViewport = {.x = 0.0F, .y = 0.25F, .width = 1.0F, .height = 0.5F};
    auto result = resolveCamera2DProjection(query);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FLOAT_EQ(result->actualPixelsPerMeter, 270.0F / 6.0F);
    EXPECT_FLOAT_EQ(result->worldWidth, 6.0F * (960.0F / 270.0F));
}

TEST(Camera2DProjectionTest, MakeResolvedCamera2DInputFillsRenderInput)
{
    auto camera = makeResolvedCamera2DInput(baseQuery());
    ASSERT_TRUE(camera.has_value()) << camera.error().message;
    EXPECT_EQ(camera->stableCameraKey, 1U);
    EXPECT_FLOAT_EQ(camera->centerX, 4.0F);
    EXPECT_FLOAT_EQ(camera->centerY, 2.0F);
    EXPECT_FLOAT_EQ(camera->worldHeight, 6.0F);
    EXPECT_FLOAT_EQ(camera->actualPixelsPerMeter, 90.0F);
    EXPECT_EQ(camera->pixelSnap, RenderPixelSnapPolicy::CameraTranslation);
}
