#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <span>

namespace Tina::Render::Bgfx {

struct BgfxUIDisplayVertex final {
    float x = 0.0F;
    float y = 0.0F;
    u32 abgr = 0;
};

struct BgfxUIDisplayGeometryRequirements final {
    usize vertexCount = 0;
    usize indexCount = 0;
};

// Validates every command before reporting the exact fixed-output counts.
// The returned counts are safe for usize multiplication and absolute u32
// indices used by writeGeometry().
[[nodiscard]] Core::Result<BgfxUIDisplayGeometryRequirements>
checkedGeometryRequirements(UIDisplayListView displayList);

// Allocation-free on success. Both output capacities and every input command
// are validated before the first write, so failure never publishes partial
// geometry or modifies either output span.
[[nodiscard]] Core::Result<BgfxUIDisplayGeometryRequirements>
writeGeometry(UIDisplayListView displayList, std::span<BgfxUIDisplayVertex> vertices,
              std::span<u32> indices);

} // namespace Tina::Render::Bgfx
