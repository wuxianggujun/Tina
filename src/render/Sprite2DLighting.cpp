#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Render {

Core::Status validateSprite2DLightingDesc(const Sprite2DLightingDesc& lighting) noexcept
{
    if (lighting.pointLights.size() > Sprite2DLightingDesc::MaximumPointLightCount)
    {
        return Core::failure(RenderErrorCode::InvalidSprite2DLighting,
                             "Sprite2D point light count exceeds the fixed frame limit");
    }
    if (!std::isfinite(lighting.ambientScale) || lighting.ambientScale < 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidSprite2DLighting,
                             "Sprite2D ambient scale must be finite and non-negative");
    }

    for (const Sprite2DPointLight& light : lighting.pointLights)
    {
        if (!std::isfinite(light.positionX) || !std::isfinite(light.positionY) ||
            !std::isfinite(light.radiusMeters) || light.radiusMeters <= 0.0F ||
            !std::isfinite(light.colorR) || !std::isfinite(light.colorG) ||
            !std::isfinite(light.colorB) || light.colorR < 0.0F || light.colorG < 0.0F ||
            light.colorB < 0.0F)
        {
            return Core::failure(
                RenderErrorCode::InvalidSprite2DLighting,
                "Sprite2D point light position/radius must be finite with positive radius and non-negative RGB");
        }
    }

    return Core::success();
}

} // namespace Tina::Render
