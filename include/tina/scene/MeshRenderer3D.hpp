#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned mesh draw component. Stores copyable weak AssetHandles and
// semantic fields only; it never owns an AssetLease or backend/GPU handle.
struct MeshRenderer3D final {
    Asset::AssetHandle mesh{};
    Asset::AssetHandle material{};
    u32 submeshIndex = 0;
    Render::RenderBoundingSphereInput localBounds{.radius = 0.5F};
    Render::RenderLinearColor baseColorFactor{};
    Render::Mesh3DAlphaMode alphaMode = Render::Mesh3DAlphaMode::Opaque;
    bool doubleSided = false;
    bool visible = true;

    friend constexpr bool operator==(const MeshRenderer3D&, const MeshRenderer3D&) noexcept = default;
};

[[nodiscard]] inline bool isValid(const MeshRenderer3D& mesh) noexcept
{
    if (!std::isfinite(mesh.localBounds.centerX) || !std::isfinite(mesh.localBounds.centerY)
        || !std::isfinite(mesh.localBounds.centerZ) || !std::isfinite(mesh.localBounds.radius)
        || mesh.localBounds.radius <= 0.0F)
    {
        return false;
    }
    if (!std::isfinite(mesh.baseColorFactor.red) || !std::isfinite(mesh.baseColorFactor.green)
        || !std::isfinite(mesh.baseColorFactor.blue) || !std::isfinite(mesh.baseColorFactor.alpha)
        || !Render::isSupportedMesh3DAlphaMode(mesh.alphaMode))
    {
        return false;
    }
    return true;
}

} // namespace Tina::Scene
