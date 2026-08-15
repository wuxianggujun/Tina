#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/render/RenderScene.hpp>

#include <cmath>

namespace Tina::Scene {

// Scene-owned skinned draw intent. Like MeshRenderer3D, this component stores
// copyable weak handles and semantic fields only. Animator3D owns the CPU pose;
// extraction copies that pose into the packet-local Render palette.
struct SkinnedMeshRenderer3D final {
    Asset::AssetHandle mesh{};
    Asset::AssetHandle material{};
    u32 submeshIndex = 0;
    Render::RenderBoundingSphereInput localBounds{.radius = 0.5F};
    Render::RenderLinearColor baseColorFactor{};
    Render::Mesh3DAlphaMode alphaMode = Render::Mesh3DAlphaMode::Opaque;
    bool doubleSided = false;
    bool visible = true;
};

[[nodiscard]] inline bool isValid(const SkinnedMeshRenderer3D& mesh) noexcept
{
    if (!std::isfinite(mesh.localBounds.centerX) || !std::isfinite(mesh.localBounds.centerY)
        || !std::isfinite(mesh.localBounds.centerZ) || !std::isfinite(mesh.localBounds.radius)
        || mesh.localBounds.radius <= 0.0F) {
        return false;
    }
    return std::isfinite(mesh.baseColorFactor.red)
        && std::isfinite(mesh.baseColorFactor.green)
        && std::isfinite(mesh.baseColorFactor.blue)
        && std::isfinite(mesh.baseColorFactor.alpha)
        && Render::isSupportedMesh3DAlphaMode(mesh.alphaMode);
}

} // namespace Tina::Scene
