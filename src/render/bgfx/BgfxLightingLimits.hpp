#pragma once

#include "shaders/tina_lighting_limits.sh"
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>

namespace Tina::Render::Detail {

[[nodiscard]] inline Core::Status validateBgfxMesh3DLighting(const Mesh3DLightingDesc& lighting) noexcept
{
    if (lighting.directionalLights.size() > TINA_DIRECTIONAL_LIGHT_SLOTS ||
        lighting.pointLights.size() > TINA_POINT_LIGHT_SLOTS ||
        lighting.spotLights.size() > TINA_SPOT_LIGHT_SLOTS)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Mesh3D lighting exceeds this bgfx shader's uniform slots");
    }
    return validateMesh3DLightingDesc(lighting);
}

[[nodiscard]] inline Core::Status validateBgfxSprite2DLighting(const Sprite2DLightingDesc& lighting) noexcept
{
    if (lighting.pointLights.size() > TINA_SPRITE_POINT_LIGHT_SLOTS ||
        lighting.shadowSegments.size() > TINA_SPRITE_SHADOW_SEGMENT_SLOTS)
    {
        return Core::failure(RenderErrorCode::InvalidSprite2DLighting,
                             "Sprite2D lighting exceeds this bgfx shader's uniform slots");
    }
    return validateSprite2DLightingDesc(lighting);
}

} // namespace Tina::Render::Detail
