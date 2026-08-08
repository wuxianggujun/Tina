#include <tina/editor/EditorViewportNavigation.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace Tina::Editor {
namespace {

TEST(EditorViewportNavigationTests, Pan2DUsesCurrentZoomAndAdvancesRevision)
{
    auto navigation = EditorViewportNavigation::Create(
        {},
        EditorViewport2DNavigationState{.center = {2.0F, -1.0F}, .zoom = 2.0F});
    ASSERT_TRUE(navigation);

    ASSERT_TRUE(navigation->pan2D({100.0F, -40.0F}));
    EXPECT_FLOAT_EQ(navigation->twoD().center.x, 1.5F);
    EXPECT_FLOAT_EQ(navigation->twoD().center.y, -1.2F);
    EXPECT_FLOAT_EQ(navigation->twoD().zoom, 2.0F);
    EXPECT_EQ(navigation->revision(), 2U);
}

TEST(EditorViewportNavigationTests, Zoom2DKeepsPointerWorldAnchorStationary)
{
    EditorViewportNavigationConfig config{};
    config.twoDZoomStepFactor = 2.0F;
    auto navigation = EditorViewportNavigation::Create(config);
    ASSERT_TRUE(navigation);

    ASSERT_TRUE(navigation->zoom2D(1.0F, {1000.0F, 800.0F}, {750.0F, 200.0F}));
    EXPECT_FLOAT_EQ(navigation->twoD().zoom, 2.0F);
    EXPECT_FLOAT_EQ(navigation->twoD().center.x, 1.25F);
    EXPECT_FLOAT_EQ(navigation->twoD().center.y, 1.0F);

    const float worldX = navigation->twoD().center.x + 250.0F * 0.005F;
    const float worldY = navigation->twoD().center.y + 200.0F * 0.005F;
    EXPECT_FLOAT_EQ(worldX, 2.5F);
    EXPECT_FLOAT_EQ(worldY, 2.0F);
}

TEST(EditorViewportNavigationTests, OrbitPanAndDollyUpdatePersistent3DState)
{
    EditorViewportNavigationConfig config{};
    config.threeDOrbitRadiansPerPixel = 0.1F;
    config.threeDPanWorldUnitsPerPixelAtUnitDistance = 0.01F;
    config.threeDDollyStepFactor = 2.0F;
    auto navigation = EditorViewportNavigation::Create(config);
    ASSERT_TRUE(navigation);

    ASSERT_TRUE(navigation->pan3D({2.0F, 3.0F}));
    EXPECT_NEAR(navigation->threeD().target.x, -0.2F, 0.00001F);
    EXPECT_NEAR(navigation->threeD().target.y, 0.3F, 0.00001F);
    EXPECT_NEAR(navigation->threeD().target.z, 0.0F, 0.00001F);

    ASSERT_TRUE(navigation->orbit3D({10.0F, 1000.0F}));
    EXPECT_NEAR(navigation->threeD().yawRadians, 1.0F, 0.00001F);
    EXPECT_FLOAT_EQ(navigation->threeD().pitchRadians,
                    config.maximumThreeDPitchRadians);

    ASSERT_TRUE(navigation->dolly3D(1.0F));
    EXPECT_FLOAT_EQ(navigation->threeD().distance, 5.0F);
    EXPECT_EQ(navigation->revision(), 4U);
}

TEST(EditorViewportNavigationTests, AppliesBatchAtomicallyAndAdvancesOnce)
{
    auto navigation = EditorViewportNavigation::Create();
    ASSERT_TRUE(navigation);
    const std::array inputs{
        EditorViewportNavigationInput{
            .kind = EditorViewportNavigationInputKind::Pan2D,
            .pixelDelta = {50.0F, 0.0F},
        },
        EditorViewportNavigationInput{
            .kind = EditorViewportNavigationInputKind::Dolly3D,
            .wheelSteps = 2.0F,
        },
    };

    ASSERT_TRUE(navigation->apply(inputs));
    EXPECT_FLOAT_EQ(navigation->twoD().center.x, -0.5F);
    EXPECT_LT(navigation->threeD().distance, 10.0F);
    EXPECT_EQ(navigation->revision(), 2U);
}

TEST(EditorViewportNavigationTests, FailedBatchPreservesBothStatesAndRevision)
{
    auto navigation = EditorViewportNavigation::Create();
    ASSERT_TRUE(navigation);
    const auto before = navigation->snapshot();
    const std::array inputs{
        EditorViewportNavigationInput{
            .kind = EditorViewportNavigationInputKind::Pan2D,
            .pixelDelta = {50.0F, 20.0F},
        },
        EditorViewportNavigationInput{
            .kind = EditorViewportNavigationInputKind::Dolly3D,
            .wheelSteps = (std::numeric_limits<float>::quiet_NaN)(),
        },
    };

    const auto status = navigation->apply(inputs);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, EditorErrorCode::InvalidConfiguration);
    EXPECT_EQ(navigation->snapshot(), before);
}

TEST(EditorViewportNavigationTests, EnforcesBatchCapacityAndIgnoresNoOpInput)
{
    auto navigation = EditorViewportNavigation::Create();
    ASSERT_TRUE(navigation);
    ASSERT_TRUE(navigation->pan2D({}));
    EXPECT_EQ(navigation->revision(), 1U);

    std::array<EditorViewportNavigationInput,
               EditorViewportNavigationLimits::MaximumInputCommandsPerBatch + 1U>
        oversized{};
    const auto status = navigation->apply(oversized);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, EditorErrorCode::InvalidConfiguration);
    EXPECT_EQ(navigation->revision(), 1U);
}

TEST(EditorViewportNavigationTests, RejectsInvalidConfigurationAndInitialState)
{
    auto config = EditorViewportNavigationConfig{};
    config.minimumTwoDZoom = 0.0F;
    auto invalidConfig = EditorViewportNavigation::Create(config);
    ASSERT_FALSE(invalidConfig);
    EXPECT_EQ(invalidConfig.error().code, EditorErrorCode::InvalidConfiguration);

    auto invalidState = EditorViewportNavigation::Create(
        {},
        EditorViewport2DNavigationState{
            .center = {},
            .zoom = (std::numeric_limits<float>::infinity)(),
        });
    ASSERT_FALSE(invalidState);
    EXPECT_EQ(invalidState.error().code, EditorErrorCode::InvalidConfiguration);
}

TEST(EditorViewportNavigationTests, CanonicalizesInitialYawForGridConsumption)
{
    const float initialYaw = std::numbers::pi_v<float> * 2.0F + 0.25F;
    auto navigation = EditorViewportNavigation::Create(
        {}, {}, EditorViewport3DNavigationState{.yawRadians = initialYaw});
    ASSERT_TRUE(navigation);
    EXPECT_NEAR(navigation->threeD().yawRadians, 0.25F, 0.00001F);
    EXPECT_GE(navigation->threeD().yawRadians, -std::numbers::pi_v<float>);
    EXPECT_LE(navigation->threeD().yawRadians, std::numbers::pi_v<float>);
}

} // namespace
} // namespace Tina::Editor
