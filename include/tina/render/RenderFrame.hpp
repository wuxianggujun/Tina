#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Render {

struct RenderFrame final {
    u64 frameIndex = 0;
    double interpolation = 0.0;
    bool surfaceSuspended = false;
};

} // namespace Tina::Render
