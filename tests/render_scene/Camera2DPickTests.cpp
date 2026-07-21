#include <gtest/gtest.h>

#include <tina/render/Camera2DPick.hpp>
#include <tina/render/RenderErrors.hpp>

#include <cmath>
#include <limits>

namespace Tina::Tests {
namespace {

using Render::Camera2DPickQuery;
using Render::RenderCamera2D;
using Render::pickWorldFromLogicalPointer;

[[nodiscard]] RenderCamera2D baseCamera() noexcept
{
    return RenderCamera2D{
        .stableCameraKey = 7,
        .centerX = 0.0F,
        .centerY = 0.0F,
        .rotationRadians = 0.0F,
        .worldWidth = 10.0F,
        .worldHeight = 10.0F,
        .actualPixelsPerMeter = 64.0F,
        .normalizedViewport = {.x = 0.0F, .y = 0.0F, .width = 1.0F, .height = 1.0F},
    };
}

[[nodiscard]] Camera2DPickQuery queryAt(double logicalX, double logicalY, RenderCamera2D camera = baseCamera())
{
    return Camera2DPickQuery{
        .logicalX = logicalX,
        .logicalY = logicalY,
        .logicalWidth = 100,
        .logicalHeight = 100,
        .camera = camera,
        .cameraRevision = 11,
        .surfaceRevision = 22,
        .inputSequence = 33,
    };
}

TEST(Camera2DPickTest, CenterLogicalMapsToCameraCenter)
{
    auto sample = pickWorldFromLogicalPointer(queryAt(50.0, 50.0));
    ASSERT_TRUE(sample.has_value()) << (sample ? "" : sample.error().message);
    EXPECT_TRUE(sample->hit);
    EXPECT_FLOAT_EQ(sample->worldX, 0.0F);
    EXPECT_FLOAT_EQ(sample->worldY, 0.0F);
    EXPECT_EQ(sample->cameraRevision, 11U);
    EXPECT_EQ(sample->surfaceRevision, 22U);
    EXPECT_EQ(sample->inputSequence, 33U);
    EXPECT_EQ(sample->stableCameraKey, 7U);
}

TEST(Camera2DPickTest, TopLeftAndBottomRightCornersMapWithYUpWorld)
{
    auto topLeft = pickWorldFromLogicalPointer(queryAt(0.0, 0.0));
    ASSERT_TRUE(topLeft.has_value()) << (topLeft ? "" : topLeft.error().message);
    ASSERT_TRUE(topLeft->hit);
    EXPECT_FLOAT_EQ(topLeft->worldX, -5.0F);
    EXPECT_FLOAT_EQ(topLeft->worldY, 5.0F);

    auto bottomRight = pickWorldFromLogicalPointer(queryAt(99.0, 99.0));
    ASSERT_TRUE(bottomRight.has_value()) << (bottomRight ? "" : bottomRight.error().message);
    ASSERT_TRUE(bottomRight->hit);
    EXPECT_NEAR(bottomRight->worldX, 4.9F, 1.0e-4F);
    EXPECT_NEAR(bottomRight->worldY, -4.9F, 1.0e-4F);
}

TEST(Camera2DPickTest, OutsideViewportIsExplicitNoHit)
{
    RenderCamera2D camera = baseCamera();
    camera.normalizedViewport = {.x = 0.25F, .y = 0.25F, .width = 0.5F, .height = 0.5F};

    auto miss = pickWorldFromLogicalPointer(queryAt(10.0, 10.0, camera));
    ASSERT_TRUE(miss.has_value()) << (miss ? "" : miss.error().message);
    EXPECT_FALSE(miss->hit);
    EXPECT_EQ(miss->stableCameraKey, 7U);
    EXPECT_EQ(miss->inputSequence, 33U);

    auto hit = pickWorldFromLogicalPointer(queryAt(50.0, 50.0, camera));
    ASSERT_TRUE(hit.has_value()) << (hit ? "" : hit.error().message);
    EXPECT_TRUE(hit->hit);
    EXPECT_FLOAT_EQ(hit->worldX, 0.0F);
    EXPECT_FLOAT_EQ(hit->worldY, 0.0F);
}

TEST(Camera2DPickTest, RightEdgeOfViewportIsExclusiveNoHit)
{
    auto edge = pickWorldFromLogicalPointer(queryAt(100.0, 50.0));
    ASSERT_TRUE(edge.has_value()) << (edge ? "" : edge.error().message);
    EXPECT_FALSE(edge->hit);
}

TEST(Camera2DPickTest, RotationQuarterTurnMapsAxes)
{
    RenderCamera2D camera = baseCamera();
    camera.rotationRadians = static_cast<float>(3.14159265358979323846 * 0.5);

    // Logical right of center (u=1,v=0.5) is camera-local +X before rotation.
    auto sample = pickWorldFromLogicalPointer(queryAt(100.0 - 1.0e-9, 50.0, camera));
    ASSERT_TRUE(sample.has_value()) << (sample ? "" : sample.error().message);
    ASSERT_TRUE(sample->hit);
    EXPECT_NEAR(sample->worldX, 0.0F, 1.0e-3F);
    EXPECT_NEAR(sample->worldY, 5.0F, 1.0e-3F);
}

TEST(Camera2DPickTest, TranslatedCameraKeepsRelativeOffset)
{
    RenderCamera2D camera = baseCamera();
    camera.centerX = 3.0F;
    camera.centerY = -1.5F;

    auto sample = pickWorldFromLogicalPointer(queryAt(50.0, 50.0, camera));
    ASSERT_TRUE(sample.has_value()) << (sample ? "" : sample.error().message);
    ASSERT_TRUE(sample->hit);
    EXPECT_FLOAT_EQ(sample->worldX, 3.0F);
    EXPECT_FLOAT_EQ(sample->worldY, -1.5F);
}

TEST(Camera2DPickTest, RejectsInvalidCameraAndExtentAndCoords)
{
    {
        auto zeroExtent = pickWorldFromLogicalPointer(Camera2DPickQuery{
            .logicalX = 1.0,
            .logicalY = 1.0,
            .logicalWidth = 0,
            .logicalHeight = 100,
            .camera = baseCamera(),
        });
        ASSERT_FALSE(zeroExtent.has_value());
        EXPECT_EQ(zeroExtent.error().code, Render::RenderErrorCode::InvalidRenderSceneInput);
    }
    {
        RenderCamera2D invalid = baseCamera();
        invalid.worldWidth = 0.0F;
        auto badCamera = pickWorldFromLogicalPointer(queryAt(50.0, 50.0, invalid));
        ASSERT_FALSE(badCamera.has_value());
        EXPECT_EQ(badCamera.error().code, Render::RenderErrorCode::InvalidRenderSceneInput);
    }
    {
        const double unrepresentable =
            static_cast<double>((std::numeric_limits<float>::max)()) * 2.0;
        auto badCoord = pickWorldFromLogicalPointer(queryAt(unrepresentable, 0.0));
        ASSERT_FALSE(badCoord.has_value());
        EXPECT_EQ(badCoord.error().code, Render::RenderErrorCode::InvalidRenderSceneInput);
    }
    {
        auto nanCoord = pickWorldFromLogicalPointer(queryAt(std::numeric_limits<double>::quiet_NaN(), 0.0));
        ASSERT_FALSE(nanCoord.has_value());
        EXPECT_EQ(nanCoord.error().code, Render::RenderErrorCode::InvalidRenderSceneInput);
    }
}

TEST(Camera2DPickTest, SampleIsStableWhenCameraWouldChangeLater)
{
    auto locked = pickWorldFromLogicalPointer(queryAt(25.0, 75.0));
    ASSERT_TRUE(locked.has_value()) << (locked ? "" : locked.error().message);
    ASSERT_TRUE(locked->hit);

    RenderCamera2D moved = baseCamera();
    moved.centerX = 100.0F;
    moved.centerY = 100.0F;
    // Re-querying with a different camera is a different mapping; the product
    // contract is that Action Mapping keeps the first locked sample instead of
    // re-projecting. Prove the two results differ so consumers must latch.
    auto reprojected = pickWorldFromLogicalPointer(queryAt(25.0, 75.0, moved));
    ASSERT_TRUE(reprojected.has_value()) << (reprojected ? "" : reprojected.error().message);
    ASSERT_TRUE(reprojected->hit);
    EXPECT_NE(locked->worldX, reprojected->worldX);
    EXPECT_NE(locked->worldY, reprojected->worldY);
}

} // namespace
} // namespace Tina::Tests
