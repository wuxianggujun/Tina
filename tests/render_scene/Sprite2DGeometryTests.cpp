#include <tina/render/Sprite2DGeometry.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <numbers>

namespace Tina::Render {
namespace {

TEST(Sprite2DGeometryTests, NonDefaultBasisRoundTripsElevation)
{
    const IsometricProjection2D basis{2.5F, 0.8F, 0.6F, 14.0F};
    ASSERT_TRUE(basis.isValid());
    const IsometricGridPoint2D source{7.25F, -3.5F, 2.0F};
    const auto restored = basis.unproject(basis.project(source), source.elevation);
    EXPECT_NEAR(restored.x, source.x, 1.0e-5F);
    EXPECT_NEAR(restored.y, source.y, 1.0e-5F);
    EXPECT_FLOAT_EQ(restored.elevation, source.elevation);
}

TEST(Sprite2DGeometryTests, BillboardRotatesPivotOffsetAfterProjectingAnchor)
{
    const Sprite2DProjection projection{IsometricProjection2D{2.0F, 1.0F, 0.75F, 12.0F}};
    const auto quad = projection.billboard({
        .positionX = 2.0F, .positionY = 3.0F, .elevation = 4.0F,
        .rotationRadians = std::numbers::pi_v<float> * 0.5F,
        .widthMeters = 4.0F, .heightMeters = 2.0F, .pivotX = 0.0F, .pivotY = 0.0F});
    ASSERT_TRUE(quad.isValid());
    EXPECT_NEAR(quad.centerX, -2.0F, 1.0e-6F);
    EXPECT_NEAR(quad.centerY, 7.5F, 1.0e-6F);
    EXPECT_NEAR(quad.halfAxisXX, 0.0F, 1.0e-6F);
    EXPECT_NEAR(quad.halfAxisXY, 2.0F, 1.0e-6F);
    EXPECT_NEAR(quad.halfAxisYX, -1.0F, 1.0e-6F);
    EXPECT_NEAR(quad.halfAxisYY, 0.0F, 1.0e-6F);
}

TEST(Sprite2DGeometryTests, GroundProjectsBothAxesAndPreservesSignedScalePicking)
{
    const Sprite2DProjection projection{IsometricProjection2D{}};
    const auto quad = projection.ground(makeSprite2DQuad({
        .positionX = 0.5F, .positionY = 0.5F, .scaleX = -1.0F}));
    ASSERT_TRUE(quad.isValid());
    EXPECT_FLOAT_EQ(quad.centerX, 0.0F);
    EXPECT_FLOAT_EQ(quad.centerY, 0.375F);
    EXPECT_FLOAT_EQ(quad.halfAxisXX, -0.375F);
    EXPECT_FLOAT_EQ(quad.halfAxisXY, -0.1875F);
    EXPECT_FLOAT_EQ(quad.halfAxisYX, -0.375F);
    EXPECT_FLOAT_EQ(quad.halfAxisYY, 0.1875F);
    EXPECT_TRUE(quad.contains(0.0F, 0.375F));
    EXPECT_FALSE(quad.contains(0.7F, 0.7F)); // Inside its AABB, outside the diamond.
}

TEST(Sprite2DGeometryTests, QuadRejectsNonFiniteDegenerateAndOverflowedCorners)
{
    auto quad = Sprite2DQuad{};
    quad.halfAxisYX = quad.halfAxisXX;
    quad.halfAxisYY = quad.halfAxisXY;
    EXPECT_FALSE(quad.isValid());
    EXPECT_FALSE(quad.contains(0.0F, 0.0F));
    quad = {};
    quad.centerX = (std::numeric_limits<float>::quiet_NaN)();
    EXPECT_FALSE(quad.isValid());
    quad.centerX = (std::numeric_limits<float>::max)();
    quad.halfAxisXX = (std::numeric_limits<float>::max)();
    EXPECT_FALSE(quad.isValid());
    // A full width may exceed float while each represented half axis is finite.
    quad = makeSprite2DQuad({.widthMeters = (std::numeric_limits<float>::max)(), .scaleX = 2.0F});
    EXPECT_TRUE(quad.isValid());
}

TEST(Sprite2DGeometryTests, DepthRetainsFractionalHeightAndNeverSaturatesToIntegerKey)
{
    const IsometricProjection2D basis{};
    EXPECT_GT(basis.sortDepth({2.0F, 3.0F, 1.0F}), basis.sortDepth({2.0F, 3.0F, 0.0F}));
    EXPECT_GT(basis.sortDepth({2.0F, 3.0F, 0.001F}), basis.sortDepth({2.0F, 3.0F, 0.0F}));
    EXPECT_LT(basis.sortDepth({100'000.0F, 100'000.0F, 0.0F}), basis.sortDepth({99'999.0F, 100'000.0F, 0.0F}));
    auto invalid = basis;
    invalid.tileWidthMeters = 0.0F;
    EXPECT_FALSE(invalid.isValid());
    invalid = basis;
    invalid.elevationStepMeters = -0.01F;
    EXPECT_FALSE(invalid.isValid());
    invalid.elevationStepMeters = 0.0F;
    EXPECT_TRUE(invalid.isValid());
}

} // namespace
} // namespace Tina::Render
