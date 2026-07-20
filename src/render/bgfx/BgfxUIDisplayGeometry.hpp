#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <span>

namespace Tina::Render::Bgfx {

// Unified UI vertex: SolidQuad uses UV (0,0)-(1,1) with white page; Glyph uses
// atlas UV in normalized 0..1 space derived from atlasUv texels / page size.
struct BgfxUIDisplayVertex final {
    float x = 0.0F;
    float y = 0.0F;
    u32 abgr = 0;
    float u = 0.0F;
    float v = 0.0F;
};

struct BgfxUIDisplayGeometryRequirements final {
    usize vertexCount = 0;
    usize indexCount = 0;
};

// Geometry-only page size (no bgfx types) so Null/geometry tests stay free of
// the private bgfx header.
struct UIAtlasPageSize final {
    u32 width = 0;
    u32 height = 0;
};

// Page sizes for normalizing Glyph atlas UV. Page 0 is required when any Glyph
// command is present. Missing sizes fail with Unsupported/InvalidDrawCommand.
struct BgfxUIAtlasPageTable final {
    static constexpr usize MaxPages = 8;
    UIAtlasPageSize pages[MaxPages]{};
    usize pageCount = 0;
};

// Validates every command before reporting the exact fixed-output counts.
// The returned counts are safe for usize multiplication and absolute u32
// indices used by writeGeometry().
[[nodiscard]] Core::Result<BgfxUIDisplayGeometryRequirements>
checkedGeometryRequirements(UIDisplayListView displayList);

// Allocation-free on success. Both output capacities and every input command
// are validated before the first write, so failure never publishes partial
// geometry or modifies either output span.
// atlasPages is required when the list contains Glyph commands.
[[nodiscard]] Core::Result<BgfxUIDisplayGeometryRequirements>
writeGeometry(UIDisplayListView displayList, std::span<BgfxUIDisplayVertex> vertices,
              std::span<u32> indices, BgfxUIAtlasPageTable atlasPages = {});

} // namespace Tina::Render::Bgfx
