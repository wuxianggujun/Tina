#include <tina/scene/CameraFollow2D.hpp>

#include <tina/scene/SceneErrors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace Tina::Scene {
namespace {

TEST(CameraFollow2DTest, RejectsInvalidConfigurationAndStepWithoutMutation)
{
    auto invalidDeadZone = CameraFollow2D::Create({
        .deadZoneHalfExtentsMeters = {-1.0F, 0.0F},
    });
    ASSERT_FALSE(invalidDeadZone);
    EXPECT_EQ(invalidDeadZone.error().code, SceneErrorCode::InvalidComponent);

    auto controller = CameraFollow2D::Create({.initialCenter = {2.0F, 3.0F}});
    ASSERT_TRUE(controller) << (controller ? "" : controller.error().message);
    const CameraFollowPoint2D before = controller->currentCenter();
    const Core::Status invalidStep = controller->fixedUpdate({
        .target = {std::numeric_limits<float>::infinity(), 0.0F},
        .viewportHalfExtentsMeters = {2.0F, 1.0F},
        .fixedDelta = Core::Duration{1.0 / 60.0},
    });
    ASSERT_FALSE(invalidStep);
    EXPECT_EQ(invalidStep.error().code, SceneErrorCode::InvalidComponent);
    EXPECT_EQ(controller->previousCenter(), before);
    EXPECT_EQ(controller->currentCenter(), before);
}

TEST(CameraFollow2DTest, DeadZoneAndMaximumSpeedProduceDeterministicSimulationCenters)
{
    auto controller = CameraFollow2D::Create({
        .initialCenter = {0.0F, 0.0F},
        .deadZoneHalfExtentsMeters = {1.0F, 0.5F},
        .maximumSpeedMetersPerSecond = 2.0F,
    });
    ASSERT_TRUE(controller) << (controller ? "" : controller.error().message);

    ASSERT_TRUE(controller->fixedUpdate({
        .target = {0.5F, 0.25F},
        .viewportHalfExtentsMeters = {2.0F, 1.0F},
        .fixedDelta = Core::Duration{0.5},
    }));
    EXPECT_EQ(controller->currentCenter(), (CameraFollowPoint2D{}));

    ASSERT_TRUE(controller->fixedUpdate({
        .target = {5.0F, 0.0F},
        .viewportHalfExtentsMeters = {2.0F, 1.0F},
        .fixedDelta = Core::Duration{0.5},
    }));
    EXPECT_FLOAT_EQ(controller->previousCenter().x, 0.0F);
    EXPECT_FLOAT_EQ(controller->currentCenter().x, 1.0F);
    EXPECT_FLOAT_EQ(controller->currentCenter().y, 0.0F);
}

TEST(CameraFollow2DTest, ClampsToWorldAndCentersViewportWhenWorldIsSmaller)
{
    auto controller = CameraFollow2D::Create({});
    ASSERT_TRUE(controller) << (controller ? "" : controller.error().message);
    ASSERT_TRUE(controller->fixedUpdate({
        .target = {100.0F, -100.0F},
        .viewportHalfExtentsMeters = {2.0F, 1.0F},
        .worldBounds = CameraFollowBounds2D{
            .minimum = {0.0F, 0.0F},
            .maximum = {8.0F, 4.0F},
        },
        .fixedDelta = Core::Duration{1.0},
    }));
    EXPECT_EQ(controller->currentCenter(), (CameraFollowPoint2D{6.0F, 1.0F}));

    ASSERT_TRUE(controller->snapTo(
        {100.0F, 100.0F},
        {5.0F, 3.0F},
        CameraFollowBounds2D{
            .minimum = {0.0F, 0.0F},
            .maximum = {8.0F, 4.0F},
        }));
    EXPECT_EQ(controller->currentCenter(), (CameraFollowPoint2D{4.0F, 2.0F}));
    EXPECT_EQ(controller->previousCenter(), controller->currentCenter());

    const float maximum = std::numeric_limits<float>::max();
    ASSERT_TRUE(controller->snapTo(
        {0.0F, 0.0F},
        {maximum, maximum},
        CameraFollowBounds2D{
            .minimum = {-maximum, -maximum},
            .maximum = {maximum * 0.5F, maximum * 0.5F},
        }));
    EXPECT_TRUE(std::isfinite(controller->currentCenter().x));
    EXPECT_TRUE(std::isfinite(controller->currentCenter().y));
    EXPECT_EQ(controller->previousCenter(), controller->currentCenter());
}

TEST(CameraFollow2DTest, InterpolatesPreviousAndCurrentAndRejectsInvalidAlpha)
{
    auto controller = CameraFollow2D::Create({});
    ASSERT_TRUE(controller) << (controller ? "" : controller.error().message);
    ASSERT_TRUE(controller->fixedUpdate({
        .target = {4.0F, 2.0F},
        .viewportHalfExtentsMeters = {1.0F, 1.0F},
        .fixedDelta = Core::Duration{1.0},
    }));

    auto center = controller->interpolatedCenter(0.25);
    ASSERT_TRUE(center);
    EXPECT_FLOAT_EQ(center->x, 1.0F);
    EXPECT_FLOAT_EQ(center->y, 0.5F);

    auto invalid = controller->interpolatedCenter(1.01);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, SceneErrorCode::InvalidComponent);
}

} // namespace
} // namespace Tina::Scene
