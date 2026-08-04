#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <array>

namespace Tina::Render::Bgfx {

inline constexpr usize BgfxCascadedDirectionalShadowCascadeCount = 4U;
inline constexpr float BgfxCascadedDirectionalShadowSplitLambda = 0.65F;
static_assert(BgfxCascadedDirectionalShadowCascadeCount ==
              Mesh3DCascadedDirectionalShadow::CascadeCount);

struct BgfxCascadedDirectionalShadowBounds final {
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

struct BgfxCascadedDirectionalShadowInput final {
    RenderPerspectiveCamera camera{};
    Mesh3DDirectionalLight light{};
    float maximumDistanceMeters = 50.0F;
    float depthPaddingMeters = 10.0F;
};

struct BgfxCascadedDirectionalShadowCascade final {
    BgfxCascadedDirectionalShadowBounds bounds{};
    float nearDepthMeters = 0.0F;
    float farDepthMeters = 0.0F;
    std::array<float, 16> lightView{};
    std::array<float, 16> lightProjection{};
    // World position -> atlas UV/depth, including backend crop and tile rules.
    std::array<float, 16> samplingTransform{};
};

struct BgfxCascadedDirectionalShadowProjection final {
    std::array<BgfxCascadedDirectionalShadowCascade,
               BgfxCascadedDirectionalShadowCascadeCount>
        cascades{};
    // Positive view-space far depth for each cascade.
    std::array<float, BgfxCascadedDirectionalShadowCascadeCount> splitDepthsMeters{};
};

[[nodiscard]] Core::Result<std::array<float, BgfxCascadedDirectionalShadowCascadeCount>>
computeCascadedDirectionalShadowSplitDepths(float nearDepthMeters,
                                            float farDepthMeters) noexcept;

[[nodiscard]] Core::Result<BgfxCascadedDirectionalShadowProjection>
computeCascadedDirectionalShadowProjection(
    const BgfxCascadedDirectionalShadowInput& input,
    bool homogeneousDepth,
    bool originBottomLeft) noexcept;

} // namespace Tina::Render::Bgfx
