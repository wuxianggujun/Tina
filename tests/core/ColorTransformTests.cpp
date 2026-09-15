#include <tina/core/color/ColorTransform.hpp>
#include <gtest/gtest.h>

#include <limits>

namespace Tina::Tests {

TEST(ColorTransformTests, IdentityAndSignedHdrChannelsArePreserved)
{
    constexpr Core::ColorTransform identity;
    EXPECT_EQ(identity.multiply, (Core::ColorRgba{1, 1, 1, 1}));
    EXPECT_EQ(identity.add, (Core::ColorRgba{0, 0, 0, 0}));
    constexpr Core::ColorTransform invert{{-1, -1, -1, 1}, {1, 1, 1, 0}};
    EXPECT_TRUE(Core::isValidColorTransform(invert));
    EXPECT_EQ(Core::ColorTransform::fromChannels(invert.channels()), invert);
    EXPECT_TRUE(Core::isValidColorTransform({{2, 3, 4, 1}, {0.5F, -0.5F, 1, 0.2F}}));
    EXPECT_FLOAT_EQ(Core::ColorRgba::fromBytes(255, 128, 0).green, 128.0F / 255.0F);
}

TEST(ColorTransformTests, RejectsNonFiniteChannelsAndEndpointOverflow)
{
    for (Core::usize index = 0; index < 8; ++index) {
        auto values = Core::ColorTransform{}.channels();
        values[index] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(Core::isValidColorTransform(Core::ColorTransform::fromChannels(values)));
        values[index] = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(Core::isValidColorTransform(Core::ColorTransform::fromChannels(values)));
    }
    Core::ColorTransform transform;
    transform.multiply.red = (std::numeric_limits<float>::max)();
    transform.add.red = (std::numeric_limits<float>::max)();
    EXPECT_FALSE(Core::isValidColorTransform(transform));
}

TEST(ColorTransformTests, TransparencyExaminesBothAlphaEndpoints)
{
    Core::ColorTransform transform;
    transform.multiply.alpha = 0;
    EXPECT_TRUE(Core::isFullyTransparent(transform));
    transform.add.alpha = 0.25F;
    EXPECT_FALSE(Core::isFullyTransparent(transform));
    transform.multiply.alpha = -1;
    EXPECT_FALSE(Core::isFullyTransparent(transform));
    transform.add.alpha = 0;
    EXPECT_TRUE(Core::isFullyTransparent(transform));
    transform.multiply.alpha = 1;
    transform.add.alpha = -1;
    EXPECT_TRUE(Core::isFullyTransparent(transform));
}

TEST(ColorTransformTests, InterpolatesAllEightChannelsWithoutByteQuantization)
{
    const Core::ColorTransform first{{-1, 2, 0, 1}, {1, 0, -2, 0}};
    const Core::ColorTransform second{{3, 0, 2, 0}, {-1, 2, 0, 1}};
    EXPECT_EQ(Core::interpolateColorTransform(first, second, 0), first);
    EXPECT_EQ(Core::interpolateColorTransform(first, second, 1), second);
    EXPECT_EQ(Core::interpolateColorTransform(first, second, 0.5F),
              (Core::ColorTransform{{1, 1, 1, 0.5F}, {0, 1, -1, 0.5F}}));
}

} // namespace Tina::Tests
