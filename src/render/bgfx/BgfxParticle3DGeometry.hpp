#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <span>

namespace Tina::Render::Bgfx {

// Corners arrive already expanded into world space, so the particle program uses
// u_viewProj and never receives a per-draw model transform.
struct BgfxParticle3DVertex final {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float textureU = 0.0F;
    float textureV = 0.0F;
    u32 abgr = 0;
};

struct BgfxParticle3DFrameRequirements final {
    u32 particleCount = 0;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u32 batchCount = 0;
};

// The camera-facing billboard basis. Both axes are unit length and orthogonal to
// the view direction, so a quad built from them faces the camera exactly.
struct BgfxParticle3DBillboardBasis final {
    float rightX = 1.0F;
    float rightY = 0.0F;
    float rightZ = 0.0F;
    float upX = 0.0F;
    float upY = 1.0F;
    float upZ = 0.0F;
};

[[nodiscard]] Core::Result<BgfxParticle3DBillboardBasis>
particle3DBillboardBasis(const RenderPerspectiveCamera& camera) noexcept;

// Validates every particle texture ref reachable from the transparent draw list.
// Applies to suspended (non-drawing) frames too.
[[nodiscard]] Core::Status validateParticle3DFrameResources(
    RenderSceneView scene, FrameResourceTableView resources) noexcept;

// Validates the particle contract before the backend commits surface state or
// allocates transient geometry. batchCount counts maximal runs of adjacent
// particle draws sharing (texture, blendMode); a mesh draw between two particles
// always breaks the run, because the back-to-front order must be preserved.
[[nodiscard]] Core::Result<BgfxParticle3DFrameRequirements>
checkedParticle3DFrame(RenderSceneView scene, FrameResourceTableView resources);

// Allocation-free and transactional: the scene and exact output extents are
// validated before the first write. Geometry is emitted in transparent draw
// order, not particle array order, so the Nth particle draw owns vertices
// [4N, 4N+4) and indices [6N, 6N+6). Indices are always absolute u32 indices.
[[nodiscard]] Core::Result<BgfxParticle3DFrameRequirements>
writeParticle3DGeometry(RenderSceneView scene, FrameResourceTableView resources,
                        std::span<BgfxParticle3DVertex> vertices, std::span<u32> indices);

} // namespace Tina::Render::Bgfx
