#include <tina/render/ShadowMapExtentConfig.hpp>

constexpr Tina::Render::ShadowMapExtentConfig DefaultShadowMapExtents{};
static_assert(DefaultShadowMapExtents.directionalCascadeTileExtent == 1024);
static_assert(DefaultShadowMapExtents.spotLightMapExtent == 1024);
static_assert(DefaultShadowMapExtents.pointLightFaceExtent == 512);
static_assert(DefaultShadowMapExtents.directionalAtlasExtent() == 2048);
