#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>

namespace Tina::Render::Bgfx {

struct BgfxTransientVertexRequest final {
    u32 count = 0;
    u16 stride = 0;
};

[[nodiscard]] Core::Result<u32>
checkedTransientVertexBudget(std::span<const BgfxTransientVertexRequest> requests);

} // namespace Tina::Render::Bgfx
