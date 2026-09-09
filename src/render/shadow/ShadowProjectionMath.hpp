#pragma once

#include <tina/math/Mat4.hpp>
#include <tina/render/RenderErrors.hpp>

#include <array>
#include <cmath>

namespace Tina::Render::Shadow::Detail {

// Shadow authoring rejects near-zero axes more strictly than general geometry.
[[nodiscard]] inline Math::Vec3 normalizeShadowAxis(Math::Vec3 value) noexcept
{
    const float squared = Math::dot(value, value);
    if (!std::isfinite(squared) || squared <= 1.0e-12F) return {};
    return value * (1.0F / std::sqrt(squared));
}

[[nodiscard]] constexpr Math::ClipDepthRange depthRange(bool homogeneous) noexcept
{
    return homogeneous ? Math::ClipDepthRange::NegativeOneToOne : Math::ClipDepthRange::ZeroToOne;
}

[[nodiscard]] inline Core::Result<std::array<float, 16>> samplingTransform(
    const Math::Mat4& view, const Math::Mat4& projection,
    bool homogeneousDepth, bool originBottomLeft, float tileScale, Math::Vec2 center) noexcept
{
    const float yScale = (originBottomLeft ? 0.5F : -0.5F) * tileScale;
    const Math::Mat4 crop{{
        0.5F * tileScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, homogeneousDepth ? 0.5F : 1.0F, 0.0F,
        center.x, center.y, homogeneousDepth ? 0.5F : 0.0F, 1.0F,
    }};
    // Column vectors: world -> view -> projection -> texture. Math::multiply
    // accumulates in double and rounds once; no vendor math or global caps.
    const Math::Mat4 sampling = (crop * projection) * view;
    if (!Math::isFinite(sampling))
        return Core::failure(RenderErrorCode::InvalidMesh3DLighting,
                             "Shadow sampling transform is not finite");
    return sampling.columns;
}

} // namespace Tina::Render::Shadow::Detail
