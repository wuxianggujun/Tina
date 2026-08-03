#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Render {

Core::Status validateMesh3DLightingDesc(const Mesh3DLightingDesc& lighting) noexcept
{
    if (lighting.directionalLights.size() > Mesh3DLightingDesc::MaximumDirectionalLightCount)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Mesh3D directional light count exceeds the fixed device limit");
    }
    if (lighting.pointLights.size() > Mesh3DLightingDesc::MaximumPointLightCount)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Mesh3D point light count exceeds the fixed device limit");
    }
    if (!std::isfinite(lighting.ambientScale) || lighting.ambientScale < 0.0F)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Mesh3D ambient scale must be finite and non-negative");
    }

    for (const Mesh3DDirectionalLight& light : lighting.directionalLights)
    {
        if (!std::isfinite(light.directionTowardLightX) ||
            !std::isfinite(light.directionTowardLightY) ||
            !std::isfinite(light.directionTowardLightZ) || !std::isfinite(light.colorR) ||
            !std::isfinite(light.colorG) || !std::isfinite(light.colorB) || light.colorR < 0.0F ||
            light.colorG < 0.0F || light.colorB < 0.0F)
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Mesh3D directional light values must be finite and RGB non-negative");
        }

        const float directionLengthSquared =
            light.directionTowardLightX * light.directionTowardLightX +
            light.directionTowardLightY * light.directionTowardLightY +
            light.directionTowardLightZ * light.directionTowardLightZ;
        if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 1.0e-12F)
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Mesh3D directional light direction must have a finite non-zero length");
        }
    }

    for (const Mesh3DPointLight& light : lighting.pointLights)
    {
        if (!std::isfinite(light.positionX) || !std::isfinite(light.positionY) ||
            !std::isfinite(light.positionZ) || !std::isfinite(light.influenceRadius) ||
            light.influenceRadius <= 0.0F || !std::isfinite(light.colorR) ||
            !std::isfinite(light.colorG) || !std::isfinite(light.colorB) || light.colorR < 0.0F ||
            light.colorG < 0.0F || light.colorB < 0.0F)
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Mesh3D point light position/radius/RGB must be finite with a positive radius and non-negative RGB");
        }
    }

    return Core::success();
}

} // namespace Tina::Render
