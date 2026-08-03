#pragma once

#include <tina/render/RenderScene.hpp>

namespace Tina::Render::Detail {

// CPU reference for the fixed-cost shader calculation. Returns the visible
// fraction of one finite line-source interval after one shadow segment.
[[nodiscard]] float sprite2DShadowSegmentVisibility(
    float fragmentX,
    float fragmentY,
    const Sprite2DPointLight& light,
    const Sprite2DShadowSegment& segment) noexcept;

} // namespace Tina::Render::Detail
