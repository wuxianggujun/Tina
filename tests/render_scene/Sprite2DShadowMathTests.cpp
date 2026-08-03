#include "Sprite2DShadowMath.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace Tina::Render::Detail {
namespace {

TEST(Sprite2DShadowMathTest, ZeroSourceRadiusPreservesHardShadowIntersection)
{
    const Sprite2DPointLight light{
        .positionX = 10.0F,
        .radiusMeters = 12.0F,
        .sourceRadiusMeters = 0.0F,
    };

    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(
                        0.0F, 0.0F, light,
                        Sprite2DShadowSegment{
                            .startX = 5.0F, .startY = -1.0F,
                            .endX = 5.0F, .endY = 1.0F,
                        }),
                    0.0F);
    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(
                        0.0F, 0.0F, light,
                        Sprite2DShadowSegment{
                            .startX = 5.0F, .startY = 2.0F,
                            .endX = 5.0F, .endY = 3.0F,
                        }),
                    1.0F);
}

TEST(Sprite2DShadowMathTest, FiniteSourceProducesBoundedPenumbraCoverage)
{
    const Sprite2DPointLight light{
        .positionX = 10.0F,
        .radiusMeters = 12.0F,
        .sourceRadiusMeters = 2.0F,
    };

    const float fullyBlocked = sprite2DShadowSegmentVisibility(
        0.0F, 0.0F, light,
        Sprite2DShadowSegment{
            .startX = 5.0F, .startY = -2.0F,
            .endX = 5.0F, .endY = 2.0F,
        });
    const float penumbra = sprite2DShadowSegmentVisibility(
        0.0F, 0.0F, light,
        Sprite2DShadowSegment{
            .startX = 5.0F, .startY = 0.0F,
            .endX = 5.0F, .endY = 1.0F,
        });
    const float unblocked = sprite2DShadowSegmentVisibility(
        0.0F, 0.0F, light,
        Sprite2DShadowSegment{
            .startX = 5.0F, .startY = 3.0F,
            .endX = 5.0F, .endY = 4.0F,
        });

    EXPECT_FLOAT_EQ(fullyBlocked, 0.0F);
    EXPECT_FLOAT_EQ(penumbra, 0.5F);
    EXPECT_FLOAT_EQ(unblocked, 1.0F);
}

TEST(Sprite2DShadowMathTest, IgnoresSegmentsOutsideFragmentToEmitterDepth)
{
    const Sprite2DPointLight light{
        .positionX = 10.0F,
        .radiusMeters = 12.0F,
        .sourceRadiusMeters = 2.0F,
    };

    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(
                        0.0F, 0.0F, light,
                        Sprite2DShadowSegment{
                            .startX = -2.0F, .startY = -1.0F,
                            .endX = -2.0F, .endY = 1.0F,
                        }),
                    1.0F);
    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(
                        0.0F, 0.0F, light,
                        Sprite2DShadowSegment{
                            .startX = 12.0F, .startY = -1.0F,
                            .endX = 12.0F, .endY = 1.0F,
                        }),
                    1.0F);
}

TEST(Sprite2DShadowMathTest, IsEndpointOrderInvariantAndSourceRadiusControlsCoverage)
{
    const Sprite2DShadowSegment forward{
        .startX = 5.0F, .startY = -0.25F,
        .endX = 5.0F, .endY = 0.25F,
    };
    const Sprite2DShadowSegment reversed{
        .startX = forward.endX, .startY = forward.endY,
        .endX = forward.startX, .endY = forward.startY,
    };
    const Sprite2DPointLight narrow{
        .positionX = 10.0F,
        .radiusMeters = 12.0F,
        .sourceRadiusMeters = 0.5F,
    };
    const Sprite2DPointLight wide{
        .positionX = 10.0F,
        .radiusMeters = 12.0F,
        .sourceRadiusMeters = 1.0F,
    };

    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(0.0F, 0.0F, narrow, forward), 0.0F);
    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(0.0F, 0.0F, wide, forward), 0.5F);
    EXPECT_FLOAT_EQ(sprite2DShadowSegmentVisibility(0.0F, 0.0F, wide, forward),
                    sprite2DShadowSegmentVisibility(0.0F, 0.0F, wide, reversed));
}

TEST(Sprite2DShadowMathTest, MultiplicativeCompositionIsDeterministic)
{
    const Sprite2DPointLight light{
        .positionX = 10.0F,
        .radiusMeters = 12.0F,
        .sourceRadiusMeters = 1.0F,
    };
    const Sprite2DShadowSegment halfBlocker{
        .startX = 5.0F, .startY = 0.0F,
        .endX = 5.0F, .endY = 0.5F,
    };
    const float first =
        sprite2DShadowSegmentVisibility(0.0F, 0.0F, light, halfBlocker);
    const float second =
        sprite2DShadowSegmentVisibility(0.0F, 0.0F, light, halfBlocker);

    EXPECT_FLOAT_EQ(first, 0.5F);
    EXPECT_FLOAT_EQ(first * second, 0.25F);
}

TEST(Sprite2DShadowMathTest, ExtremeCoordinatesRemainFiniteAndBounded)
{
    const Sprite2DPointLight light{
        .positionX = 2.5e38F,
        .radiusMeters = 3.0e38F,
        .sourceRadiusMeters = 2.0e38F,
    };
    const Sprite2DShadowSegment segment{
        .startX = 1.25e38F, .startY = -2.0e38F,
        .endX = 1.25e38F, .endY = 2.0e38F,
    };

    const float visibility = sprite2DShadowSegmentVisibility(0.0F, 0.0F, light, segment);

    EXPECT_TRUE(std::isfinite(visibility));
    EXPECT_GE(visibility, 0.0F);
    EXPECT_LE(visibility, 1.0F);

    const Sprite2DPointLight cancellationLight{
        .positionX = 2.5e28F,
        .radiusMeters = 3.0e28F,
        .sourceRadiusMeters = 6.25e26F,
    };
    const Sprite2DShadowSegment cancellationSegment{
        .startX = 2.5e38F, .startY = 2.5e38F,
        .endX = -2.5e38F, .endY = -2.5e38F,
    };
    const float cancellationVisibility =
        sprite2DShadowSegmentVisibility(0.0F, 0.0F, cancellationLight, cancellationSegment);

    EXPECT_TRUE(std::isfinite(cancellationVisibility));
    EXPECT_GE(cancellationVisibility, 0.0F);
    EXPECT_LE(cancellationVisibility, 1.0F);
}

} // namespace
} // namespace Tina::Render::Detail
