#include "BgfxLightingLimits.hpp"
#include <gtest/gtest.h>
#include <array>

namespace Tina::Render {

TEST(BgfxLightingLimitsTests, MeshUniformSlotsHaveOneSharedCppAndShaderBoundary)
{
    const std::array<Mesh3DDirectionalLight, TINA_DIRECTIONAL_LIGHT_SLOTS + 1> directional{};
    const std::array<Mesh3DPointLight, TINA_POINT_LIGHT_SLOTS + 1> points{};
    const std::array<Mesh3DSpotLight, TINA_SPOT_LIGHT_SLOTS + 1> spots{};
    const Mesh3DLightingDesc atLimit{
        std::span(directional).first(TINA_DIRECTIONAL_LIGHT_SLOTS),
        std::span(points).first(TINA_POINT_LIGHT_SLOTS),
        std::span(spots).first(TINA_SPOT_LIGHT_SLOTS)};
    EXPECT_TRUE(Detail::validateBgfxMesh3DLighting(atLimit));
    EXPECT_TRUE(validateMesh3DLightingDesc({directional, points, spots}));
    EXPECT_FALSE(Detail::validateBgfxMesh3DLighting({.directionalLights = directional}));
    EXPECT_FALSE(Detail::validateBgfxMesh3DLighting({.pointLights = points}));
    EXPECT_FALSE(Detail::validateBgfxMesh3DLighting({.spotLights = spots}));
}

TEST(BgfxLightingLimitsTests, SpriteUniformSlotsRejectOverflowWithoutTruncation)
{
    const std::array<Sprite2DPointLight, TINA_SPRITE_POINT_LIGHT_SLOTS + 1> lights{};
    const std::array<Sprite2DShadowSegment, TINA_SPRITE_SHADOW_SEGMENT_SLOTS + 1> segments{};
    EXPECT_TRUE(Detail::validateBgfxSprite2DLighting({
        std::span(lights).first(TINA_SPRITE_POINT_LIGHT_SLOTS),
        std::span(segments).first(TINA_SPRITE_SHADOW_SEGMENT_SLOTS)}));
    EXPECT_TRUE(validateSprite2DLightingDesc({lights, segments}));
    EXPECT_FALSE(Detail::validateBgfxSprite2DLighting({.pointLights = lights}));
    EXPECT_FALSE(Detail::validateBgfxSprite2DLighting({.shadowSegments = segments}));
}

} // namespace Tina::Render
