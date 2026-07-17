#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/RenderSurface.hpp>

#include <optional>

namespace Tina::Render {

struct RenderFrame final {
    u64 frameIndex = 0;
    double interpolation = 0.0;
    std::optional<RenderSurfaceState> primaryWindowSurface{};
};

} // namespace Tina::Render
