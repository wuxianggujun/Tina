#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <array>

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
    float shadowDistanceMeters = 50.0F;
    float depthPadding = 10.0F;
};

struct BgfxDirectionalShadowProjection final {
    BgfxDirectionalShadowBounds bounds{};
    std::array<float, 16> lightView{};
    std::array<float, 16> lightProjection{};
    // World position -> shadow texture UV/depth, including backend crop rules.
    std::array<float, 16> samplingTransform{};
};

// Computes the light-space orthographic bounds for the current camera slice.
// The result is backend-private because view/projection matrix conventions are
// owned by bgfx; callers build the final matrix from these finite bounds.
[[nodiscard]] Core::Result<BgfxDirectionalShadowBounds>
computeDirectionalShadowBounds(const BgfxDirectionalShadowBoundsInput& input) noexcept;

[[nodiscard]] Core::Result<BgfxDirectionalShadowProjection>
computeDirectionalShadowProjection(const BgfxDirectionalShadowBoundsInput& input,
                                   bool homogeneousDepth,
                                   bool originBottomLeft) noexcept;

} // namespace Tina::Render::Bgfx
