#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned opaque mesh draw component. Stores semantic fields only (no GPU
// handles). meshKey/materialKey are backend-neutral bind table ids (set via
// setMesh3DBinding / setMesh3DMaterialTextureBinding). Full AssetHandle on the
// component + extract-time resolve remains Deferred.
struct MeshRenderer3D final {
    u32 meshKey = 0;
    u32 materialKey = 0;
    u32 submeshIndex = 0;
    Render::RenderBoundingSphereInput localBounds{.radius = 0.5F};
    Render::RenderLinearColor baseColorFactor{};
    bool doubleSided = false;
    bool visible = true;

    friend constexpr bool operator==(const MeshRenderer3D&, const MeshRenderer3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const MeshRenderer3D& mesh) noexcept
{
    if (mesh.meshKey == 0 || mesh.materialKey == 0)
    {
        return false;
    }
    if (!std::isfinite(mesh.localBounds.centerX) || !std::isfinite(mesh.localBounds.centerY)
        || !std::isfinite(mesh.localBounds.centerZ) || !std::isfinite(mesh.localBounds.radius)
        || mesh.localBounds.radius <= 0.0F)
    {
        return false;
    }
    if (!std::isfinite(mesh.baseColorFactor.red) || !std::isfinite(mesh.baseColorFactor.green)
        || !std::isfinite(mesh.baseColorFactor.blue) || !std::isfinite(mesh.baseColorFactor.alpha))
    {
        return false;
    }
    return true;
}

} // namespace Tina::Scene
