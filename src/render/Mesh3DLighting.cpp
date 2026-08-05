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
    if (lighting.spotLights.size() > Mesh3DLightingDesc::MaximumSpotLightCount)
    {
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Mesh3D spot light count exceeds the fixed device limit");
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

    if (lighting.cascadedDirectionalShadow.has_value())
    {
        const Mesh3DCascadedDirectionalShadow& shadow = *lighting.cascadedDirectionalShadow;
        if (shadow.directionalLightIndex >= lighting.directionalLights.size())
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Mesh3D cascaded shadow references a missing directional light");
        }
        if (!std::isfinite(shadow.maximumDistanceMeters) || shadow.maximumDistanceMeters <= 0.0F ||
            shadow.maximumDistanceMeters > Mesh3DCascadedDirectionalShadow::MaximumDistanceMeters ||
            !std::isfinite(shadow.depthBias) || shadow.depthBias < 0.0F ||
            shadow.depthBias > Mesh3DCascadedDirectionalShadow::MaximumDepthBias ||
            !std::isfinite(shadow.normalBiasMeters) || shadow.normalBiasMeters < 0.0F ||
            shadow.normalBiasMeters > Mesh3DCascadedDirectionalShadow::MaximumNormalBiasMeters)
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Mesh3D cascaded directional shadow distance and bias values must be finite and bounded");
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

    if (lighting.pointLightShadow.has_value())
    {
        const Mesh3DPointLightShadow& shadow = *lighting.pointLightShadow;
        if (shadow.pointLightIndex >= lighting.pointLights.size())
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Mesh3D point shadow references a missing point light");
        }
        const Mesh3DPointLight& light = lighting.pointLights[shadow.pointLightIndex];
        if (!std::isfinite(shadow.nearPlaneMeters) || shadow.nearPlaneMeters <= 0.0F ||
            shadow.nearPlaneMeters >= light.influenceRadius || !std::isfinite(shadow.depthBias) ||
            shadow.depthBias < 0.0F || shadow.depthBias > Mesh3DPointLightShadow::MaximumDepthBias ||
            !std::isfinite(shadow.normalBiasMeters) || shadow.normalBiasMeters < 0.0F ||
            shadow.normalBiasMeters > Mesh3DPointLightShadow::MaximumNormalBiasMeters)
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Mesh3D point shadow requires finite, bounded near plane and bias values inside the referenced light radius");
        }
    }

    for (const Mesh3DSpotLight& light : lighting.spotLights)
    {
        if (!std::isfinite(light.positionX) || !std::isfinite(light.positionY) ||
            !std::isfinite(light.positionZ) || !std::isfinite(light.influenceRadius) ||
            light.influenceRadius <= 0.0F || !std::isfinite(light.directionFromLightX) ||
            !std::isfinite(light.directionFromLightY) || !std::isfinite(light.directionFromLightZ) ||
            !std::isfinite(light.innerConeCosine) || !std::isfinite(light.outerConeCosine) ||
            light.innerConeCosine < -1.0F || light.innerConeCosine > 1.0F ||
            light.outerConeCosine < -1.0F || light.outerConeCosine > 1.0F ||
            light.innerConeCosine <= light.outerConeCosine || !std::isfinite(light.colorR) ||
            !std::isfinite(light.colorG) || !std::isfinite(light.colorB) || light.colorR < 0.0F ||
            light.colorG < 0.0F || light.colorB < 0.0F)
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Mesh3D spot light position/radius/direction/cone/RGB must be finite with a positive radius, cone cosines in [-1,1] ordered inner > outer, and non-negative RGB");
        }

        const float directionLengthSquared =
            light.directionFromLightX * light.directionFromLightX +
            light.directionFromLightY * light.directionFromLightY +
            light.directionFromLightZ * light.directionFromLightZ;
        if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 1.0e-12F)
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Mesh3D spot light direction must have a finite non-zero length");
        }
    }

    if (lighting.spotLightShadow.has_value())
    {
        const Mesh3DSpotLightShadow& shadow = *lighting.spotLightShadow;
        if (shadow.spotLightIndex >= lighting.spotLights.size())
        {
            return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                                 "Mesh3D spot shadow references a missing spot light");
        }
        const Mesh3DSpotLight& light = lighting.spotLights[shadow.spotLightIndex];
        if (light.outerConeCosine <= 0.0F || !std::isfinite(shadow.nearPlaneMeters) ||
            shadow.nearPlaneMeters <= 0.0F ||
            shadow.nearPlaneMeters >= light.influenceRadius || !std::isfinite(shadow.depthBias) ||
            shadow.depthBias < 0.0F || shadow.depthBias > Mesh3DSpotLightShadow::MaximumDepthBias ||
            !std::isfinite(shadow.normalBiasMeters) || shadow.normalBiasMeters < 0.0F ||
            shadow.normalBiasMeters > Mesh3DSpotLightShadow::MaximumNormalBiasMeters)
        {
            return Core::failure(
                RenderErrorCode::InvalidMesh3DLighting,
                "Mesh3D spot shadow requires a positive outer cone cosine plus finite, bounded near plane and bias values inside the referenced light radius");
        }
    }

    return Core::success();
}

} // namespace Tina::Render
