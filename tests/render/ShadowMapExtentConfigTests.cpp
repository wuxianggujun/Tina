#include <tina/render/RenderErrors.hpp>
#include <tina/render/ShadowMapExtentConfig.hpp>

#include <gtest/gtest.h>

#include <array>

namespace Tina::Render {
namespace {

TEST(ShadowMapExtentConfigTest, DefaultsMatchTheCurrentProductQuality)
{
    const ShadowMapExtentConfig config{};

    EXPECT_EQ(config.directionalCascadeTileExtent, 1024U);
    EXPECT_EQ(config.directionalAtlasExtent(), 2048U);
    EXPECT_EQ(config.spotLightMapExtent, 1024U);
    EXPECT_EQ(config.pointLightFaceExtent, 512U);
    EXPECT_TRUE(validateShadowMapExtentConfig(config).has_value());
}

TEST(ShadowMapExtentConfigTest, AcceptsBoundedPowerOfTwoExtents)
{
    for (const u16 extent : std::array{
             ShadowMapExtentConfig::MinimumExtent,
             u16{256},
             u16{1024},
             ShadowMapExtentConfig::MaximumExtent,
         })
    {
        const ShadowMapExtentConfig config{
            .directionalCascadeTileExtent = extent,
            .spotLightMapExtent = extent,
            .pointLightFaceExtent = extent,
        };
        EXPECT_TRUE(validateShadowMapExtentConfig(config).has_value());
    }
}

TEST(ShadowMapExtentConfigTest, RejectsEachInvalidExtentField)
{
    for (const u16 invalidExtent : std::array<u16, 3>{0, 384, 8192})
    {
        auto directional = ShadowMapExtentConfig{};
        directional.directionalCascadeTileExtent = invalidExtent;
        auto directionalResult = validateShadowMapExtentConfig(directional);
        ASSERT_FALSE(directionalResult.has_value());
        EXPECT_EQ(directionalResult.error().code,
                  RenderErrorCode::InvalidShadowMapExtentConfig);

        auto spot = ShadowMapExtentConfig{};
        spot.spotLightMapExtent = invalidExtent;
        auto spotResult = validateShadowMapExtentConfig(spot);
        ASSERT_FALSE(spotResult.has_value());
        EXPECT_EQ(spotResult.error().code,
                  RenderErrorCode::InvalidShadowMapExtentConfig);

        auto point = ShadowMapExtentConfig{};
        point.pointLightFaceExtent = invalidExtent;
        auto pointResult = validateShadowMapExtentConfig(point);
        ASSERT_FALSE(pointResult.has_value());
        EXPECT_EQ(pointResult.error().code,
                  RenderErrorCode::InvalidShadowMapExtentConfig);
    }
}

} // namespace
} // namespace Tina::Render
