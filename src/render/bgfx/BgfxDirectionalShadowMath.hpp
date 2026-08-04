#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

namespace Tina::Render::Bgfx {

struct BgfxDirectionalShadowBounds final {
    float minX = 0.0F;
    float maxX = 0.0F;
    float minY = 0.0F;
    float maxY = 0.0F;
    float minZ = 0.0F;
    float maxZ = 0.0F;

    [[nodiscard]] float width() const noexcept { return maxX - minX; }
    [[nodiscard]] float height() const noexcept { return maxY - minY; }
    [[nodiscard]] float depth() const noexcept { return maxZ - minZ; }
};

struct BgfxDirectionalShadowBoundsInput final {
    RenderPerspectiveCamera camera{};
    Mesh3DDirectionalLight light{};
    float shadowDistance = 50.0F;
    float depthPadding = 10.0F;
};

// Computes the light-space orthographic bounds for the current camera slice.
// The result is backend-private because view/projection matrix conventions are
// owned by bgfx; callers build the final matrix from these finite bounds.
[[nodiscard]] Core::Result<BgfxDirectionalShadowBounds>
computeDirectionalShadowBounds(const BgfxDirectionalShadowBoundsInput& input) noexcept;

} // namespace Tina::Render::Bgfx
